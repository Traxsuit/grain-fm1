/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* The app contract: what the platform (firmware/src/) calls, and when. Everything the firmware does beyond booting,
 * USB-MIDI, updates and the flash driver lives behind these five functions in firmware/app/; replace app.c (and add
 * files it includes) to make a different instrument. Unity build: firmware/src/firmware.c includes the platform,
 * then app.c, then main.c, so the app sees every platform helper and main.c sees the app.
 *
 * Contexts (the rules that keep the FM-1 healthy):
 *   main loop    app_init, app_frame, app_fault_name. May draw, may touch the flash (storage_hw.c st_*), may
 *                block briefly; must return to the platform every frame (the watchdog is fed there, the update
 *                and UBOOT requests are served there).
 *   audio ISR    app_audio. Every 2.9 ms, HALF_FRAMES / CTL calls in a row. No drawing, no flash, no waiting;
 *                O(1) work per block; rare paths in __attribute__((noinline)) functions. Its cost is measured
 *                (tests/target_budget.py).
 *   between them the main loop never edits state the ISR is using mid-block: it sets a volatile request field
 *                the ISR takes at the start of a block, and the ISR publishes results the same way.
 *
 * Reserved by the platform (do not reuse them for app gestures):
 *   OCT- + OCT+ held 5 s   UBOOT (the chip's boot mode, for recovery) with a countdown on the screen from 2 s
 *   SysEx F0 22 24 35 ...  the update protocol (ota.c) and the UBOOT soft key
 *   the first 30 s         the boot guard counts a crash or hang in them as a failed boot (two: UBOOT)
 */
static void app_init(void);                     /* boot: display on, input running, before the audio starts */
static void app_frame(void);                    /* main loop, ~60 times a second (every >= 15 ms): input, LEDs, screen */
static void app_audio(int32_t *out, uint32_t n);  /* audio ISR: n (CTL) stereo frames, out[2i] left, out[2i+1] right,
                                                 * Q15 (+-32767 is full scale, -6 dBFS at the codec) */
static void app_audio_stop(void);               /* main loop: before an update or UBOOT, silence and forget notes */
static const char *app_fault_name(void);        /* the crash screen's title (main.c fm1_fault) */

/* set by the platform: 1 at boot and after anything it drew over the app's screen (the UBOOT countdown, the update
 * screen); the app draws its whole screen again on its next frame and clears it */
static uint8_t app_redraw = 1;
