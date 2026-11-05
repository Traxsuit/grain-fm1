/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware side of storage.c (FELUCCA_FLASH): flash reads in short IRQ-off windows so the
 * audio keeps up, erases and writes only inside the storage areas, the audio buffer silenced
 * first. The host tests (storage_test.c) provide their own st_* on a simulated NOR. */
static uint8_t flash_ok;                 /* JEDEC id matched at boot (persist_boot) */
static int st_read(uint32_t off, void *dst, uint32_t n)   /* 256-byte IRQ-off windows: audio keeps up */
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static void audio_silence(void)                 /* IRQs off: the DMA would loop stale audio (buzz) */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}
/* M-Vave-OP1, before an erase: the DAC faded out and both halves of the buffer silent (audio.c aud_mute), waited for
 * at most 20 ms; nothing to wait for before the audio runs (persist_boot). The main loop gives the sound back at the
 * start of its next pass (main.c), so a save of several erases and writes stays silent through all of them */
static void audio_fade_out(void)
{
    uint32_t t0 = fm1_ticks();
    if (!aud_live)
        return;
    aud_mute = 1;
    while ((!aud_muted || aud_quiet < 2u) && fm1_ticks() - t0 < 20000u * FM1_TICKS_PER_US)
        ;
}
static int st_erase(uint32_t off)
{
    uint32_t took, f;
    int rc;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    audio_fade_out();                           /* (M-Vave-OP1: no click before or after) */
    f = irq_save();
    audio_silence();
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    irq_restore(f);
    return rc;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!FL_STORE_OK(off, n))                   /* only inside the storage areas */
        return -8;
    return fl_write(off, src, n);
}
