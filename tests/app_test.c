/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors
 * Copyright (C) 2026 Stephen Webster */
/* Host test of GRAIN-FM1 (firmware/app/) on the host platform (tests/host_platform.h):
 *   table     params.c is consistent: ranges, defaults, CCs unique per channel, slots unique, every role present
 *   CC        a knob sends its CC with the new absolute value on the right channel, once per frame (its latest),
 *             clamped at min / max, accelerated when fast; empty slots send nothing; the global and fixed channels
 *   CC in     the v2 hook: a CC from Live sets the value (clamped), is not echoed, overrides one not sent yet
 *   keys      note on / off with velocity 100, the octave, a note-off matching its note-on after an octave change,
 *             app_audio_stop releases every note and held keys stay silent until let go
 *   controls  pages (buttons, SELECT, HOME, wrapping), channel (PRESETS), octave limits, OCT- + OCT+ together
 *   screen    every view drawn (overview, dimmed, zoom, fade back, visualiser), lazy, app_redraw, the layout lint at
 *             the widest values; PNGs in build/shots/
 *   flash     SAVE, autosave, power cycle, another app's record, a changed table, damaged values, no flash
 *   sound     none: silence always
 *   cc -O1 -Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests -o build/host/app_test tests/app_test.c -lm */
#include "host_platform.h"

static int failed;
static void check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    failed += !ok;
}

/* ------------------------------------------------------------ helpers --- */
static int32_t buf[2u * 1024u];

static void tick(void)                                  /* a frame and the ISR after it (MIDI goes out there) */
{
    host_frame();
    host_render(buf, 128);
}

static void rx_tick(void)                               /* MIDI in: the ISR takes it, the next frame applies it */
{
    host_render(buf, 128);
    tick();
}

static void frames(uint32_t n)
{
    while (n--)
        tick();
}

static void drain(void)
{
    uint32_t pkt;
    host_render(buf, 128);
    while (host_midi_out(&pkt))
        ;
}

/* the MIDI the app sent since the last drain: count, and the last one as status / d1 / d2 */
static uint32_t out_n, out_st, out_d1, out_d2, out_cc_n;
static void collect(void)
{
    uint32_t pkt;
    out_n = out_cc_n = 0;
    host_render(buf, 128);
    while (host_midi_out(&pkt)) {
        out_n++;
        out_st = (pkt >> 8) & 0xFFu;
        out_d1 = (pkt >> 16) & 0x7Fu;
        out_d2 = (pkt >> 24) & 0x7Fu;
        out_cc_n += (out_st & 0xF0u) == 0xB0u;
    }
}

static void press(uint32_t label)
{
    host_press(label, 1);
    tick();
    host_press(label, 0);
    tick();
}

static uint32_t ink_rows(uint32_t y0, uint32_t y1)     /* rows in y0..y1 with anything but the background */
{
    uint32_t x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = 0; x < 240u; x++)
            if (host_fb[y * 240u + x] != T_BG) {
                n++;
                break;
            }
    return n;
}

static int region_has(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint16_t c)
{
    uint32_t x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            if (host_fb[y * 240u + x] == c)
                return 1;
    return 0;
}

static void reset_state(void)                           /* the table's defaults, page 1, channel 1, octave 0 */
{
    ctl_defaults();
    memset(&keys, 0, sizeof keys);
    ccq_r = ccq_w = inq_r = inq_w = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    last_buttons = 0;
    saved = 0;
    fm1_ms += 10000u;                                   /* every timer (dim, zoom, message) long gone */
    tick();
    drain();
}

/* ------------------------------------------------------------ table --- */
static void table(void)
{
    uint32_t i, j, ok = 1, slots = 1, uniq = 1, roles = 1;
    for (i = 0; i < NPARAMS; i++) {
        const param_t *p = &PARAMS[i];
        ok &= p->page < NPAGES && p->slot < 4u && p->min < p->max && p->max <= 127u && p->def >= p->min &&
              p->def <= p->max && p->cc <= 119u && p->ch <= 16u && p->fmt <= FMT_BIP && p->role < NROLES &&
              p->name && p->name[0];
        for (j = i + 1u; j < NPARAMS; j++) {
            slots &= !(PARAMS[j].page == p->page && PARAMS[j].slot == p->slot);
            uniq &= !(PARAMS[j].cc == p->cc && PARAMS[j].ch == p->ch);
            roles &= p->role == ROLE_NONE || PARAMS[j].role != p->role;
        }
    }
    check("table: ranges, defaults, CC 0..119, channel 0..16, names", ok);
    check("table: one parameter per page slot", slots);
    check("table: no CC used twice on one channel", uniq);
    check("table: each visualiser role on one parameter at most", roles);
    check("table: Position, Spray, Frequency and Grain Size all present",
          param_of_role(ROLE_POSITION) >= 0 && param_of_role(ROLE_SPRAY) >= 0 && param_of_role(ROLE_FREQ) >= 0 &&
              param_of_role(ROLE_SIZE) >= 0);
    for (i = 0, ok = 1; i < NPAGES; i++) {
        uint32_t b = PAGES[i].button, n = 0;
        for (j = 0; j < 6u; j++)
            n += PAGE_BUTTONS[j] == b;
        ok &= n == 1u && text_w(&AF_M, PAGES[i].name) <= 150;
        for (j = i + 1u; j < NPAGES; j++)
            ok &= PAGES[j].button != b;
    }
    check("table: pages on distinct page buttons, names fit the header", ok);
}

/* ------------------------------------------------------------ CC out --- */
static void cc_out(void)
{
    int32_t pos = param_at(0, 1);
    reset_state();
    check("defaults: Position at 64, Filter Freq at 127", ctl.val[pos] == 64u && ctl.val[param_at(0, 0)] == 127u);

    host_turn(EN_K2, 1);
    tick();
    collect();
    check("KNOB 2 one click: one CC", out_n == 1u);
    check("  CC 21 on channel 1 (B0), value 65", out_st == 0xB0u && out_d1 == 21u && out_d2 == 65u);

    host_turn(EN_K2, -1);
    tick();
    collect();
    check("KNOB 2 back one click: CC 21 = 64", out_n == 1u && out_d2 == 64u);

    host_turn(EN_K2, 3);                                /* 3 clicks in a frame: x2 */
    host_frame();
    host_turn(EN_K2, -1);                               /* .. and one back before the ISR runs */
    host_frame();
    collect();
    check("two frames before the ISR: one CC each (latest values)", out_n == 2u && out_d2 == 69u);
    check("fast turn accelerated: 3 clicks = 6 steps", ctl.val[pos] == 69u);

    host_turn(EN_K2, 1);
    host_turn(EN_K2, -1);                               /* net zero within a frame */
    tick();
    collect();
    check("a click and back in one frame: nothing sent", out_n == 0u);

    host_turn(EN_K2, 200);
    tick();
    collect();
    check("turned past max: clamped at 127, sent once", ctl.val[pos] == 127u && out_n == 1u && out_d2 == 127u);
    host_turn(EN_K2, 1);
    tick();
    collect();
    check("at max, turned further: nothing sent", out_n == 0u);
    host_turn(EN_K2, -500);
    tick();
    collect();
    check("turned past min: clamped at 0", ctl.val[pos] == 0u && out_d2 == 0u);

    host_turn(EN_K1, -1);
    host_turn(EN_K3, 1);
    host_turn(EN_K4, 1);
    tick();
    collect();
    check("three knobs in one frame: three CCs", out_n == 3u && out_cc_n == 3u);

    ctl_set_channel(4);                                 /* channel 5 */
    host_turn(EN_K3, 1);
    tick();
    collect();
    check("global channel 5: CC 22 on B4", out_st == 0xB4u && out_d1 == 22u);
    ctl_set_channel(0);

    {   /* every knob on every page sends its own table row's CC */
        uint32_t p, k, ok = 1;
        for (p = 0; p < NPAGES; p++) {
            ctl_set_page((int32_t)p);
            tick();
            drain();
            for (k = 0; k < 4u; k++) {
                int32_t i = param_at(p, k);
                host_turn(EN_K1 + k, ctl.val[i >= 0 ? i : 0] >= 127u ? -1 : 1);
                tick();
                collect();
                ok &= i < 0 ? out_n == 0u : out_n == 1u && out_d1 == PARAMS[i].cc &&
                                               out_st == (0xB0u | param_ch((uint32_t)i)) && out_d2 == ctl.val[i];
            }
        }
        check("every page, every knob: its row's CC, channel and value", ok);
    }

    {   /* a burst bigger than the ring: nothing lost, the latest values */
        uint32_t i;
        for (i = 0; i < 40u; i++)
            ctl.dirty[i % NPARAMS] = 1;
        ccq_w = ccq_r + QN - 2u;                        /* the ring nearly full (as if the ISR stalled) */
        ctl_flush();
        check("ring nearly full: the rest stays queued", ctl.dirty[NPARAMS - 1u] == 1u);
        ccq_r = ccq_w;
        ctl_flush();
        check("next frame: the rest sent", ctl.dirty[NPARAMS - 1u] == 0u);
        drain();
    }
    ctl_set_page(0);
    tick();
    drain();
}

/* ------------------------------------------------------------ CC in (the v2 hook) --- */
static void cc_in(void)
{
    int32_t pos = param_at(0, 1), spray = param_of_role(ROLE_SPRAY);
    reset_state();
    host_midi(0xB0, 21, 100);
    rx_tick();
    collect();
    check("CC 21 = 100 from Live: Position follows", ctl.val[pos] == 100u);
    check("  not echoed back", out_n == 0u);
    host_midi(0xB1, 21, 10);
    rx_tick();
    check("CC 21 on another channel: ignored", ctl.val[pos] == 100u);
    host_midi(0xB0, 24, 90);
    rx_tick();
    check("a parameter on another page (Spray) follows too", ctl.val[spray] == 90u);
    ctl.val[pos] = 31;                                  /* changed this frame, its CC not flushed yet .. */
    ctl.dirty[pos] = 1;
    host_midi(0xB0, 21, 30);                            /* .. when Live's arrives */
    host_render(buf, 128);
    ctl_take_input();
    check("Live's value wins over one not sent yet", ctl.val[pos] == 30u && !ctl.dirty[pos]);
    tick();
    collect();
    check("  and the stale one is never sent", out_n == 0u);
    host_midi(0x90, 60, 100);                           /* a note in: not a CC */
    rx_tick();
    check("note in: no value changes", ctl.val[pos] == 30u);
    check("MIDI in: the activity dot drawn", region_has(228, 8, 238, 20, T_ACCENT));
    host_turn(EN_K2, 1);
    tick();
    collect();
    check("turned after Live set it: continues from Live's value", out_d2 == 31u);
    midi_in_overflow = 1;
    tick();
    check("MIDI ring overflow: survived, values kept", ctl.val[pos] == 31u && !midi_in_overflow);
}

/* ------------------------------------------------------------ keys --- */
static void key_notes(void)
{
    reset_state();
    fm1_in.notes = 1u;                                  /* F3 */
    tick();
    collect();
    check("key F3: note-on 53, velocity 100, channel 1", out_n == 1u && out_st == 0x90u && out_d1 == 53u && out_d2 == 100u);
    fm1_in.notes = 0;
    tick();
    collect();
    check("released: note-off 53", out_n == 1u && out_st == 0x80u && out_d1 == 53u);

    press(B_OCTUP);
    drain();
    fm1_in.notes = 1u << 7;                             /* C4 + 1 octave */
    tick();
    collect();
    check("OCT+: key C4 sends C5 (72)", out_d1 == 72u && out_st == 0x90u);
    press(B_OCTDN);
    press(B_OCTDN);
    drain();
    fm1_in.notes = 0;
    tick();
    collect();
    check("octave changed while held: the note-off is still 72", out_n == 1u && out_st == 0x80u && out_d1 == 72u);

    ctl_set_channel(2);
    tick();
    drain();
    fm1_in.notes = 1u | 1u << 26;                       /* F3 and G5 at OCT -1 */
    tick();
    collect();
    check("two keys, channel 3, OCT -1: two note-ons on 92", out_n == 2u && out_st == 0x92u);
    ctl_set_channel(0);
    tick();
    app_audio_stop();
    collect();
    check("app_audio_stop: both notes off, on their channel", out_n == 2u && out_st == 0x82u);
    tick();
    collect();
    check("keys still held after the stop: silent", out_n == 0u);
    fm1_in.notes = 0;
    tick();
    fm1_in.notes = 1u;
    tick();
    collect();
    check("let go and pressed again: plays", out_n == 1u && out_st == 0x90u);
    fm1_in.notes = 0;
    tick();
    drain();
    {
        uint32_t i;
        for (i = 0; i < 8u; i++)
            press(B_OCTUP);
        check("octave stops at +3", ctl.octave == 3);
        for (i = 0; i < 8u; i++)
            press(B_OCTDN);
        check("octave stops at -3", ctl.octave == -3);
        fm1_in.notes = 1u;                              /* F3 at -3: MIDI 17 */
        tick();
        collect();
        check("F3 at OCT -3: note 17", out_d1 == 17u);
        fm1_in.notes = 0;
        tick();
        drain();
    }
    host_press(B_OCTDN, 1);                             /* both held (the UBOOT gesture): no octave change */
    tick();
    {
        int8_t o = ctl.octave;
        host_press(B_OCTUP, 1);
        tick();
        check("OCT+ pressed while OCT- held: no change", ctl.octave == o);
    }
    host_press(B_OCTDN, 0);
    host_press(B_OCTUP, 0);
    tick();
}

/* ------------------------------------------------------------ controls --- */
static void controls_test(void)
{
    reset_state();
    press(B_SCL);
    check("SCL: page 2", ctl.page == 1u);
    press(B_HOME);
    check("HOME: page 1", ctl.page == 0u);
    host_turn(EN_SELECT, 1);
    tick();
    check("SELECT +1: page 2", ctl.page == 1u);
    host_turn(EN_SELECT, 1);
    tick();
    check("SELECT past the last page: wraps to page 1", ctl.page == 0u);
    host_turn(EN_SELECT, -1);
    tick();
    check("SELECT -1 from page 1: the last page", ctl.page == NPAGES - 1u);
    press(B_FX);
    check("FX: page 1", ctl.page == 0u);
    press(B_ENV);
    check("ENV (no page in the table): stays, SOON shown", ctl.page == 0u && msg_on());
    check("the current page's button LED lit", host_led[panel.btn[B_FX]] && !host_led[panel.btn[B_SCL]]);
    host_turn(EN_PRESET, 4);
    tick();
    check("PRESETS +4: channel 5 shown", ctl.channel == 4u && msg_on());
    host_turn(EN_PRESET, 40);
    tick();
    check("PRESETS: channel stops at 16", ctl.channel == 15u);
    host_turn(EN_PRESET, -40);
    tick();
    check("PRESETS: channel stops at 1", ctl.channel == 0u);
    press(B_PLAY);
    check("PLAY: SOON", msg_on() && !strcmp(ui.msg, "SOON"));
    host_turn(EN_ALGO, 1);
    tick();
    check("ALGORITHM: SOON", msg_on() && !strcmp(ui.msg, "ALGORITHM SOON"));
    fm1_in.notes = 1u << 3;
    tick();
    check("a held key: its LED lit", host_led[14 + 3] && !host_led[14]);
    fm1_in.notes = 0;
    tick();
    drain();
}

/* ------------------------------------------------------------ screen --- */
static void screen(void)
{
    uint32_t i;
    reset_state();
    memset(host_fb, 0, sizeof host_fb);
    app_redraw = 1;
    tick();
    check("overview: header drawn", ink_rows(0, HDR_H) > 8u);
    check("overview: four slots drawn", ink_rows(GRID_Y, VIS_Y) > 60u);
    check("overview: visualiser drawn", ink_rows(VIS_Y, 240) > 40u && !app_redraw);
    check("overview: the page colour in the slots (arcs)", region_has(0, GRID_Y, 240, VIS_Y, page_col()));
    host_png("build/shots/overview.png");
    frames(2);
    {
        uint32_t b = 0;
        host_frame();                                   /* a frame without a visualiser tick */
        b = host_blits;
        if (ui.frame % VIS_FRAMES == 0u) {
            host_frame();
            b = host_blits;
        }
        check("lazy: an idle frame sends at most the visualiser", b <= 1u);
    }

    {   /* a message stamped at 0 ms (the boot's version) goes after MSG_MS too */
        uint16_t before[240 * HDR_H];
        uint32_t keep = fm1_ms;
        fm1_ms = 0;
        ui_msg("v0.1");
        tick();
        memcpy(before, host_fb, sizeof before);
        fm1_ms = MSG_MS + 16u;
        tick();
        check("a message from 0 ms: gone after MSG_MS (CH / OCT back)", memcmp(before, host_fb, sizeof before) != 0);
        fm1_ms = keep;
        tick();
    }
    host_turn(EN_K2, 5);
    tick();
    check("a knob turned: the zoom shows", zoom_level() == FADE_STEPS + 1u && ui.zoomed);
    check("zoom: the large value drawn in the zoom card", ink_rows(ZOOM_Y + 26, ZOOM_Y + 60) > 15u);
    for (i = 0; i < 20u; i++) {
        host_turn(EN_K2, i & 4u ? -1 : 2);
        tick();
    }
    host_png("build/shots/zoom.png");
    fm1_ms += ZOOM_MS;
    tick();
    check("after ZOOM_MS: fading", zoom_level() > 0u && zoom_level() <= FADE_STEPS);
    host_png("build/shots/zoom_fade.png");
    fm1_ms += FADE_MS * FADE_STEPS;
    tick();
    check("after the fade: the slots again", zoom_level() == 0u && !ui.zoomed);
    check("slots dimmed: the touched one highlighted (SURF)", ui_dimming() && region_has(SLOT_W + 4, GRID_Y + 4,
          SLOT_W + 20, GRID_Y + 20, T_SURF) && !region_has(4, GRID_Y + 4, 20, GRID_Y + 20, T_SURF));
    host_png("build/shots/dimmed.png");
    fm1_ms += DIM_MS;
    tick();
    check("after DIM_MS: no slot highlighted", !ui_dimming() && !region_has(SLOT_W + 4, GRID_Y + 4, SLOT_W + 20,
          GRID_Y + 20, T_SURF));

    {   /* the visualiser follows Position: the playhead (ACCENT) moves */
        int32_t pos = param_at(0, 1);
        uint32_t x, px0 = 999, px1 = 999;
        ctl.val[pos] = 10;
        frames(VIS_FRAMES);
        for (x = 0; x < 240u; x++)
            if (host_fb[(VIS_Y + 30u) * 240u + x] == T_ACCENT) { px0 = x; break; }
        ctl.val[pos] = 120;
        frames(VIS_FRAMES);
        for (x = 0; x < 240u; x++)
            if (host_fb[(VIS_Y + 30u) * 240u + x] == T_ACCENT) { px1 = x; break; }
        check("visualiser: the playhead follows Position", px0 < 30u && px1 > 210u && px1 < 240u);
    }
    {   /* Frequency sets how many grains: count them over a second at its min and max */
        int32_t fq = param_of_role(ROLE_FREQ);
        uint32_t lo = 0, hi = 0, g, t;
        ctl.val[fq] = 0;
        for (t = 0; t < 60u; t++) {
            vis_step();
            for (g = 0; g < NGRAIN; g++)
                lo += ui.grain[g].life == 3u;
        }
        ctl.val[fq] = 127;
        for (t = 0; t < 60u; t++) {
            vis_step();
            for (g = 0; g < NGRAIN; g++)
                hi += ui.grain[g].life == 3u;
        }
        check("visualiser: more grains at a higher Frequency", hi > 4u * lo && lo > 0u);
    }
    {   /* Grain Size sets the dots' size */
        int32_t sz = param_of_role(ROLE_SIZE);
        uint32_t g, r0 = 0, r1 = 0;
        ctl.val[sz] = 0;
        for (g = 0; g < 30u; g++) vis_step();
        for (g = 0; g < NGRAIN; g++) r0 = ui.grain[g].life && ui.grain[g].r > r0 ? ui.grain[g].r : r0;
        ctl.val[sz] = 127;
        for (g = 0; g < 30u; g++) vis_step();
        for (g = 0; g < NGRAIN; g++) r1 = ui.grain[g].life && ui.grain[g].r > r1 ? ui.grain[g].r : r1;
        check("visualiser: Grain Size sets the dots' size", r0 == 1u && r1 == 7u);
    }
    {   /* the widest everything: long values, channel 16, OCT -3, a message, spray at max, position at the ends */
        uint32_t p, k, lint = 0;
        ctl_set_channel(15);
        ctl_set_octave(-3);
        for (i = 0; i < NPARAMS; i++)
            ctl.val[i] = PARAMS[i].max;
        for (p = 0; p < NPAGES; p++) {
            ctl_set_page((int32_t)p);
            host_clipped = 0;
            app_redraw = 1;
            tick();
            frames(VIS_FRAMES);
            for (k = 0; k < 4u; k++) {                  /* each slot touched: highlight, zoom */
                host_turn(EN_K1 + k, -1);
                host_turn(EN_K1 + k, 1);
                ctl.touched = (int8_t)k;
                ctl.touch_ms = fm1_ms;
                tick();
            }
            lint += host_clipped;
            if (host_clipped)
                printf("  (page %u: %u, the last: %s)\n", p + 1u, host_clipped, host_clip_what);
        }
        host_png("build/shots/widest_zoom.png");
        fm1_ms += 5000u;
        ui_msg("ALGORITHM SOON");
        saved = -1;
        saved_ms = fm1_ms;
        host_clipped = 0;
        tick();
        lint += host_clipped;
        host_png("build/shots/widest.png");
        for (i = 0; i < NPARAMS; i++)
            ctl.val[i] = PARAMS[i].min;
        host_clipped = 0;
        app_redraw = 1;
        frames(VIS_FRAMES * 4u);
        lint += host_clipped;
        check("layout: nothing cut or outside its canvas, every page, widest values", lint == 0u);
        saved = 0;
    }
    ctl_set_page(1);
    app_redraw = 1;
    frames(VIS_FRAMES);
    host_png("build/shots/page2.png");
    memset(host_fb, 0, sizeof host_fb);
    app_redraw = 1;
    tick();
    check("app_redraw: the whole screen drawn again", ink_rows(0, 240) > 150u && !app_redraw);
    {   /* every palette: the screen draws without lint problems */
        uint32_t pal, lint = 0;
        for (pal = 0; pal < NPALETTES; pal++) {
            palette_set(pal);
            host_clipped = 0;
            app_redraw = 1;
            tick();
            lint += host_clipped;
        }
        palette_set(ui_palette());
        check("every palette: drawn, no layout problems", lint == 0u);
    }
    {
        const char *c = APP_NAME;
        int all = 1;
        for (; *c; c++)
            all &= glyph_at(&AF_L, (uint8_t)*c) >= 0;
        check("every character of APP_NAME is in the large font", all);
    }
}

/* ------------------------------------------------------------ flash --- */
static void power_cycle(void)                           /* the app's RAM gone, the flash kept */
{
    memset(&ctl, 0xA5, sizeof ctl);
    memset(&keys, 0, sizeof keys);
    ccq_r = ccq_w = inq_r = inq_w = 0;
    saved = 0;
    host_boot();
    tick();
    drain();
}

static void persistence(void)
{
    int32_t pos = param_at(0, 1), spray = param_of_role(ROLE_SPRAY);
    host_nor_erase();
    reset_state();
    power_cycle();
    check("fresh unit: the table's defaults, page 1", ctl.val[pos] == 64u && ctl.page == 0u && ctl.channel == 0u);

    host_turn(EN_K2, 1);
    press(B_SCL);
    host_turn(EN_PRESET, 2);
    press(B_OCTUP);
    tick();
    host_erases = 0;
    power_cycle();
    check("changes without SAVE and before the autosave: lost", ctl.val[pos] == 64u && host_erases == 0u);

    host_turn(EN_K2, 1);                                /* 65 */
    press(B_SCL);
    host_turn(EN_K1, 1);                                /* Spray 21 */
    host_turn(EN_PRESET, 2);
    press(B_OCTUP);
    press(B_SAVE);
    check("SAVE: stored, SAVED shown", saved == 1 && host_erases > 0u);
    host_png("build/shots/saved.png");
    power_cycle();
    check("power-on: values, page, channel and octave back",
          ctl.val[pos] == 65u && ctl.val[spray] == 21u && ctl.page == 1u && ctl.channel == 2u && ctl.octave == 1);

    host_erases = 0;
    host_turn(EN_K1, 3);
    tick();
    fm1_ms += AUTOSAVE_MS - 100u;
    tick();
    check("autosave: not before AUTOSAVE_MS", host_erases == 0u);
    fm1_ms += 200u;
    tick();
    check("autosave: after AUTOSAVE_MS of quiet", host_erases > 0u && !ctl.changed);
    {
        uint8_t v = ctl.val[spray];
        power_cycle();
        check("autosaved value back after power-on", ctl.val[spray] == v);
    }
    host_erases = 0;
    fm1_ms += 3u * AUTOSAVE_MS;
    tick();
    check("nothing changed: no more saves (flash wear)", host_erases == 0u);

    {   /* the unit ran another firmware since: its record in the same slot reads as empty */
        uint8_t other[64];
        memset(other, 99, sizeof other);
        host_nor_erase();
        store_save(0, 0x12345678u, other, sizeof other);
        power_cycle();
        check("another app's record: the table's defaults", ctl.val[pos] == 64u && ctl.page == 0u);
    }
    {   /* saved with another table: page, channel, octave kept; values not (they belong to other parameters) */
        rec_t r;
        memset(&r, 0, sizeof r);
        r.table = table_hash() ^ 1u;
        r.page = 1;
        r.channel = 5;
        r.octave = 2;
        r.nvals = (uint8_t)NPARAMS;
        memset(r.val, 7, sizeof r.val);
        store_save(0, APP_ID, &r, sizeof r);
        power_cycle();
        check("a changed table: values reset, page / channel / octave kept",
              ctl.val[pos] == 64u && ctl.page == 1u && ctl.channel == 5u && ctl.octave == 2);
    }
    {   /* damaged values (a bug, an older version): clamped */
        rec_t r;
        memset(&r, 0, sizeof r);
        r.table = table_hash();
        r.page = 200;
        r.channel = 0xF3;
        r.octave = -100;
        r.nvals = (uint8_t)NPARAMS;
        memset(r.val, 200, sizeof r.val);
        store_save(0, APP_ID, &r, sizeof r);
        power_cycle();
        check("damaged record: page, channel, octave and values in range",
              ctl.page == 0u && ctl.channel == 3u && ctl.octave == -3 && ctl.val[pos] == 127u);
    }
    {   /* a record cut short (an older version with fewer fields) */
        rec_t r;
        memset(&r, 0, sizeof r);
        r.table = table_hash();
        r.page = 1;
        store_save(0, APP_ID, &r, 6);
        power_cycle();
        check("a short record: what it has, the rest defaults", ctl.page == 1u && ctl.val[pos] == 64u);
    }
    flash_ok = 0;
    press(B_SAVE);
    check("no flash: SAVE fails visibly", saved == -1);
    flash_ok = 1;
    host_erases = 0;
    press(B_SAVE);
    check("flash back: SAVE works", saved == 1 && host_erases > 0u);
    host_nor_erase();
    reset_state();
}

/* ------------------------------------------------------------ sound --- */
static void sound(void)
{
    uint32_t i, loud = 0;
    reset_state();
    fm1_in.notes = 0x7FFFFFFu;
    host_midi(0x90, 60, 127);
    host_render(buf, 1024);
    for (i = 0; i < 2048u; i++)
        loud |= (uint32_t)buf[i];
    check("no sound: silence with every key held and MIDI in", loud == 0u);
    fm1_in.notes = 0;
    drain();
}

int main(void)
{
    if (system("mkdir -p build/shots"))
        return 1;
    host_boot();
    table();
    cc_out();
    cc_in();
    key_notes();
    controls_test();
    screen();
    persistence();
    sound();
    puts(failed ? "APP TEST FAILED" : "app test passed");
    return failed ? 1 : 0;
}
