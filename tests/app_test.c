/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* Host test of the app (firmware/app/) on the host platform (tests/host_platform.h): the start app's screen (drawn,
 * lazy, redrawn after the platform drew over it, the layout lint), its LEDs, its record in flash (SAVE, power-on,
 * another app's data, values out of range) and its sound (pitch of a key and of a MIDI note, note off, a lost MIDI
 * stream, app_audio_stop, no clicks). Screens in build/shots/. A template: when the app changes,
 * change these checks with it, and prove each new one can fail (break the code once, watch it go red).
 *   cc -O1 -Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests -o build/host/app_test tests/app_test.c -lm
 *   build/host/app_test */
#include "host_platform.h"
#include <math.h>

static int failed;
static void check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    failed += !ok;
}

/* ---------------------------------------------------------------- screen --- */
static uint32_t ink_rows(uint32_t y0, uint32_t y1)   /* rows in y0..y1 with anything but the background */
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

static void screen(void)
{
    uint16_t key0;
    host_boot();
    host_frame();
    check("boot: the name and version drawn", ink_rows(40, 100) > 20u);
    {   /* the large face is sparse (tools/gen_aa_font.py L_CHARS): a character it lacks draws as a space */
        const char *c = APP_NAME;
        int all = 1;
        for (; *c; c++)
            all &= glyph_at(&AF_L, (uint8_t)*c) >= 0;
        check("boot: every character of APP_NAME is in the large font", all);
    }
    check("boot: the keys row drawn", ink_rows(120, 148) > 20u);
    host_png("build/shots/home.png");
    key0 = host_fb[134u * 240u + 16u];
    host_frame();
    check("lazy: nothing changed, nothing sent to the panel", host_blits == 0u);

    fm1_in.notes = 1u;                                  /* F3 held */
    host_press(B_PLAY, 1);
    host_frame();
    check("a key held: drawn in the accent", host_fb[134u * 240u + 16u] != key0 && host_fb[134u * 240u + 16u] == T_ACCENT);
    check("a key held: its LED lit (id 14), the others dark", host_led[14] && !host_led[15]);
    check("a button held: its LED lit", host_led[panel.btn[B_PLAY]]);
    check("a button held: its name drawn", ink_rows(160, 180) > 8u);
    host_png("build/shots/held.png");
    fm1_in.notes = 0;
    host_press(B_PLAY, 0);
    host_frame();
    check("released: the LEDs dark again", !host_led[14] && !host_led[panel.btn[B_PLAY]]);

    host_turn(EN_K1, 3);
    host_frame();
    check("a knob turned: the screen follows", host_blits > 0u && knob[0] == 3);
    host_turn(EN_K4, -2);
    host_frame();
    check("KNOB 4 counter-clockwise: negative", knob[3] == -2);

    memset(host_fb, 0, sizeof host_fb);                 /* the platform drew over the screen (UBOOT countdown) */
    app_redraw = 1;
    host_frame();
    check("app_redraw: the whole screen drawn again", ink_rows(40, 100) > 20u && !app_redraw);

    fm1_in.notes = (1u << 27) - 1u;                     /* everything at its widest */
    fm1_in.buttons = (1u << 14) - 1u;
    host_turn(EN_K1, -2000);                            /* (clamped at -999) */
    host_turn(EN_K2, -2000);
    host_turn(EN_K3, -2000);
    host_turn(EN_K4, -2000);
    tone.last_midi = 127;
    host_frame();
    host_png("build/shots/widest.png");
    check("layout: nothing cut or outside its canvas", host_clipped == 0u);
    if (host_clipped)
        printf("  (%u, the last: %s)\n", host_clipped, host_clip_what);
    check("knobs clamped at -999", knob[0] == -999 && knob[3] == -999);
    fm1_in.notes = fm1_in.buttons = 0;
    tone.last_midi = 0;
}

/* ---------------------------------------------------------------- flash --- */
static void power_cycle(void)                           /* the app's RAM gone, the flash kept */
{
    memset(knob, 0, sizeof knob);
    saved = 0;
    host_boot();
    host_frame();
}

static void press_save(void)
{
    host_press(B_SAVE, 1);
    host_frame();
    host_press(B_SAVE, 0);
    host_frame();
}

static void persistence(void)
{
    host_nor_erase();                                   /* a fresh unit (the widest screen above held SAVE too) */
    host_turn(EN_K1, 5 - knob[0]);
    host_turn(EN_K2, -7 - knob[1]);
    host_frame();
    power_cycle();
    check("not saved: power-on starts at zero", knob[0] == 0 && knob[1] == 0);
    host_turn(EN_K1, 5);
    host_turn(EN_K2, -7);
    host_frame();
    press_save();
    check("SAVE: stored, SAVED shown", saved == 1 && host_erases > 0u);
    host_png("build/shots/saved.png");
    power_cycle();
    check("power-on: the saved values back", knob[0] == 5 && knob[1] == -7 && knob[2] == 0);
    host_turn(EN_K1, 1);
    host_frame();
    check("a knob turned after SAVE: SAVED gone", saved == 0);
    {   /* the unit ran another firmware since: its record in the same slot must read as empty */
        int32_t other[4] = {111, 222, 333, 444};
        host_nor_erase();
        store_save(0, 0x12345678u, other, sizeof other);
        power_cycle();
        check("another app's record: ignored (zeros)", knob[0] == 0 && knob[3] == 0);
    }
    {   /* a record of ours with values out of range (an older version, a bug): clamped */
        int32_t wild[4] = {5000, -5000, 1, 2};
        store_save(0, APP_ID, wild, sizeof wild);
        power_cycle();
        check("stored values out of range: clamped", knob[0] == 999 && knob[1] == -999 && knob[3] == 2);
    }
    flash_ok = 0;
    press_save();
    check("no flash: SAVE fails visibly", saved == -1);
    flash_ok = 1;
    memset(knob, 0, sizeof knob);
    saved = 0;
    host_frame();
}

/* ---------------------------------------------------------------- sound --- */
#define SECS 1u
static int32_t buf[2u * FS * SECS];

static double pitch(const int32_t *b, uint32_t frames)   /* rising zero crossings per second, left channel */
{
    uint32_t i, n = 0, first = 0, last = 0;
    for (i = 1; i < frames; i++)
        if (b[2u * (i - 1u)] < 0 && b[2u * i] >= 0) {
            if (!n)
                first = i;
            last = i;
            n++;
        }
    return n > 1u ? (double)(n - 1u) * FS / (double)(last - first) : 0.0;
}

static int32_t peak(const int32_t *b, uint32_t frames)
{
    int32_t m = 0;
    uint32_t i;
    for (i = 0; i < 2u * frames; i++)
        m = abs(b[i]) > m ? abs(b[i]) : m;
    return m;
}

static int32_t max_step(const int32_t *b, uint32_t frames)   /* the largest jump between two samples: a click */
{
    int32_t m = 0;
    uint32_t i;
    for (i = 1; i < frames; i++)
        m = abs(b[2u * i] - b[2u * (i - 1u)]) > m ? abs(b[2u * i] - b[2u * (i - 1u)]) : m;
    return m;
}

static int same_lr(const int32_t *b, uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; i++)
        if (b[2u * i] != b[2u * i + 1u])
            return 0;
    return 1;
}

static void sound(void)
{
    double f;
    uint32_t pkt;
    host_render(buf, 1024);
    check("silence with nothing held", peak(buf, 1024) == 0);

    fm1_in.notes = 1u;                                  /* key 0: F3, 174.61 Hz */
    host_render(buf, FS / CTL * CTL);
    f = pitch(buf, FS / CTL * CTL);
    check("key F3: 174.6 Hz (+-0.5)", fabs(f - 174.61) < 0.5);
    check("key F3: peak below the AMP ceiling, above -20 dB", peak(buf, FS / CTL * CTL) <= AMP && peak(buf, FS / CTL * CTL) > 3000);
    check("key F3: the attack ramps (no click)", max_step(buf, 2048) < 600);
    check("mono: left = right", same_lr(buf, FS / CTL * CTL));
    fm1_in.notes = 1u | 1u << 12;                       /* F3 and F4 held: the lowest plays */
    host_render(buf, 8192);
    check("two keys: the lowest plays", fabs(pitch(buf + 2u * 4096u, 4096) - 174.61) < 2.0);
    fm1_in.notes = 0;
    host_render(buf, 1024);
    check("released: silent within 1024 frames, no click", peak(buf + 2u * 512u, 512) == 0 && max_step(buf, 1024) < 600);

    host_midi(0x90, 69, 100);                           /* A4 over USB-MIDI */
    host_render(buf, FS / CTL * CTL);
    f = pitch(buf, FS / CTL * CTL);
    check("MIDI A4: 440 Hz (+-1)", fabs(f - 440.0) < 1.0);
    check("MIDI A4: shown as the last note", tone.last_midi == 69u);
    host_midi(0x80, 60, 0);                             /* another note's off */
    host_render(buf, 1024);
    check("note-off of another note: A4 goes on", peak(buf + 2u * 512u, 512) > 3000);
    host_midi(0x90, 69, 0);                             /* note-on velocity 0 = off */
    host_render(buf, 1024);
    check("note-on velocity 0: off", peak(buf + 2u * 512u, 512) == 0);

    host_midi(0x91, 57, 100);                           /* any channel */
    host_render(buf, 1024);
    check("channel 2 plays too", peak(buf + 2u * 512u, 512) > 3000);
    midi_in_overflow = 1;                               /* a broken stream: events lost */
    host_render(buf, 1024);
    check("MIDI ring overflow: every note released", peak(buf + 2u * 512u, 512) == 0 && !midi_in_overflow);

    fm1_in.notes = 1u;
    host_render(buf, 1024);
    fm1_in.notes = 0;
    host_midi(0x90, 64, 100);
    app_audio_stop();
    host_render(buf, 1024);
    check("app_audio_stop: silent at once, the MIDI note forgotten", peak(buf, 1024) == 0);
    while (host_midi_out(&pkt))
        ;
    midi_out(0x90, 60, 100);
    check("midi_out: a USB-MIDI note-on packet", host_midi_out(&pkt) && pkt == (0x09u | 0x90u << 8 | 60u << 16 | 100u << 24));
}

int main(void)
{
    if (system("mkdir -p build/shots"))
        return 1;
    screen();
    persistence();
    sound();
    puts(failed ? "APP TEST FAILED" : "app test passed");
    return failed ? 1 : 0;
}
