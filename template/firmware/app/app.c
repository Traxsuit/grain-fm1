/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* The start app: proof that the platform works end to end, and a template for the real one. It shows its name and
 * version, the keys, buttons and knobs as they are pressed and turned (and lights their LEDs), the last MIDI note and
 * the audio load, plays a triangle wave on the lowest held key or MIDI note, and keeps the knobs' values in flash
 * (SAVE stores them, power-on loads them: store.c). Replace it (the fm1-new-project
 * skill does that with you); keep the contract in app.h. */
#ifndef APP_NAME
#define APP_NAME "FM-1 KIT"
#endif
#ifndef APP_VERSION
#define APP_VERSION "v0.1"       /* build.py --release X.Y */
#endif
#define APP_ID 0x3054494Bu       /* this app's records in the store (store.c): every app its own (setup/new_project.py
                                  * draws one), so another firmware's data in the same flash reads as empty */

/* ---------------------------------------------------------- audio (ISR) --- */
#define NOTE_KEY0 53u            /* note key 0 is F3 (hal/fm1_input.h fm1_in.notes) */
#define AMP 12000                /* the triangle's peak, Q15 (~ -9 dB) */
#define RAMP 64                  /* gain steps per block up or down: ~6 ms from silence to full, no clicks */

/* phase increments of C4 .. B4 (2^32 f / FS); other octaves shift */
static const uint32_t OCT4_INC[12] = {25480119u, 26995246u, 28600467u, 30301139u, 32102938u, 34011878u,
                                      36034330u, 38177043u, 40447168u, 42852281u, 45400411u, 48100060u};

static struct {
    uint32_t phase, inc;
    int32_t gain;                /* 0 .. 1024 */
    uint8_t midi_note;           /* held over MIDI, 0 = none */
    volatile uint8_t last_midi;  /* for the screen: the last note-on received (0: none yet) */
    volatile uint8_t stop_req;   /* main loop -> ISR: forget every note (app_audio_stop) */
} tone;

static uint32_t note_inc(uint32_t n)             /* MIDI note -> phase increment */
{
    uint32_t oct = n / 12u, inc = OCT4_INC[n % 12u];
    return oct >= 5u ? inc << (oct - 5u) : inc >> (5u - oct);
}

static __attribute__((noinline)) void tone_midi(uint32_t pkt)   /* one channel message (any channel) */
{
    uint32_t st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
    if (pkt == MIDI_LOST)
        tone.midi_note = 0;
    else if (st == 0x90u && d2) {
        tone.midi_note = (uint8_t)d1;
        tone.last_midi = (uint8_t)d1;
    } else if ((st == 0x80u || st == 0x90u) && d1 == tone.midi_note)
        tone.midi_note = 0;
}

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t keys = fm1_in.notes, pkt, i, note = 0;
    int32_t target;
    while (midi_in_take(&pkt, 0))
        tone_midi(pkt);
    if (tone.stop_req) {                         /* after the queue: what came before the stop is forgotten too */
        tone.stop_req = 0;
        tone.midi_note = 0;
        tone.gain = 0;
    }
    if (keys)
        note = NOTE_KEY0 + (uint32_t)__builtin_ctz(keys);
    else if (tone.midi_note)
        note = tone.midi_note;
    if (note)
        tone.inc = note_inc(note);
    target = note ? 1024 : 0;
    for (i = 0; i < n; i++) {
        int32_t tri, s;
        if (tone.gain < target)
            tone.gain += tone.gain + RAMP / 8 > target ? target - tone.gain : RAMP / 8;
        else if (tone.gain > target)
            tone.gain -= tone.gain - RAMP / 8 < target ? tone.gain - target : RAMP / 8;
        tone.phase += tone.inc;
        tri = (int32_t)(tone.phase >> 16) - 32768;            /* saw, -32768 .. 32767 */
        tri = (tri < 0 ? -tri : tri) * 2 - 32768;             /* triangle */
        s = (tri * AMP >> 15) * tone.gain >> 10;
        out[2u * i] = s;
        out[2u * i + 1u] = s;
    }
}

static void app_audio_stop(void) { tone.stop_req = 1; }

/* -------------------------------------------------------- screen (main loop) --- */
static int32_t knob[4];                          /* KNOB 1..4, summed steps: the stored record (slot 0) */
static int8_t saved;                             /* the status line: 1 SAVED, -1 SAVE FAILED, 0 nothing */
static uint32_t drawn_sig;                       /* what the screen shows (lazy drawing: redraw on a change) */
static uint32_t last_buttons;                    /* (presses: the buttons down now and not last frame) */

static uint32_t screen_sig(void)                 /* every value the screen shows goes in here */
{
    uint32_t h = 2166136261u, i;
    uint32_t v[10] = {fm1_in.notes, fm1_in.buttons, (uint32_t)knob[0], (uint32_t)knob[1], (uint32_t)knob[2],
                      (uint32_t)knob[3], tone.last_midi, audio_cpu_q8 * 100u / 256u, usb.config, (uint32_t)saved};
    for (i = 0; i < 10u; i++)
        h = (h ^ v[i]) * 16777619u;
    return h;
}

static void num(char *b, int32_t v)              /* signed decimal */
{
    char t[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        *b++ = '-';
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < sizeof t);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void draw_keys(uint32_t y)                /* the 27 note keys, F3 .. G5, held ones in the accent */
{
    uint32_t k, keys = fm1_in.notes;
    cv_begin(240, 28, T_BG);
    for (k = 0; k < 27u; k++)
        cv_rrect(13 + (int32_t)k * 8, 2, 6, 24, 2, (keys >> k) & 1u ? T_ACCENT : T_RAISE, T_BG);
    cv_blit(0, y);
}

static void draw_buttons(uint32_t y)             /* the held buttons by their printed names */
{
    char line[80];
    uint32_t b, n = 0, k;
    for (b = 0; b < NB; b++) {
        if (!panel_held(b))
            continue;
        if (n)
            line[n++] = ' ';
        for (k = 0; B_NAME[b][k] && n + 1u < sizeof line; k++)
            line[n++] = B_NAME[b][k];
    }
    line[n] = 0;
    if (!n)
        str_cpy(line, "-", sizeof line);
    if (text_w(&AF_M, line) > 232) {             /* many held: the end ellipsised, the rest left-aligned */
        char fit[48];
        text_fit(fit, sizeof fit, line, &AF_M, 232);
        cv_begin(240, AF_M.h, T_BG);
        cv_text_flags(4, 0, &AF_M, fit, T_TEXT, T_BG, 1u);
    } else {
        cv_begin(240, AF_M.h, T_BG);
        cv_text_in(0, 0, 240, &AF_M, line, T_TEXT, T_BG);
    }
    cv_blit(0, y);
}

static void draw_status(uint32_t y)              /* knobs, MIDI, load, USB */
{
    char b[16];
    uint32_t i;
    cv_begin(240, 2u * AF_S.h + 4u, T_BG);
    for (i = 0; i < 4u; i++) {
        num(b, knob[i]);
        cv_text(8 + (int32_t)i * 58, 0, &AF_S, b, T_THEME);
    }
    str_cpy(b, "MIDI ", sizeof b);
    num(b + 5, tone.last_midi);
    cv_text(8, AF_S.h + 4, &AF_S, tone.last_midi ? b : "MIDI -", T_MID);
    str_cpy(b, "CPU ", sizeof b);
    num(b + 4, (int32_t)(audio_cpu_q8 * 100u / 256u));
    cv_text(96, AF_S.h + 4, &AF_S, b, T_MID);
    cv_text(184, AF_S.h + 4, &AF_S, saved > 0 ? "SAVED" : saved < 0 ? "FAILED" : usb.config ? "USB" : "",
            saved < 0 ? T_REC : T_MID);
    cv_blit(0, y);
}

static void draw_all(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_line(0, 40, 240, &AF_L, APP_NAME, T_TEXT, T_BG, 1);
    draw_text_line(0, 76, 240, &AF_S, APP_VERSION, T_MID, T_BG, 1);
    drawn_sig = ~screen_sig();
}

static void app_init(void)
{
    int32_t i;
    if (store_load(0, APP_ID, knob, sizeof knob) == (int32_t)sizeof knob)   /* else: none, or not ours: zeros */
        for (i = 0; i < 4; i++)
            knob[i] = knob[i] < -999 ? -999 : knob[i] > 999 ? 999 : knob[i];   /* (never trust what was stored) */
    draw_all();
}

static void app_frame(void)
{
    uint32_t i, sig, pressed = fm1_in.buttons & ~last_buttons;
    last_buttons = fm1_in.buttons;
    for (i = 0; i < 4u; i++) {                   /* -999 .. 999: the screen's columns hold four digits */
        int32_t s = panel_enc(EN_K1 + i);
        if (s)
            saved = 0;
        knob[i] += s;
        knob[i] = knob[i] < -999 ? -999 : knob[i] > 999 ? 999 : knob[i];
    }
    if (pressed & panel_mask(B_SAVE))            /* ~50 ms: the audio fades out and in around the erase */
        saved = store_save(0, APP_ID, knob, sizeof knob) ? -1 : 1;
    for (i = 0; i < 14u; i++)                    /* LEDs: what is held is lit (ids: 0..13 buttons, 14..40 keys) */
        fm1_led_key(i, (int)((fm1_in.buttons >> i) & 1u));
    for (i = 0; i < 27u; i++)
        fm1_led_key(14u + i, (int)((fm1_in.notes >> i) & 1u));
    if (app_redraw) {
        app_redraw = 0;
        draw_all();
    }
    sig = screen_sig();
    if (sig == drawn_sig)
        return;
    drawn_sig = sig;
    draw_keys(120);
    draw_buttons(160);
    draw_status(190);
}

static const char *app_fault_name(void) { return APP_NAME " CRASH"; }
