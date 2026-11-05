/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Stephen Webster */
/* THE TABLE: every page and every parameter GRAIN-FM1 controls, in one place. Edit here, rebuild, and map the CCs to
 * Granulator III in Live's MIDI map mode. tests/app_test.c checks the table (ranges, defaults, no CC used twice on
 * one channel, at most one parameter per page slot, every role present), so a typo fails the build's tests, not
 * the unit.
 *
 * PAGES: name (header, up to ~12 characters), colour (RGB565: the page's slots, arcs and header; 0xRRRRRGGGGGGBBBBB),
 *        the button that opens it (panel.c B_*; SELECT steps through all pages in this order).
 * PARAMS: page (index into PAGES), slot (0..3 = KNOB 1..4), name (on screen, ~10 characters), CC number (0..119),
 *        channel (0 = the global channel set with PRESETS, 1..16 = fixed), min / max (the range sent, 0..127),
 *        default, display format, role (what the grain visualiser reads it as; ROLE_NONE for most).
 * Changing the table resets the stored values to these defaults (data.c: the record carries a hash of the table). */

enum { FMT_RAW, FMT_PCT, FMT_BIP };               /* 0..127 / 0..100 % of min..max / -64..+63 around 64 */
enum { ROLE_NONE, ROLE_POSITION, ROLE_SPRAY, ROLE_FREQ, ROLE_SIZE, NROLES };

typedef struct {
    const char *name;
    uint16_t colour;
    uint8_t button;
} page_t;

typedef struct {
    uint8_t page, slot;
    const char *name;
    uint8_t cc, ch, min, max, def, fmt, role;
} param_t;

#define RGB565(r, g, b) ((uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | (b) >> 3))

static const page_t PAGES[] = {
    {"GRAIN", RGB565(255, 168, 40), B_FX},        /* amber */
    {"SPRAY", RGB565(80, 184, 236), B_SCL},       /* ice */
};

static const param_t PARAMS[] = {
    /* page slot name           cc  ch  min max  def  fmt      role */
    {0, 0, "Filter Freq",       20, 0,  0, 127, 127, FMT_PCT, ROLE_NONE},
    {0, 1, "Position",          21, 0,  0, 127,  64, FMT_PCT, ROLE_POSITION},
    {0, 2, "Grain Size",        22, 0,  0, 127,  64, FMT_PCT, ROLE_SIZE},
    {0, 3, "Variation",         23, 0,  0, 127,   0, FMT_PCT, ROLE_NONE},
    {1, 0, "Spray",             24, 0,  0, 127,  20, FMT_PCT, ROLE_SPRAY},
    {1, 1, "Frequency",         25, 0,  0, 127,  64, FMT_PCT, ROLE_FREQ},
    {1, 2, "Param 7",           26, 0,  0, 127,  64, FMT_RAW, ROLE_NONE},
    {1, 3, "Param 8",           27, 0,  0, 127,  64, FMT_RAW, ROLE_NONE},
};

#define NPAGES (sizeof PAGES / sizeof PAGES[0])
#define NPARAMS (sizeof PARAMS / sizeof PARAMS[0])
#define MAXPARAMS 48u                                 /* the stored record's room (data.c): 12 pages of 4 */
_Static_assert(NPAGES >= 1 && NPAGES <= 12, "1..12 pages");
_Static_assert(NPARAMS <= MAXPARAMS, "the record holds MAXPARAMS values");
