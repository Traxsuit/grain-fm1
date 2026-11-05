/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* Records in flash for the app: STORE_SLOTS slots of up to STORE_MAX bytes each (settings, a project, a bank of
 * sounds), every slot two 4 KiB sectors written in turn (A/B), each copy with a header: magic, the app's id, a
 * sequence number, the length, a CRC-32 of header and data. store_load takes the newest copy that checks out, so a
 * save cut off by a power loss leaves the one before. After Felucca's storage.c, in a form any app can use.
 *
 * Where: [STORE_LO, STORE_HI) = 0x97000 .. 0xE0000, the part of the flash the update loader never touches (it
 * rewrites the app area below and may erase sectors above that look like its records). Every firmware made from
 * this kit (and Felucca, M-Vave-OP1) uses this area, and they replace one another on the same unit: data in it can
 * be another firmware's. APP_ID tells them apart: give every app its own (app.c), and treat what store_load refuses
 * as empty. Bigger or other layouts: st_read / st_erase / st_prog (storage_hw.c) inside the same area.
 *
 * Main loop only, never the audio ISR: an erase stops everything ~45 ms (the audio fades out and in around it:
 * storage_hw.c audio_fade_out). Save only when the user will not mind that (stopped, or on a SAVE press), and not in
 * the first 30 s after power-on if it can be avoided (the boot guard counts a crash there). Stored formats are
 * append-only: new fields at the end, the length tells a reader what is there. */
#define STORE_LO 0x00097000u
#define STORE_HI 0x000E0000u
#define STORE_SLOTS 36u                          /* (0xE0000 - 0x97000) / 8 KiB = 36.5 */
#define STORE_HEAD 20u
#define STORE_MAX (0x1000u - STORE_HEAD)         /* 4076 B a record */
#define STORE_MAGIC 0x5354524Bu                  /* "KRTS" */
_Static_assert(STORE_LO + STORE_SLOTS * 0x2000u <= STORE_HI, "the slots fit the store area");

typedef struct { uint32_t magic, app, seq, len, crc; } store_head_t;
_Static_assert(sizeof(store_head_t) == STORE_HEAD, "the header is 20 bytes");

static uint32_t store_crc(uint32_t c, const uint8_t *p, uint32_t n)   /* CRC-32 (IEEE), bitwise: no table */
{
    uint32_t k;
    c = ~c;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

static uint32_t store_sector(uint32_t slot, uint32_t copy) { return STORE_LO + slot * 0x2000u + copy * 0x1000u; }

/* a copy's header if it is one of ours for this app and its data checks out (crc over the header's first four
 * words and the data); data read into buf (STORE_MAX) */
static int store_check(uint32_t off, uint32_t app, store_head_t *h, uint8_t *buf)
{
    uint32_t c;
    if (st_read(off, h, STORE_HEAD) || h->magic != STORE_MAGIC || h->app != app || h->len > STORE_MAX)
        return 0;
    if (h->len && st_read(off + STORE_HEAD, buf, h->len))
        return 0;
    c = store_crc(0, (const uint8_t *)h, 16u);
    return store_crc(c, buf, h->len) == h->crc;
}

static uint8_t store_buf[STORE_MAX] __attribute__((section(".pool")));

/* the newest valid copy of slot: its length (its data in dst, at most max bytes), or -1 if there is none. A record
 * shorter than max leaves the rest of dst as it was (the caller's defaults: an older, shorter format) */
static int32_t store_load(uint32_t slot, uint32_t app, void *dst, uint32_t max)
{
    store_head_t h[2];
    uint32_t ok[2], k, pick, n;
    if (slot >= STORE_SLOTS || !flash_ok)
        return -1;
    for (k = 0; k < 2u; k++)
        ok[k] = (uint32_t)store_check(store_sector(slot, k), app, &h[k], store_buf);
    if (!ok[0] && !ok[1])
        return -1;
    pick = ok[0] && ok[1] ? (int32_t)(h[1].seq - h[0].seq) > 0 : ok[1];
    if (!store_check(store_sector(slot, pick), app, &h[pick], store_buf))   /* (read again: the buffer is shared) */
        return -1;
    n = h[pick].len < max ? h[pick].len : max;
    memcpy(dst, store_buf, n);
    return (int32_t)h[pick].len;
}

/* write len bytes as the slot's newest copy, over the older one (the newer stays until this one checks out).
 * 0 = saved and read back, < 0 = nothing changed for a reader (the last good copy is still there) */
static int store_save(uint32_t slot, uint32_t app, const void *src, uint32_t len)
{
    store_head_t h[2], w;
    uint32_t ok[2], k, dst, off;
    if (slot >= STORE_SLOTS || len > STORE_MAX)
        return -2;
    if (!flash_ok)
        return -1;
    for (k = 0; k < 2u; k++)
        ok[k] = (uint32_t)store_check(store_sector(slot, k), app, &h[k], store_buf);
    dst = !ok[0] ? 0u : !ok[1] ? 1u : (int32_t)(h[1].seq - h[0].seq) > 0 ? 0u : 1u;   /* the empty or older copy */
    w.magic = STORE_MAGIC;
    w.app = app;
    w.seq = ok[!dst] ? h[!dst].seq + 1u : 1u;     /* (the other copy is the newest when there is one) */
    w.len = len;
    w.crc = store_crc(store_crc(0, (const uint8_t *)&w, 16u), src, len);
    off = store_sector(slot, dst);
    if (st_erase(off))
        return -3;
    if (len && st_prog(off + STORE_HEAD, src, len))   /* the data first, the header last: a cut write has none */
        return -4;
    if (st_prog(off, &w, STORE_HEAD))
        return -4;
    return store_check(off, app, &h[dst], store_buf) && h[dst].seq == w.seq ? 0 : -5;
}
