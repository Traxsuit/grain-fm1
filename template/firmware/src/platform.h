/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* What the platform files share and an app may use: the audio clock and block sizes, the millisecond clock, the
 * boot-loop guard, the ring publish barrier and the FPU mark. These were part of Felucca's core.h; the platform keeps
 * Felucca's names (FELUCCA_* build options, felucca_dbg) so code and notes from Felucca and M-Vave-OP1 still match. */
#include <stdint.h>

#define FS 44100                 /* the engines' sample rate (the I2S clock runs at ~44,117.6 Hz, audio.c DAC_TICKS) */
#define CTL 32                   /* app_audio's block: frames per call (a half buffer is HALF_FRAMES / CTL blocks) */
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz (a key waits 0..1 half, then plays 1 half later) */
_Static_assert(HALF_FRAMES % CTL == 0, "a half buffer is whole blocks");

#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */

/* The chip's FPU (JieLi's SDK builds WL82 with -mfprev1; this build does not, its code is integer only). A function
 * marked FM1_FPU computes its floats with it; every float helper it calls needs the mark too, else that helper is not
 * inlined and runs as soft float. Host and browser builds: plain float */
#ifdef __PI32V2__
#define FM1_FPU __attribute__((target("fprev1")))
#else
#define FM1_FPU
#endif

static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */

/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT. Never remove it: with one
 * firmware bank and no recovery button it is what lets a broken image still be replaced over USB */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
