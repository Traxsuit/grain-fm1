/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* I2S output (ALNK0 -> external codec) and the audio ISR: per half buffer, HALF_FRAMES / CTL calls of the app's
 * app_audio (Q15 stereo), then 24-bit words for the codec. The ISR measures itself (felucca_dbg, audio_cpu_q8) and
 * fades the output around flash erases (aud_mute, storage_hw.c). Everything app_audio does runs here: keep it O(1)
 * per block and check its cost (tests/target_budget.py). */
/* registers: hal/fm1_audio.h */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define DAC_TICKS 544u            /* TIMER4 ticks per I2S frame (24 MHz / 44,117.6 Hz) */
#define CPU_AVG (4096u / HALF_FRAMES)   /* halves the load meter averages: ~93 ms whatever the half */
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

/* diagnostics, kept across resets and UBOOT entry: read with `fm1t memr` */
#define DBG_MAGIC 0x44424731u                       /* "DBG1" */
struct felucca_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;           /* where the main loop is (breadcrumbs) */
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;   /* as found at boot */
} felucca_dbg __attribute__((section(".noinit")));
static volatile uint32_t audio_halves, audio_max_us;
static volatile uint32_t audio_cpu_q8;                 /* the render's share of a half, /256, averaged over ~93 ms */
static volatile uint32_t t5_nested_ticks;              /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
static uint32_t audio_cpu_rem;                         /* keep the fractional IIR step: no low-load bias */

static void audio_block(int32_t *out, uint32_t n)       /* the app's block, then Q15 -> 24 bit */
{
    uint32_t i;
    app_audio(out, n);
    for (i = 0; i < n; i++) {
        out[2u * i] *= 1 << OUT_SHIFT;
        out[2u * i + 1u] *= 1 << OUT_SHIFT;
    }
}

/* A flash erase stops everything for ~45 ms (storage_hw.c st_erase: XIP stalls, the interrupts are off); cut there,
 * the audio clicks twice. So the main loop asks for silence first (aud_mute 1, storage_hw.c audio_fade_out): the next
 * half buffer fades the DAC out (2.9 ms) and the ones after are silent; at the start of its next pass the main loop
 * asks for the sound again (aud_mute 0, main.c) and the next half fades it in. aud_muted is the ISR's own level, so a
 * fade only ever starts from where the output is (no race with the flag); aud_quiet counts the silent halves since
 * (two: both halves of the buffer silent, the erase may start) */
#define HALF_LOG2 7
_Static_assert((1u << HALF_LOG2) == HALF_FRAMES, "HALF_LOG2 matches HALF_FRAMES");
static volatile uint8_t aud_mute, aud_muted, aud_quiet, aud_live;
static __attribute__((noinline)) void aud_fade_half(int32_t *o)   /* audio ISR, a half of 24-bit stereo */
{
    uint32_t want = aud_mute, i;
    if (want && aud_muted) {
        for (i = 0; i < HALF_WORDS; i++)
            o[i] = 0;
        if (aud_quiet < 255u)
            aud_quiet++;
        return;
    }
    for (i = 0; i < HALF_FRAMES; i++) {                 /* (|o| < 2^23, g <= 2^7: fits) */
        int32_t g = want ? (int32_t)(HALF_FRAMES - i) : (int32_t)i;
        o[2u * i] = (o[2u * i] * g) >> HALF_LOG2;
        o[2u * i + 1u] = (o[2u * i + 1u] * g) >> HALF_LOG2;
    }
    aud_muted = (uint8_t)want;
    aud_quiet = 0;
}

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    felucca_dbg.in_audio = 1;                   /* first: TIMER5 nests from here on (main.c) */
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us;
        int32_t *o = &abuf[half * HALF_WORDS];
        for (b = 0; b < HALF_FRAMES; b += CTL)
            audio_block(o + 2u * b, CTL);
        if (aud_mute | aud_muted)
            aud_fade_half(o);                           /* a flash erase coming or just done */
        aud_live = 1;
        fm1_audio_ack_half();
        audio_halves++;
        us = (fm1_ticks() - t0 - t5_nested_ticks) / FM1_TICKS_PER_US;   /* the render alone */
        if (us > audio_max_us)
            audio_max_us = us;
        {
            uint32_t load = audio_cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / FS) + audio_cpu_rem;
            audio_cpu_q8 = load / CPU_AVG;
            audio_cpu_rem = load % CPU_AVG;
        }
        if (fm1_audio_free_half() != half)
            felucca_dbg.late++;                         /* the DMA moved on while we rendered */
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us)
            felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = audio_cpu_q8;
    }
    felucca_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* codec and ALNK0 bring-up (fm1_audio.h) */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
