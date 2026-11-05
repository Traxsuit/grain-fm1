/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Stephen Webster */
/* The controller: parameter values, pages, the MIDI channel and octave, and every MIDI message GRAIN-FM1 sends or
 * takes. No drawing here (ui.c), no flash (data.c).
 *
 * The knobs are endless encoders: the firmware owns each value (0..127 within its row's min..max), a click moves it
 * and the new absolute value is sent as a 7-bit CC. Nothing can disagree with a physical position, so there is no
 * pickup; the first turn of a session sets Live to the FM-1's value.
 *
 * Contexts. MIDI in and out live in the audio ISR (midi.c: one context each). The main loop changes values and
 * queues their CCs in CCQ (single producer: the main loop, single consumer: the ISR); the ISR sends them, plays the
 * keys as notes and drains MIDI in, handing incoming CCs back through INQ (producer: the ISR, consumer: the main
 * loop). That second ring is the v2 return path: a Max for Live device sending Granulator III's values back as the
 * same CCs updates the values and the screen (ctl_take_input), and nothing is echoed. */

#define OCT_MIN (-3)
#define OCT_MAX 3
#define KEY_NOTE0 53u                 /* note key 0 is F3 (hal/fm1_input.h) */
#define KEY_VEL 100u                  /* the keys have no velocity: a fixed one */
#define QN 32u                        /* ring sizes (powers of two) */

static struct {
    uint8_t val[MAXPARAMS];           /* the values, by PARAMS index */
    uint8_t page;                     /* PAGES index */
    uint8_t channel;                  /* the global channel, 0..15 (shown 1..16) */
    int8_t octave;                    /* OCT_MIN .. OCT_MAX */
    uint8_t dirty[MAXPARAMS];         /* a CC to send for this value at the end of the frame */
    int8_t touched;                   /* the slot (0..3) turned last, -1 none */
    uint32_t touch_ms;                /* when */
    uint32_t change_ms;               /* the last change of anything stored (data.c's autosave) */
    uint8_t changed;                  /* something stored changed since the last save */
} ctl;

/* main loop -> ISR: CC packets to send; ISR -> main loop: CC messages received (status << 16 | cc << 8 | value) */
static uint32_t ccq[QN], inq[QN];
static volatile uint32_t ccq_w, ccq_r, inq_w, inq_r;

/* the ISR's own: the keys as notes */
static struct {
    uint32_t keys;                    /* the keys sounding (sent a note-on) */
    uint32_t ignore;                  /* keys held through app_audio_stop: silent until let go */
    uint8_t note[27];                 /* the note each sounding key sent: its note-off matches after an octave change */
    uint8_t ch[27];                   /* .. and its channel */
    volatile int8_t octave;           /* main loop -> ISR */
    volatile uint8_t channel;         /* main loop -> ISR */
    volatile uint8_t stop_req;        /* main loop -> ISR: every note off (app_audio_stop) */
    volatile uint32_t rx_count;       /* MIDI messages received (the screen's activity mark) */
} keys;

static uint32_t param_ch(uint32_t i) { return PARAMS[i].ch ? PARAMS[i].ch - 1u : ctl.channel; }

static int32_t param_at(uint32_t page, uint32_t slot)   /* PARAMS index of a page slot, -1 if empty */
{
    uint32_t i;
    for (i = 0; i < NPARAMS; i++)
        if (PARAMS[i].page == page && PARAMS[i].slot == slot)
            return (int32_t)i;
    return -1;
}

static int32_t param_of_role(uint32_t role)              /* the first parameter with that role, -1 none */
{
    uint32_t i;
    for (i = 0; i < NPARAMS; i++)
        if (PARAMS[i].role == role)
            return (int32_t)i;
    return -1;
}

static uint32_t role_val(uint32_t role, uint32_t fallback)   /* 0..127 for the visualiser */
{
    int32_t i = param_of_role(role);
    return i < 0 ? fallback : ctl.val[i];
}

static uint32_t clamp_param(uint32_t i, int32_t v)
{
    return v < PARAMS[i].min ? PARAMS[i].min : v > PARAMS[i].max ? PARAMS[i].max : (uint32_t)v;
}

static void ctl_mark(void)
{
    ctl.changed = 1;
    ctl.change_ms = fm1_ms;
}

static void ctl_defaults(void)
{
    uint32_t i;
    memset(&ctl, 0, sizeof ctl);
    for (i = 0; i < NPARAMS; i++)
        ctl.val[i] = (uint8_t)clamp_param(i, PARAMS[i].def);
    ctl.touched = -1;
}

/* clicks -> value steps: one per click turned slowly; a fast turn (several clicks in one frame) moves 2x or 4x, so
 * 0..127 takes a flick, not 127 clicks */
static int32_t accel(int32_t clicks)
{
    int32_t a = clicks < 0 ? -clicks : clicks;
    return clicks * (a >= 5 ? 4 : a >= 2 ? 2 : 1);
}

static void ctl_turn(uint32_t slot, int32_t clicks)     /* KNOB slot+1 turned */
{
    int32_t i = param_at(ctl.page, slot);
    uint32_t v;
    if (!clicks)
        return;
    ctl.touched = (int8_t)slot;
    ctl.touch_ms = fm1_ms;
    if (i < 0)
        return;
    v = clamp_param((uint32_t)i, ctl.val[i] + accel(clicks));
    if (v != ctl.val[i]) {
        ctl.val[i] = (uint8_t)v;
        ctl.dirty[i] = 1;
        ctl_mark();
    }
}

static void ctl_set_page(int32_t p)
{
    p = (p % (int32_t)NPAGES + (int32_t)NPAGES) % (int32_t)NPAGES;
    if ((uint32_t)p != ctl.page) {
        ctl.page = (uint8_t)p;
        ctl.touched = -1;
        ctl_mark();
    }
}

static void ctl_set_channel(int32_t c)
{
    c = c < 0 ? 0 : c > 15 ? 15 : c;
    if ((uint32_t)c != ctl.channel) {
        ctl.channel = (uint8_t)c;
        ctl_mark();
    }
}

static void ctl_set_octave(int32_t o)
{
    o = o < OCT_MIN ? OCT_MIN : o > OCT_MAX ? OCT_MAX : o;
    if (o != ctl.octave) {
        ctl.octave = (int8_t)o;
        ctl_mark();
    }
}

/* the end of a frame: one CC per changed value (its latest), into the ring for the ISR. A full ring keeps the rest
 * dirty for the next frame: the newest value always gets out, none is sent twice in a frame */
static void ctl_flush(void)
{
    uint32_t i;
    keys.octave = ctl.octave;
    keys.channel = ctl.channel;
    for (i = 0; i < NPARAMS; i++) {
        uint32_t st;
        if (!ctl.dirty[i])
            continue;
        if (ccq_w - ccq_r >= QN)
            break;
        st = 0xB0u | param_ch(i);
        ccq[ccq_w % QN] = (st >> 4) | st << 8 | (uint32_t)PARAMS[i].cc << 16 | (uint32_t)ctl.val[i] << 24;
        RING_PUBLISH();
        ccq_w++;
        ctl.dirty[i] = 0;
    }
}

/* v1's return path (the v2 hook): a CC on a parameter's channel and number sets its value, clamped, without an
 * echo. Returns the PARAMS index it set, or -1 */
static int32_t ctl_cc_in(uint32_t ch, uint32_t cc, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < NPARAMS; i++)
        if (PARAMS[i].cc == cc && param_ch(i) == ch) {
            ctl.val[i] = (uint8_t)clamp_param(i, (int32_t)v);
            ctl.dirty[i] = 0;                 /* Live's value wins over one not sent yet */
            ctl_mark();
            return (int32_t)i;
        }
    return -1;
}

static void ctl_take_input(void)                        /* main loop: what the ISR received */
{
    while (inq_r != inq_w) {
        uint32_t m = inq[inq_r % QN];
        RING_PUBLISH();
        inq_r++;
        ctl_cc_in((m >> 16) & 15u, (m >> 8) & 127u, m & 127u);
    }
}

/* ---------------------------------------------------------- audio ISR --- */
static __attribute__((noinline)) void keys_all_off(void)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (keys.keys >> k & 1u)
            midi_out(0x80u | keys.ch[k], keys.note[k], 0);
    keys.keys = 0;
    keys.ignore = fm1_in.notes;
}

static __attribute__((noinline)) void keys_change(uint32_t now)
{
    uint32_t k, diff = now ^ keys.keys;
    for (k = 0; k < 27u; k++) {
        if (!(diff >> k & 1u))
            continue;
        if (now >> k & 1u) {
            int32_t n = (int32_t)(KEY_NOTE0 + k) + 12 * keys.octave;
            if (n < 0 || n > 127)
                continue;                     /* (never at OCT_MIN..OCT_MAX, but the note must exist) */
            keys.note[k] = (uint8_t)n;
            keys.ch[k] = keys.channel & 15u;
            midi_out(0x90u | keys.ch[k], keys.note[k], KEY_VEL);
            keys.keys |= 1u << k;
        } else if (keys.keys >> k & 1u) {
            midi_out(0x80u | keys.ch[k], keys.note[k], 0);
            keys.keys &= ~(1u << k);
        }
    }
}

static __attribute__((noinline)) void midi_rx(uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xFFu;
    if (pkt == MIDI_LOST)                     /* nothing held from MIDI in: values just catch up later */
        return;
    keys.rx_count++;
    if ((st & 0xF0u) == 0xB0u && inq_w - inq_r < QN) {
        inq[inq_w % QN] = st << 16 | ((pkt >> 16) & 0x7Fu) << 8 | ((pkt >> 24) & 0x7Fu);
        RING_PUBLISH();
        inq_w++;
    }
}

static void ctl_audio(void)                             /* once per app_audio call */
{
    uint32_t pkt, now;
    while (midi_in_take(&pkt, 0))
        midi_rx(pkt);
    if (keys.stop_req) {
        keys.stop_req = 0;
        keys_all_off();
        ccq_r = ccq_w;                        /* (an update or UBOOT follows: nothing more to send) */
        return;
    }
    while (ccq_r != ccq_w) {
        midi_out_event(ccq[ccq_r % QN]);
        RING_PUBLISH();
        ccq_r++;
    }
    now = fm1_in.notes;
    keys.ignore &= now;
    now &= ~keys.ignore;
    if (now != keys.keys)
        keys_change(now);
}
