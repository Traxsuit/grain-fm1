/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Stephen Webster */
/* What GRAIN-FM1 keeps over a power-off: one record in store.c slot 0 (APP_ID): the values, the page, the global
 * channel and the octave. Saved by SAVE and on its own AUTOSAVE_MS after the last change (never every change: a save
 * erases a flash sector and fades the audio ~50 ms, and flash wears).
 * The record carries a hash of the table (params.c): a changed table (a CC, a range, a default, a row added or moved)
 * starts from the table's defaults instead of loading values onto the wrong parameters. Append-only: new fields go
 * at the end, a shorter (older) record leaves them at their defaults. */

#define REC_SLOT 0u
#define AUTOSAVE_MS 5000u

typedef struct {
    uint32_t table;                   /* table_hash() of the table it was saved with */
    uint8_t page, channel;
    int8_t octave;
    uint8_t nvals;
    uint8_t val[MAXPARAMS];
} rec_t;

static int8_t saved;                  /* the status line: 1 SAVED, -1 SAVE FAILED, 0 nothing */
static uint32_t saved_ms;

static uint32_t table_hash(void)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NPARAMS; i++) {
        const param_t *p = &PARAMS[i];
        uint32_t v = (uint32_t)p->page | p->slot << 4 | p->cc << 8 | p->ch << 16 | p->fmt << 24;
        h = (h ^ v) * 16777619u;
        h = (h ^ ((uint32_t)p->min | p->max << 8 | p->def << 16 | p->role << 24)) * 16777619u;
    }
    return (h ^ NPAGES) * 16777619u;
}

static void data_load(void)
{
    rec_t r;
    int32_t n;
    uint32_t i;
    ctl_defaults();
    memset(&r, 0, sizeof r);
    n = store_load(REC_SLOT, APP_ID, &r, sizeof r);
    if (n < (int32_t)sizeof r.table)
        return;                                        /* none, damaged, another app's: the defaults */
    /* field by field, as far as the record reaches (an older, shorter one leaves the rest at the defaults); every
     * value checked: never trust what was stored */
    if (n > (int32_t)__builtin_offsetof(rec_t, page) && r.page < NPAGES)
        ctl.page = r.page;
    if (n > (int32_t)__builtin_offsetof(rec_t, channel))
        ctl.channel = r.channel & 15u;
    if (n > (int32_t)__builtin_offsetof(rec_t, octave))
        ctl.octave = r.octave < OCT_MIN ? OCT_MIN : r.octave > OCT_MAX ? OCT_MAX : r.octave;
    if (r.table != table_hash() || r.nvals != NPARAMS || n < (int32_t)(__builtin_offsetof(rec_t, val) + NPARAMS))
        return;                                        /* another table: its values are not these parameters' */
    for (i = 0; i < NPARAMS; i++)
        ctl.val[i] = (uint8_t)clamp_param(i, r.val[i]);
}

static void data_save(void)
{
    rec_t r;
    memset(&r, 0, sizeof r);
    r.table = table_hash();
    r.page = ctl.page;
    r.channel = ctl.channel;
    r.octave = ctl.octave;
    r.nvals = (uint8_t)NPARAMS;
    memcpy(r.val, ctl.val, NPARAMS);
    saved = store_save(REC_SLOT, APP_ID, &r, sizeof r) ? -1 : 1;
    saved_ms = fm1_ms;
    if (saved > 0)
        ctl.changed = 0;
    else
        ctl.change_ms = fm1_ms;                        /* a failed save retries AUTOSAVE_MS later, not every frame */
}

static void data_frame(void)                           /* the autosave, and the status line's timeout */
{
    if (ctl.changed && fm1_ms - ctl.change_ms >= AUTOSAVE_MS && flash_ok)
        data_save();
    if (saved && fm1_ms - saved_ms >= 1500u)
        saved = 0;
}
