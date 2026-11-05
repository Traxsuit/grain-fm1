/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Stephen Webster */
/* The screen, 240 x 240, one view in four areas, each drawn lazily into its own canvas strip (a signature of what it
 * shows; drawn again only when that changes, everything when the platform sets app_redraw):
 *   header      0..28    the page's name in its colour, the page number; CH / OCT, or a short message (SOON, CH 5,
 *                        SAVED), and a dot while MIDI arrives
 *   slots      28..168   four slots 120 x 70 (KNOB 1 2 / 3 4): name, value, an arc of the value within min..max in
 *                        the page colour. The knob turned last is highlighted and the other three dimmed for DIM_MS
 *   zoom       28..168   (over the slots) while a knob turns and ZOOM_MS after: its name, the value in the large font,
 *                        a bar of the range and a graph of its last values; then FADE_STEPS steps back to the slots
 *   visualiser 168..240  a waveform strip; a playhead at Position, a band as wide as Spray around it, grains (dots)
 *                        appearing at a rate set by Frequency, sized by Grain Size. From the local values (roles in
 *                        params.c), whatever page is shown; redrawn every VIS_FRAMES frames (~20 a second)
 * A full redraw of the visualiser strip is ~17 k pixels (~23 ms of SPI DMA, the CPU free meanwhile); the slots and the
 * header only when they change. */

#define HDR_H 28u
#define GRID_Y HDR_H
#define SLOT_W 120u
#define SLOT_H 70u
#define VIS_Y (GRID_Y + 2u * SLOT_H)              /* 168 */
#define VIS_H (240u - VIS_Y)                      /* 72 */
#define ZOOM_Y (GRID_Y + 8u)                      /* the zoom card: 240 x 124 (CV_MAX) inside the slots' area */
#define ZOOM_H 124u
#define DIM_MS 3000u                              /* from the last turn: ~1.5 s of the dimmed slots after the zoom */
#define ZOOM_MS 1200u
#define FADE_MS 90u                               /* per fade step */
#define FADE_STEPS 3u
#define MSG_MS 1000u
#define RX_MS 150u
#define VIS_FRAMES 3u
#define HIST 48u                                  /* the zoom graph's values (one every other frame: ~1.5 s) */
#define NGRAIN 16u

static struct {
    uint32_t hdr_sig, slot_sig[4], zoom_sig, vis_sig;
    uint8_t zoomed;                               /* the zoom shows (its area is not the slots') */
    char msg[16];
    uint32_t msg_ms;
    uint32_t rx_seen, rx_ms;
    int32_t hist_param;
    uint8_t hist[HIST];
    uint32_t hist_n, frame;
    uint32_t vis_tick, rng, spawn_acc;
    struct { uint8_t x, y, life, r; } grain[NGRAIN];
    uint8_t wave[240];                            /* the strip's waveform: half-height per column, 0..30 */
} ui;

static const uint16_t QSIN[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005,
    7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406,
    12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
    15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};

static int32_t isin(uint32_t a)                   /* a: 256 per turn -> Q14 */
{
    uint32_t q = (a >> 6) & 3u, i = a & 63u;
    int32_t v = q & 1u ? QSIN[64u - i] : QSIN[i];
    return q & 2u ? -v : v;
}
static int32_t icos(uint32_t a) { return isin(a + 64u); }

static uint32_t ui_rand(void)
{
    ui.rng = ui.rng * 1664525u + 1013904223u;
    return ui.rng >> 16;
}

static uint32_t fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static void ui_msg(const char *s)
{
    str_cpy(ui.msg, s, sizeof ui.msg);
    ui.msg_ms = fm1_ms;
}

static char *put_num(char *b, int32_t v, int sign)    /* decimal, '+' if sign and v > 0; returns the end */
{
    char t[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        *b++ = '-';
    else if (sign && v > 0)
        *b++ = '+';
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < sizeof t);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}

/* a value as the table's format says: digits in num, the unit ("%" or "") in unit */
static void fmt_value(uint32_t i, char *num, const char **unit)
{
    const param_t *p = &PARAMS[i];
    int32_t v = ctl.val[i];
    *unit = "";
    if (p->fmt == FMT_PCT) {
        v = p->max > p->min ? (v - p->min) * 100 / (p->max - p->min) : 0;
        *unit = "%";
        put_num(num, v, 0);
    } else if (p->fmt == FMT_BIP)
        put_num(num, v - 64, 1);
    else
        put_num(num, v, 0);
}

static uint32_t frac_q8(uint32_t i)               /* the value within min..max, 0..256 */
{
    const param_t *p = &PARAMS[i];
    return p->max > p->min ? (uint32_t)(ctl.val[i] - p->min) * 256u / (uint32_t)(p->max - p->min) : 0u;
}

static uint16_t page_col(void) { return PAGES[ctl.page].colour; }
static uint16_t dimmed(uint16_t c) { return ux_mix(T_BG, c, 40); }

static int ui_dimming(void) { return ctl.touched >= 0 && fm1_ms - ctl.touch_ms < DIM_MS; }

/* ------------------------------------------------------------ header --- */
static int msg_on(void) { return ui.msg[0] && fm1_ms - ui.msg_ms < MSG_MS; }
static int rx_on(void) { return fm1_ms - ui.rx_ms < RX_MS; }

static uint32_t hdr_sig(void)
{
    uint32_t h = fnv(2166136261u, ctl.page);
    h = fnv(h, ctl.channel);
    h = fnv(h, (uint32_t)(int32_t)ctl.octave);
    h = fnv(h, (uint32_t)saved);
    h = fnv(h, (uint32_t)msg_on());               /* on / off itself: a message stamped at 0 ms must still go */
    h = fnv(h, msg_on() ? ui.msg_ms : 0u);
    h = fnv(h, (uint32_t)rx_on());
    return fnv(h, usb.config);
}

static void draw_header(void)
{
    char b[24], *e;
    int32_t x;
    cv_begin(240, HDR_H, T_BG);
    x = cv_text(8, CAP_IN(M, HDR_H), &AF_M, PAGES[ctl.page].name, page_col());
    e = put_num(b, ctl.page + 1, 0);
    *e++ = '/';
    put_num(e, (int32_t)NPAGES, 0);
    cv_text(x + 6, CAP_IN(S, HDR_H), &AF_S, b, T_MID);
    if (saved || msg_on()) {
        const char *s = saved > 0 ? "SAVED" : saved < 0 ? "SAVE FAILED" : ui.msg;
        cv_text_r(226, CAP_IN(S, HDR_H), &AF_S, s, saved < 0 ? T_REC : T_TEXT, T_BG);
    } else {
        str_cpy(b, "CH ", sizeof b);
        e = put_num(b + 3, ctl.channel + 1, 0);
        str_cpy(e, "  OCT ", 8);
        put_num(e + 6, ctl.octave, 1);
        cv_text_r(226, CAP_IN(S, HDR_H), &AF_S, b, usb.config ? T_MID : T_DIM, T_BG);
    }
    if (rx_on())
        cv_rrect(230, HDR_H / 2u - 3, 6, 6, 3, T_ACCENT, T_BG);
    cv_rect(0, HDR_H - 1, 240, 1, T_LINE);
    cv_blit(0, 0);
}

/* ------------------------------------------------------------ slots --- */
#define ARC_CX 28
#define ARC_CY 37
#define ARC_R 17
#define ARC_STEPS 72u

/* 270 degrees from the lower left (a = 160 of 256, 225 deg) clockwise to the lower right; q8 of it drawn in col */
static void draw_arc(int32_t cx, int32_t cy, int32_t r, uint32_t q8, uint16_t col, uint16_t track, int32_t th)
{
    uint32_t s, lit = q8 * ARC_STEPS / 256u;
    for (s = 0; s <= ARC_STEPS; s++) {
        uint32_t a = 160u - s * 192u / ARC_STEPS;       /* 192 of 256: 270 degrees */
        int32_t x = cx + (icos(a & 255u) * r >> 14), y = cy - (isin(a & 255u) * r >> 14);
        cv_rect(x - th / 2, y - th / 2, th, th, s <= lit && q8 ? col : track);
    }
}

static uint32_t slot_sig(uint32_t s)
{
    int32_t i = param_at(ctl.page, s);
    uint32_t h = fnv(2166136261u, ctl.page);
    h = fnv(h, (uint32_t)i);
    h = fnv(h, i >= 0 ? ctl.val[i] : 0u);
    h = fnv(h, (uint32_t)(ui_dimming() ? (ctl.touched == (int32_t)s ? 1 : 2) : 0));
    return fnv(h, ux.gen);
}

static void draw_slot(uint32_t s)
{
    int32_t i = param_at(ctl.page, s);
    int hot = ui_dimming() && ctl.touched == (int32_t)s, dim = ui_dimming() && !hot;
    uint16_t bg = hot ? T_SURF : T_BG, col = dim ? dimmed(page_col()) : page_col();
    uint16_t text = dim ? T_DIM : T_TEXT, mid = dim ? T_DIM : T_MID;
    char num[12], name[24];
    const char *unit;
    int32_t x;
    cv_begin(SLOT_W, SLOT_H, T_BG);
    cv_rrect(3, 3, SLOT_W - 6, SLOT_H - 6, 8, bg, T_BG);
    if (hot)
        cv_rect(10, SLOT_H - 6, SLOT_W - 20, 2, col);
    if (i < 0) {
        draw_arc(ARC_CX, ARC_CY, ARC_R, 0, col, T_LINE, 3);
        cv_text(53, 12, &AF_S, "KNOB", T_DIM);
        put_num(num, (int32_t)s + 1, 0);
        cv_text(53, 31, &AF_M, num, T_DIM);
    } else {
        draw_arc(ARC_CX, ARC_CY, ARC_R, frac_q8((uint32_t)i), col, dim ? T_LINE : ux_mix(T_BG, T_TEXT, 22), 4);
        text_fit(name, sizeof name, PARAMS[i].name, &AF_S, (int32_t)SLOT_W - 60);   /* ~10 characters */
        cv_text_on(53, 12, &AF_S, name, mid, bg);
        fmt_value((uint32_t)i, num, &unit);
        x = cv_text_on(53, 31, &AF_M, num, text, bg);
        if (unit[0])
            cv_text_on(x + 1, 34, &AF_S, unit, mid, bg);
    }
    cv_blit((s & 1u) * SLOT_W, GRID_Y + (s >> 1) * SLOT_H);
}

/* ------------------------------------------------------------ zoom --- */
static int32_t zoom_param(void) { return ctl.touched >= 0 ? param_at(ctl.page, (uint32_t)ctl.touched) : -1; }

/* 0: not shown; 1..FADE_STEPS+1: shown, FADE_STEPS+1 fully, lower ones fading back */
static uint32_t zoom_level(void)
{
    uint32_t t;
    if (zoom_param() < 0)
        return 0;
    t = fm1_ms - ctl.touch_ms;
    if (t < ZOOM_MS)
        return FADE_STEPS + 1u;
    t = (t - ZOOM_MS) / FADE_MS;
    return t < FADE_STEPS ? FADE_STEPS - t : 0u;
}

static void hist_frame(void)
{
    int32_t i = zoom_param();
    if (i != ui.hist_param) {                 /* another knob: its graph starts flat at its value */
        ui.hist_param = i;
        ui.hist_n = 0;
        if (i >= 0)
            memset(ui.hist, ctl.val[i], HIST);
    }
    if (i >= 0 && !(ui.frame & 1u))
        ui.hist[ui.hist_n++ % HIST] = ctl.val[i];
}

static uint32_t zoom_sig(uint32_t lvl)
{
    int32_t i = zoom_param();
    uint32_t h = fnv(fnv(2166136261u, (uint32_t)i), lvl);
    h = fnv(h, i >= 0 ? ctl.val[i] : 0u);
    return fnv(fnv(h, ui.hist_n), ctl.page);
}

static void draw_zoom(uint32_t lvl)
{
    int32_t i = zoom_param(), x, w, k;
    uint32_t pct = 100u * lvl / (FADE_STEPS + 1u);      /* the fade: every colour towards the background */
    uint16_t col = ux_mix(T_BG, page_col(), (int32_t)pct), text = ux_mix(T_BG, T_TEXT, (int32_t)pct);
    uint16_t mid = ux_mix(T_BG, T_MID, (int32_t)pct), surf = ux_mix(T_BG, T_SURF, (int32_t)pct);
    char num[12];
    const char *unit;
    const param_t *p = &PARAMS[i];
    cv_begin(240, ZOOM_H, T_BG);
    cv_rrect(6, 0, 228, ZOOM_H, 8, surf, T_BG);
    cv_text_in(6, 6, 228, &AF_M, p->name, mid, surf);
    fmt_value((uint32_t)i, num, &unit);
    w = text_w(&AF_L, num) + (unit[0] ? text_w(&AF_M, unit) + 2 : 0);
    x = cv_text_on(120 - w / 2, 26, &AF_L, num, text, surf);
    if (unit[0])
        cv_text_on(x + 2, 36, &AF_M, unit, mid, surf);
    /* the range bar */
    cv_rect(20, 66, 200, 4, ux_mix(surf, T_TEXT, 18));
    cv_rect(20, 66, (int32_t)(frac_q8((uint32_t)i) * 200u / 256u), 4, col);
    /* the graph of the last values, oldest left */
    for (k = 0; k < (int32_t)HIST; k++) {
        uint32_t v = ui.hist[(ui.hist_n + (uint32_t)k) % HIST];
        int32_t y = 116 - (int32_t)(v * 36u / 127u), gx = 24 + k * 4;
        cv_rect(gx, y, 3, 116 - y + 1, k == (int32_t)HIST - 1 ? col : ux_mix(surf, col, 55));
    }
    cv_blit(0, ZOOM_Y);
}

/* ------------------------------------------------------------ visualiser --- */
static void vis_init(void)
{
    uint32_t x;
    for (x = 0; x < 240u; x++) {              /* three partials and a slow swell: looks like a sample, costs nothing */
        int32_t a = isin((x * 7u) & 255u) / 2 + isin((x * 23u + 40u) & 255u) / 3 + isin((x * 61u + 9u) & 255u) / 5;
        int32_t env = 9000 + isin((x * 2u + 200u) & 255u) / 2;           /* 0.27 .. 0.82 of Q14 */
        a = (a < 0 ? -a : a) * env >> 14;
        ui.wave[x] = (uint8_t)(4 + (a * 26 >> 14));
    }
    ui.rng = 12345u;
}

static void vis_step(void)                    /* a tick: grains age, new ones by Frequency */
{
    uint32_t pos = role_val(ROLE_POSITION, 64), spray = role_val(ROLE_SPRAY, 0), freq = role_val(ROLE_FREQ, 64);
    uint32_t size = role_val(ROLE_SIZE, 64), g, n;
    int32_t px = (int32_t)(pos * 239u / 127u), half = (int32_t)(spray * 110u / 127u) + 2;
    for (g = 0; g < NGRAIN; g++)
        if (ui.grain[g].life)
            ui.grain[g].life--;
    ui.spawn_acc += 8u + freq * 3u;           /* ~0.1 .. 5 grains a tick (~2 .. 100 a second) */
    for (n = ui.spawn_acc >> 6, ui.spawn_acc &= 63u; n; n--)
        for (g = 0; g < NGRAIN; g++)
            if (!ui.grain[g].life) {
                int32_t r = (int32_t)(1u + size * 6u / 127u), y;
                int32_t x = px + (int32_t)(ui_rand() % (uint32_t)(2 * half + 1)) - half;
                x = x < r ? r : x > 239 - r ? 239 - r : x;           /* the whole dot inside the strip */
                y = 36 + (int32_t)(ui_rand() % (2u * ui.wave[x] + 1u)) - ui.wave[x];
                y = y < r + 1 ? r + 1 : y > (int32_t)VIS_H - 2 - r ? (int32_t)VIS_H - 2 - r : y;
                ui.grain[g].x = (uint8_t)x;
                ui.grain[g].y = (uint8_t)y;
                ui.grain[g].r = (uint8_t)r;
                ui.grain[g].life = 3;
                break;
            }
}

static uint32_t vis_sig(void)
{
    uint32_t h = fnv(2166136261u, ui.vis_tick);
    h = fnv(h, role_val(ROLE_POSITION, 64));
    h = fnv(h, role_val(ROLE_SPRAY, 0));
    h = fnv(h, role_val(ROLE_SIZE, 64));
    return fnv(fnv(h, ctl.page), ux.gen);
}

static void draw_vis(void)
{
    uint32_t pos = role_val(ROLE_POSITION, 64), spray = role_val(ROLE_SPRAY, 0), g;
    int32_t px = (int32_t)(pos * 239u / 127u), half = (int32_t)(spray * 110u / 127u) + 2, x;
    uint16_t col = page_col(), band = ux_mix(T_BG, col, 18), wav = ux_mix(T_BG, T_TEXT, 30);
    uint16_t wav_in = ux_mix(T_BG, col, 60);
    cv_begin(240, VIS_H, T_BG);
    cv_rect(px - half, 4, 2 * half + 1, VIS_H - 8, band);
    for (x = 0; x < 240; x++) {
        int32_t a = ui.wave[x];
        cv_rect(x, 36 - a, 1, 2 * a + 1, x >= px - half && x <= px + half ? wav_in : wav);
    }
    for (g = 0; g < NGRAIN; g++)
        if (ui.grain[g].life) {
            int32_t r = ui.grain[g].r, d = 2 * r + 1;
            cv_rrect(ui.grain[g].x - r, ui.grain[g].y - r, d, d, r > 8 ? 8 : r,
                     ui.grain[g].life == 3 ? T_TEXT : ux_mix(T_BG, T_TEXT, 30 + 20 * ui.grain[g].life), band);
        }
    cv_rect(px - 1, 2, 2, VIS_H - 4, T_ACCENT);
    cv_blit(0, VIS_Y);
}

/* ------------------------------------------------------------ the frame --- */
/* the look: HI-CON (black, white text), so the pages' own colours carry the colour; GREY, the platform's boot palette,
 * would turn every mixed colour grey */
static uint32_t ui_palette(void)
{
    uint32_t i, k;
    for (i = 0; i < NPALETTES; i++) {           /* (the firmware's libc has no strcmp) */
        const char *n = UI_PALETTES[i].name, *w = "HI-CON";
        for (k = 0; n[k] && n[k] == w[k]; k++)
            ;
        if (n[k] == w[k])
            return i;
    }
    return 0;
}

static void ui_init(void)
{
    palette_set(ui_palette());
    memset(&ui, 0, sizeof ui);
    ui.hist_param = -1;
    vis_init();
}

static void ui_frame(int full)
{
    uint32_t s, sig, lvl;
    ui.frame++;
    if (keys.rx_count != ui.rx_seen) {
        ui.rx_seen = keys.rx_count;
        ui.rx_ms = fm1_ms;
    }
    if (full) {
        lcd_fill(0, 0, 240, 240, T_BG);
        ui.hdr_sig = ~hdr_sig();
        for (s = 0; s < 4u; s++)
            ui.slot_sig[s] = ~slot_sig(s);
        ui.zoom_sig = ~0u;
        ui.vis_sig = ~vis_sig();
        ui.zoomed = 0;
    }
    sig = hdr_sig();
    if (sig != ui.hdr_sig) {
        ui.hdr_sig = sig;
        draw_header();
    }
    hist_frame();
    lvl = zoom_level();
    if (lvl) {
        if (!ui.zoomed) {                     /* the margins around the zoom card */
            ui.zoomed = 1;
            lcd_fill(0, GRID_Y, 240, ZOOM_Y - GRID_Y, T_BG);
            lcd_fill(0, ZOOM_Y + ZOOM_H, 240, VIS_Y - ZOOM_Y - ZOOM_H, T_BG);
            ui.zoom_sig = ~0u;
        }
        sig = zoom_sig(lvl);
        if (sig != ui.zoom_sig) {
            ui.zoom_sig = sig;
            draw_zoom(lvl);
        }
    } else {
        if (ui.zoomed) {                      /* back to the slots: all four again */
            ui.zoomed = 0;
            for (s = 0; s < 4u; s++)
                ui.slot_sig[s] = ~slot_sig(s);
        }
        for (s = 0; s < 4u; s++) {
            sig = slot_sig(s);
            if (sig != ui.slot_sig[s]) {
                ui.slot_sig[s] = sig;
                draw_slot(s);
            }
        }
    }
    if (ui.frame % VIS_FRAMES == 0u) {
        ui.vis_tick++;
        vis_step();
    }
    sig = vis_sig();
    if (sig != ui.vis_sig) {
        ui.vis_sig = sig;
        draw_vis();
    }
}
