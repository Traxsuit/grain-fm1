/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors
 * The design (the device clock driven by the audio the worklet asks for, the screen and LEDs read back between
 * render quanta) follows X0X's browser build, web/emu/x0x_web.c of charlesvestal/fm1-x0x (GPL-3.0), a Felucca fork,
 * as M-Vave-OP1's web/emu did. */
/* The firmware's app in the browser: firmware/app/ on the host platform (tests/host_platform.h: the real gfx.c,
 * panel.c, MIDI rings and midi.c; a framebuffer, the panel's state and LEDs instead of the hardware), compiled to
 * WebAssembly and run in an AudioWorklet (web/emu/worklet.js). The audio the worklet asks for drives the clock: every
 * 128-frame quantum renders through app_audio, and every 16 ms of it the main loop runs one app_frame.
 * Not here: the boot path, the flash, USB enumeration, updates, UBOOT, the crash screen (only real hardware and
 * the host tests see those).
 *   web/emu/build.sh  ->  build/emu/{emu.wasm, worklet.js, index.html} */
#include "host_platform.h"

#define WEB_MAX 1024u
static int32_t web_buf[2u * WEB_MAX];
static float web_l[WEB_MAX], web_r[WEB_MAX];
static uint32_t web_frames;              /* frames rendered since the last main-loop pass */
static uint32_t web_draws;               /* transfers to the panel so far: the page redraws when it changes */

#define EXPORT __attribute__((used, visibility("default")))

EXPORT void web_boot(void)
{
    host_boot();
    web_draws++;
}

EXPORT uint32_t web_sample_rate(void) { return FS; }

EXPORT void web_render(uint32_t n)        /* n frames (a multiple of CTL, <= WEB_MAX) into web_out_l / _r */
{
    uint32_t i;
    if (n > WEB_MAX)
        n = WEB_MAX;
    host_render(web_buf, n);
    for (i = 0; i < n; i++) {
        web_l[i] = (float)web_buf[2u * i] / 65536.0f;   /* Q15 at -6 dBFS (audio.c OUT_SHIFT): full scale is 2^16 */
        web_r[i] = (float)web_buf[2u * i + 1u] / 65536.0f;
    }
    web_frames += n;
    while (web_frames >= FS * 16u / 1000u) {    /* the main loop, every 16 ms of audio */
        web_frames -= FS * 16u / 1000u;
        host_frame();
        web_draws += host_blits;
    }
}

EXPORT float *web_out_l(void) { return web_l; }
EXPORT float *web_out_r(void) { return web_r; }
EXPORT uint16_t *web_screen(void) { return host_fb; }
EXPORT uint32_t web_screen_draws(void) { return web_draws; }
EXPORT uint8_t *web_leds(void) { return host_led; }        /* 41 key ids: 0..13 matrix buttons, 14..40 note keys */
EXPORT uint32_t web_button_id(uint32_t label) { return label < NB ? panel.btn[label] : 255u; }
EXPORT void web_keys(uint32_t mask) { fm1_in.notes = mask & ((1u << 27) - 1u); }
EXPORT void web_buttons(uint32_t labels)      /* bit b = label b (panel.c B_*) held */
{
    uint32_t b, m = 0;
    for (b = 0; b < NB; b++)
        if ((labels >> b) & 1u)
            m |= panel_mask(b);
    fm1_in.buttons = m;
}
EXPORT void web_enc(uint32_t role, int32_t steps) { if (role < NE) host_turn(role, steps); }
EXPORT void web_midi(uint32_t status, uint32_t d1, uint32_t d2) { host_midi(status, d1, d2); }
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }
