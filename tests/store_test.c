/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* Host test of firmware/src/store.c on a simulated NOR (erase: 0xFF; program: bits only go 1 -> 0): round trip,
 * the copies in turn, the newest wins (across the sequence number's wrap), a save cut short at any byte leaves the
 * last good record, a damaged copy falls back, another app's data reads as empty and is overwritten cleanly,
 * nothing written outside the slot's two sectors, limits.
 *   cc -O1 -o build/host/store_test tests/store_test.c && build/host/store_test */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t nor[0x100000];
static uint8_t flash_ok = 1;
static uint32_t prog_budget = 0xFFFFFFFFu;     /* bytes that may still be programmed: a power cut after that */
static uint32_t lo_touched = 0xFFFFFFFFu, hi_touched;

static void touch(uint32_t off, uint32_t n)
{
    if (off < lo_touched)
        lo_touched = off;
    if (off + n > hi_touched)
        hi_touched = off + n;
}
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off & 0xFFFu)
        return -8;
    touch(off, 0x1000u);
    memset(nor + off, 0xFF, 0x1000u);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    touch(off, n);
    for (i = 0; i < n; i++) {
        if (!prog_budget)
            return -1;                          /* the power is gone: the rest never arrives */
        prog_budget--;
        nor[off + i] &= s[i];
    }
    return 0;
}

#include "../firmware/src/store.c"

static int failed;
static void check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    failed += !ok;
}

#define APP 0x4B495431u                          /* "KIT1" */
#define OTHER 0x4F503121u

int main(void)
{
    uint8_t a[300], b[300], r[300];
    uint32_t i, cut, ok_all = 1;
    int32_t n;
    memset(nor, 0xFF, sizeof nor);
    for (i = 0; i < sizeof a; i++) {
        a[i] = (uint8_t)(i * 7u);
        b[i] = (uint8_t)(i * 13u + 1u);
    }

    check("empty flash: no record", store_load(3, APP, r, sizeof r) == -1);
    check("save: ok", store_save(3, APP, a, sizeof a) == 0);
    memset(r, 0, sizeof r);
    n = store_load(3, APP, r, sizeof r);
    check("load: the length and the bytes", n == (int32_t)sizeof a && !memcmp(r, a, sizeof a));
    check("save a second: the other copy", store_save(3, APP, b, sizeof b) == 0 &&
                                           nor[store_sector(3, 0)] != 0xFF && nor[store_sector(3, 1)] != 0xFF);
    n = store_load(3, APP, r, sizeof r);
    check("load: the newest", n == (int32_t)sizeof b && !memcmp(r, b, sizeof b));
    check("a third save goes over the oldest", store_save(3, APP, a, 100) == 0 && store_load(3, APP, r, sizeof r) == 100);
    check("other slots untouched", lo_touched >= store_sector(3, 0) && hi_touched <= store_sector(3, 0) + 0x2000u);

    for (cut = 0; cut < STORE_HEAD + sizeof b; cut += 1u) {   /* a power cut at every programmed byte */
        uint8_t keep[0x2000];
        memcpy(keep, nor + store_sector(5, 0), sizeof keep);
        if (cut == 0) {
            store_save(5, APP, a, sizeof a);
            memcpy(keep, nor + store_sector(5, 0), sizeof keep);
        }
        prog_budget = cut;
        store_save(5, APP, b, sizeof b);
        prog_budget = 0xFFFFFFFFu;
        n = store_load(5, APP, r, sizeof r);
        ok_all &= n == (int32_t)sizeof a && !memcmp(r, a, sizeof a);
        memcpy(nor + store_sector(5, 0), keep, sizeof keep);
    }
    check("a save cut at any byte: the last good record loads", ok_all);

    store_save(6, APP, a, sizeof a);
    store_save(6, APP, b, sizeof b);
    nor[store_sector(6, 1) + STORE_HEAD + 17u] ^= 0x10u;   /* the newest copy (B) damaged */
    n = store_load(6, APP, r, sizeof r);
    check("a damaged newest copy: the older one loads", n == (int32_t)sizeof a && !memcmp(r, a, sizeof a));
    check("... and the next save replaces the damaged one", store_save(6, APP, b, sizeof b) == 0 &&
                                                            store_load(6, APP, r, sizeof r) == (int32_t)sizeof b && !memcmp(r, b, sizeof b));

    store_save(7, OTHER, a, sizeof a);
    check("another app's record reads as empty", store_load(7, APP, r, sizeof r) == -1);
    check("... is overwritten cleanly", store_save(7, APP, b, sizeof b) == 0 && store_load(7, APP, r, sizeof r) == (int32_t)sizeof b);
    memset(nor + store_sector(8, 0), 0x5A, 0x2000u);   /* foreign bytes that are no record at all */
    check("foreign bytes read as empty", store_load(8, APP, r, sizeof r) == -1);

    store_save(9, APP, a, 10);
    {   /* the sequence number about to wrap: the next save must still be the newest */
        store_head_t h;
        uint32_t off = store_sector(9, 0);
        memcpy(&h, nor + off, sizeof h);
        h.seq = 0xFFFFFFFFu;
        h.crc = store_crc(store_crc(0, (const uint8_t *)&h, 16u), nor + off + STORE_HEAD, h.len);
        memset(nor + off, 0xFF, 0x1000u);
        memcpy(nor + off, &h, sizeof h);
        memcpy(nor + off + STORE_HEAD, a, 10);
    }
    store_save(9, APP, b, 20);
    check("sequence wrap: the save after 0xFFFFFFFF is the newest", store_load(9, APP, r, sizeof r) == 20 && !memcmp(r, b, 20));

    memset(r, 0xEE, sizeof r);
    store_save(10, APP, a, 8);
    n = store_load(10, APP, r, sizeof r);
    check("a shorter record leaves the rest of the buffer", n == 8 && r[8] == 0xEE && r[299] == 0xEE);
    n = store_load(3, APP, r, 50);
    check("a longer record: only max bytes copied, its length returned", n == 100 && r[50] == 0xEE);
    check("an empty record (length 0) saves and loads", store_save(11, APP, a, 0) == 0 && store_load(11, APP, r, 0) == 0);

    check("too long: refused", store_save(12, APP, a, STORE_MAX + 1u) == -2);
    check("slot out of range: refused", store_save(STORE_SLOTS, APP, a, 4) == -2 && store_load(STORE_SLOTS, APP, r, 4) == -1);
    flash_ok = 0;
    check("no flash: refused, nothing loads", store_save(13, APP, a, 4) == -1 && store_load(3, APP, r, 4) == -1);
    flash_ok = 1;
    check("the store area ends below the OTA staging (0xE0000)", store_sector(STORE_SLOTS - 1u, 1) + 0x1000u <= 0xE0000u);

    puts(failed ? "STORE TEST FAILED" : "store test passed");
    return failed ? 1 : 0;
}
