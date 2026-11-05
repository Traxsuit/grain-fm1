/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* MIDI for the app, over usb.c's rings. In: USB-MIDI and TRS MIDI IN (midi_uart.c) fill one ring of 4-byte USB-MIDI
 * event packets (byte 0: cable << 4 | CIN, then status, data 1, data 2) from the TIMER5 ISR; the app drains it
 * from ONE place, normally the audio ISR at the start of a block (sample-accurate enough: a block is 0.73 ms). SysEx
 * never reaches the ring: usb.c keeps it for the update protocol (ota.c); channel messages and the clock (F8 FA FB
 * FC) do. Out: packets for the USB host, from the
 * audio ISR or the main loop (not both). */
#define MIDI_LOST 0xFFFFFFFFu    /* midi_in_take: the ring overflowed and events are gone; release every held note */

static int midi_in_take(uint32_t *pkt, uint32_t *source)  /* 1: *pkt is the next event (source 1 USB, 2 TRS) */
{
    uint32_t at;
    if (midi_in_overflow) {                   /* a lost note-off must never leave a held note */
        mi_r = mi_w;
        RING_PUBLISH();
        midi_in_overflow = 0;
        *pkt = MIDI_LOST;
        if (source)
            *source = 0;
        return 1;
    }
    if (mi_r == mi_w)
        return 0;
    at = mi_r % MQ;
    *pkt = midi_in_q[at];
    if (source)
        *source = midi_in_source[at] ? midi_in_source[at] : 1u;
    RING_PUBLISH();                           /* the slot read before the index moves on */
    mi_r++;
    return 1;
}

static void midi_out(uint32_t status, uint32_t d1, uint32_t d2)   /* a channel message to the USB host */
{
    midi_out_event((status >> 4) | status << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24);
}
