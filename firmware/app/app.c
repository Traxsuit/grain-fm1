/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors
 * Copyright (C) 2026 Stephen Webster */
/* GRAIN-FM1: a silent USB MIDI controller for Ableton Live's Granulator III (docs/SPEC.md). The four knobs send CCs
 * from a table of pages (params.c), the keys pass notes through, the screen shows the page's four parameters, a zoom
 * on the knob being turned and a grain visualiser. Written with Claude Code's assistance.
 *   params.c  the table: pages and parameters (edit this one)
 *   ctl.c     values, pages, channel, octave; MIDI out (CCs, notes) and in (CCs: the v2 return path)
 *   data.c    the record in flash: values, page, channel, octave; SAVE and the autosave
 *   ui.c      the screen
 * This file: the five hooks of the app contract (app.h) and the controls. */
#ifndef APP_NAME
#define APP_NAME "GRAIN-FM1"
#endif
#ifndef APP_VERSION
#define APP_VERSION "v0.1"       /* build.py --release X.Y */
#endif
#define APP_ID 0xE68193FDu       /* this app's records in the store (store.c): another firmware's read as empty */

#include "params.c"
#include "ctl.c"
#include "data.c"
#include "ui.c"

/* the page buttons, in the order of the printed row; a page opens with the button its PAGES row names */
static const uint8_t PAGE_BUTTONS[6] = {B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO};
static const uint8_t SOON_BUTTONS[4] = {B_ARP, B_SEQ, B_PLAY, B_REC};
static uint32_t last_buttons;

/* ---------------------------------------------------------- audio (ISR) --- */
static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t i;
    ctl_audio();                                 /* MIDI in and out, the keys as notes */
    for (i = 0; i < 2u * n; i++)                 /* no sound: GRAIN-FM1 is a controller */
        out[i] = 0;
}

static void app_audio_stop(void) { keys.stop_req = 1; }

/* ---------------------------------------------------------- main loop --- */
static void app_init(void)
{
    data_load();
    ui_init();
    ui_msg(APP_VERSION);
    ui_frame(1);
    app_redraw = 0;
}

static int32_t page_of_button(uint32_t b)        /* the PAGES index a button opens, -1 none */
{
    uint32_t p;
    for (p = 0; p < NPAGES; p++)
        if (PAGES[p].button == b)
            return (int32_t)p;
    return -1;
}

static void controls(void)
{
    uint32_t i, pressed = fm1_in.buttons & ~last_buttons;
    int32_t s;
    char b[8];
    last_buttons = fm1_in.buttons;
    for (i = 0; i < 4u; i++)
        ctl_turn(i, panel_enc(EN_K1 + i));
    if ((s = panel_enc(EN_SELECT)) != 0)
        ctl_set_page(ctl.page + s);
    if ((s = panel_enc(EN_PRESET)) != 0) {
        ctl_set_channel(ctl.channel + s);
        str_cpy(b, "CH ", sizeof b);
        put_num(b + 3, ctl.channel + 1, 0);
        ui_msg(b);
    }
    if (panel_enc(EN_ALGO))
        ui_msg("ALGORITHM SOON");
    for (i = 0; i < 6u; i++)
        if (pressed & panel_mask(PAGE_BUTTONS[i])) {
            int32_t p = page_of_button(PAGE_BUTTONS[i]);
            if (p >= 0)
                ctl_set_page(p);
            else
                ui_msg("PAGE SOON");
        }
    if (pressed & panel_mask(B_HOME))
        ctl_set_page(0);
    for (i = 0; i < 4u; i++)
        if (pressed & panel_mask(SOON_BUTTONS[i]))
            ui_msg("SOON");
    /* OCT- / OCT+: a press moves the keys an octave; with the other one held it does nothing (both held 5 s is the
     * platform's recovery gesture, UBOOT) */
    if ((pressed & panel_mask(B_OCTDN)) && !panel_held(B_OCTUP))
        ctl_set_octave(ctl.octave - 1);
    if ((pressed & panel_mask(B_OCTUP)) && !panel_held(B_OCTDN))
        ctl_set_octave(ctl.octave + 1);
    if (pressed & panel_mask(B_SAVE))
        data_save();
}

static void leds(void)
{
    uint32_t i;
    for (i = 0; i < NB; i++)                     /* the current page's button lit, a held button lit while held */
        fm1_led_key(panel.btn[i], (int)(PAGES[ctl.page].button == i || panel_held(i)));
    for (i = 0; i < 27u; i++)
        fm1_led_key(14u + i, (int)((fm1_in.notes >> i) & 1u));
}

static void app_frame(void)
{
    int full = app_redraw;
    app_redraw = 0;
    controls();
    ctl_take_input();                            /* CCs from Live (v2): values follow, nothing is echoed */
    ctl_flush();                                 /* one CC per changed value, to the ISR */
    data_frame();
    leds();
    ui_frame(full);
}

static const char *app_fault_name(void) { return APP_NAME " CRASH"; }
