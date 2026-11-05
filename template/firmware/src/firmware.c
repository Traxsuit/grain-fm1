/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* The firmware: one compilation unit (the HAL is header-only). Each file uses what the files above it define, so the
 * order matters: HAL, base and display, USB and MIDI, flash and update, the app (firmware/app/), then main.c. */

/* ---------------------------------------------------- build options --- */
/* build.py: FELUCCA_<NAME>=0 / 1 in the environment overrides these. The names are Felucca's (the platform's code
 * and its notes match Felucca and M-Vave-OP1 that way). */
#ifndef FELUCCA_FLASH
#define FELUCCA_FLASH 1          /* the flash driver (storage_hw.c): reads, erases and writes in the storage areas */
#endif
#ifndef FELUCCA_OTA
#define FELUCCA_OTA 1            /* the M-UPGRADE update entry (ota.c): how a new package gets in over USB. Keep it on */
#endif
#ifndef FELUCCA_OTA_DRYRUN
#define FELUCCA_OTA_DRYRUN 0     /* 1 = stage, ask "success", then undo: no record, no reset */
#endif
#ifndef FELUCCA_UART
#define FELUCCA_UART 1           /* TRS MIDI IN on UART1 / PH8 */
#endif
#define FELUCCA_CDC 0            /* (Felucca's serial console and USB audio input need its app; not in the kit) */
#define FELUCCA_UAC 0
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_900"    /* package identity (build.py: the .fwsc marker string); the installers check it */
#endif
#if FELUCCA_OTA && !FELUCCA_FLASH
#error "FELUCCA_OTA needs FELUCCA_FLASH"
#endif

/* ------------------------------------------------------------- HAL --- */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"

/* ----------------------------------------------------- base, display --- */
#include "platform.h"
#include "libc.c"
#include "lcd.c"
#include "gfx.c"
#include "panel.c"

/* ------------------------------------------------------- USB, MIDI --- */
#include "usb.c"
#if FELUCCA_UART
#include "midi_uart.c"
#endif
#include "midi.c"

/* ------------------------------------------------- audio, flash, update --- */
#include "../app/app.h"
#include "audio.c"
#if FELUCCA_FLASH
#include "fm1_flash.h"
#include "storage_hw.c"
#include "store.c"               /* records for the app: A/B copies with a CRC in the store area */
#endif
#if FELUCCA_OTA
#include "ota.c"
#include "ota_hw.c"
#endif

/* ------------------------------------------------------------- app --- */
#include "../app/app.c"

#include "main.c"
