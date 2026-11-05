/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* The platform on a computer: the app (firmware/app/) built against the real gfx.c, panel.c, usb.c's MIDI rings,
 * midi.c and store.c, with the hardware replaced by a 240 x 240 framebuffer, a key / button / knob state the test
 * sets, an LED array, a simulated NOR flash and a render call. An app that uses a HAL call missing here: add a stub
 * (plain state the test can set and read), as the ones below. The host tests (tests/app_test.c) and the browser emulator (web/emu/emu.c) both include it.
 *   cc -O1 -Ibuild/gen -Ifirmware/src -Ifirmware/app ... (build/gen: ./build.sh generates the fonts and palettes)
 * The host's own: host_fb (RGB565), host_led[41] (key ids: 0..13 buttons, 14..40 note keys), host_frame() (one main
 * loop pass: app_frame), host_render(out, frames) (stereo Q15 as the codec gets it, before the 24-bit shift),
 * host_midi(status, d1, d2) (a message on USB-MIDI IN), host_midi_out(&pkt) (what the app sent), nor[] (the flash
 * behind storage_hw.c's st_read / st_erase / st_prog and store.c). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libc.c defines memset / memcpy / memcmp for the target: renamed here, the host's stay */
#define memset kit_memset
#define memcpy kit_memcpy
#define memcmp kit_memcmp
#include "platform.h"
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp

/* ------------------------------------------------------------ input, LEDs --- */
#define FM1_NCOL 11u
#define FM1_NENC 7u
static volatile struct {
    uint32_t notes;              /* bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;            /* bit i = matrix button i (0..13); panel.c maps labels to these */
    int16_t enc_steps[FM1_NENC]; /* + = clockwise, matrix encoder ids */
} fm1_in;
static uint8_t host_led[41];
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    return s;
}
static void fm1_led_key(uint32_t id, int on)
{
    if (id < sizeof host_led)
        host_led[id] = (uint8_t)(on != 0);
}
/* the rest of the HAL's input and LED calls an app may use (hal/fm1_input.h, fm1_adc.h), as plain state */
static __attribute__((unused)) uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL],
    fm1_led_mid[FM1_NCOL];
static uint32_t host_dim_lo, host_edge_buttons, host_edge_notes;
static void fm1_led_dim_level(uint32_t lo) { host_dim_lo = lo; }
static uint32_t fm1_input_edges(uint32_t *released)  /* button presses (and releases) since the last call */
{
    uint32_t now = fm1_in.buttons, p = now & ~host_edge_buttons;
    if (released)
        *released = host_edge_buttons & ~now;
    host_edge_buttons = now;
    return p;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t now = fm1_in.notes, p = now & ~host_edge_notes;
    host_edge_notes = now;
    return p;
}
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t host_adc[16] = {[FM1_ADC_BATT] = 700, [FM1_ADC_MASTER] = 700};
static int32_t fm1_adc_read(uint32_t ch) { return host_adc[ch & 15u]; }

/* ------------------------------------------------------------ display --- */
static uint16_t host_fb[240 * 240];      /* RGB565, native byte order */
static uint32_t host_blits, host_pixels; /* drawing cost: transfers and pixels sent since the last host_frame */
static void lcd_sync(void) {}
static void lcd_wake_now(void) {}
static void lcd_init(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            host_fb[j * 240u + i] = c;
    host_blits++;
    host_pixels += w * h;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)   /* px byte-swapped */
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t p = px[j * w + i];
                host_fb[(y + j) * 240u + x + i] = (uint16_t)(p >> 8 | p << 8);
            }
    host_blits++;
    host_pixels += w * h;
}
/* the layout lint: a text or a cell (cv_rrect) that does not fit its canvas, or a text cut, counts in host_clipped
 * (the app's screens must keep it 0; host_clip_what names the last one) */
static uint32_t host_clipped;
static const char *host_clip_what;
static uint32_t cv_w, cv_h;
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) \
    ((x0) < 0 || (y0) < 0 || (uint32_t)(x1) > cv_w || (uint32_t)(y1) > cv_h || ((flags) & 2u) \
         ? (void)(host_clipped++, host_clip_what = (s)) : (void)0)
#define GFX_HOOK_CELL(x0, y0, x1, y1) \
    ((x0) < 0 || (y0) < 0 || (uint32_t)(x1) > cv_w || (uint32_t)(y1) > cv_h \
         ? (void)(host_clipped++, host_clip_what = "a cell") : (void)0)
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)
#define GFX_HOOK_BEGIN() ((void)0)
#define GFX_HOOK_PIXELS(n) ((void)0)
#include "gfx.c"
#include "panel.c"

/* ------------------------------------------------------------ USB-MIDI --- */
#define FELUCCA_OTA 0
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
#include "usb.c"
#include "midi.c"

/* ------------------------------------------------------------ flash --- */
/* a 1 MiB NOR (erase: 0xFF; program: bits only go 1 -> 0) behind storage_hw.c's three calls, in its store areas
 * only; host_nor_erase() is a fresh unit, a test keeps nor[] across host_boot() for a power cycle */
static uint8_t nor[0x100000];
static uint8_t flash_ok = 1;
static uint32_t host_erases;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off + n > sizeof nor)
        return -8;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if ((off & 0xFFFu) || off < 0x97000u || off >= 0xE0000u)
        return -8;
    memset(nor + off, 0xFF, 0x1000u);
    host_erases++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off < 0x97000u || off + n > 0xE0000u)
        return -8;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
static void host_nor_erase(void) { memset(nor, 0xFF, sizeof nor); }
#include "store.c"

/* ------------------------------------------------------------ audio --- */
static volatile uint32_t audio_cpu_q8;   /* (audio.c on the target; the host's renders cost nothing here) */
#include "app.h"
#include "app.c"

static void host_boot(void)              /* power-on: the app's RAM is not cleared here (a test that power-cycles
                                          * resets what it checks); the flash stays */
{
    static int first = 1;
    if (first) {
        first = 0;
        host_nor_erase();
    }
    palette_set(UI_GREY_INDEX);
    panel.magic = 0;
    panel_init();
    usb.config = 1;                      /* configured: MIDI out flows */
    app_init();
}

static void host_frame(void)
{
    host_blits = host_pixels = 0;
    fm1_ms += 16u;
    app_frame();
}

static void host_render(int32_t *out, uint32_t frames)   /* frames: a multiple of CTL */
{
    uint32_t b;
    for (b = 0; b < frames; b += CTL)
        app_audio(out + 2u * b, CTL);
}

static void host_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    midi_enqueue((status >> 4) | status << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24, 1u);
}

static int host_midi_out(uint32_t *pkt)
{
    if (mo_r == mo_w)
        return 0;
    *pkt = midi_out_q[mo_r++ % MQ];
    return 1;
}

static void host_press(uint32_t label, int down)   /* a button by its printed label (panel.c B_*) */
{
    if (down)
        fm1_in.buttons |= panel_mask(label);
    else
        fm1_in.buttons &= ~panel_mask(label);
}

static void host_turn(uint32_t role, int32_t steps)  /* a knob by its role (panel.c EN_*) */
{
    fm1_in.enc_steps[panel.enc[role]] += (int16_t)(steps * panel.dir[role]);
}

static int host_png(const char *path)               /* host_fb -> PNG via a PPM and tests/shot.py */
{
    char ppm[512], cmd[1200];
    FILE *f;
    uint32_t i;
    snprintf(ppm, sizeof ppm, "%s.ppm", path);
    f = fopen(ppm, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = host_fb[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u),
                          (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    snprintf(cmd, sizeof cmd, "python3 tests/shot.py '%s' '%s' && rm -f '%s'", ppm, path, ppm);
    return system(cmd);
}
