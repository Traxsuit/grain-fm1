/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* Boot and main loop of the platform; the app hangs off it (firmware/app/app.h). Boot order: WDT first, boot-loop
 * guard, fatal vectors, guards; then flash check, LCD, input (TIMER5 IRQ, 10 kHz), the app, audio (ALNK0 IRQ), USB.
 * The main loop feeds the watchdog, serves the update protocol and the two ways into UBOOT, and gives the app a
 * frame every >= 15 ms. Taken from Felucca's main.c; what was Felucca's own (tracks, presets, menus) is the app's now. */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];

/* TIMER5 outranks ALNK0, so the scan keeps its 100 us pace while a half buffer renders. Nested in ALNK0 it only scans
 * (GPIO + fm1_in, nothing the audio ISR touches) and counts ms; USB and UART polls wait for the first tick after the
 * render, and the time spent nested is handed to the audio ISR so its load figures stay render-only. */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    felucca_dbg.timer_irqs++;
    fm1_input_tick();
    {   /* milliseconds from the 24 MHz TIMER4 (robust to a late tick) */
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due)
        owed |= 1u;                             /* 2 kHz: all USB SIE traffic lives here */
#if FELUCCA_UART
    if (sub % 5u == 2u)
        owed |= 2u;                             /* 2 kHz: <= ~7 bytes per call at 31250 baud */
#endif
    if (++sub == 10u)
        sub = 0;
    if (felucca_dbg.in_audio) {
        felucca_dbg.nested++;
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
#if FELUCCA_UART
    if (owed & 2u)
        uart_midi_poll();
#endif
    owed = 0;
}
extern void isr_timer5(void);

static void timer5_start(void)                 /* OSC /4 = 6 MHz, PRD 600 -> 10 kHz */
{
    fm1_timer5_start(isr_timer5, 4);   /* above ALNK0 (3): the scan nests into the render (see fm1_timer5_irq) */
}

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

/* a fatal exception (hal/fm1_irq.h): the record on the screen for 4 s, then a reset (the boot guard counts it) */
static void fm1_fault(const fm1_crash_t *c)
{
    static const uint16_t BG = 0x0018u, INK = 0xFFFFu;   /* fixed, outside the palettes (byte-swapped RGB565) */
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_wake_now();
    lcd_fill(0, 0, 240, 240, BG);
    draw_text_line(0, 8, 240, &AF_M, app_fault_name(), INK, BG, 1);
    hexs(b, c->vec);
    draw_text_line(10, 40, 220, &AF_M, b, INK, BG, 0);
    hexs(b, c->pc);
    draw_text_line(10, 60, 220, &AF_M, b, INK, BG, 0);
    hexs(b, c->emu);
    draw_text_line(10, 84, 220, &AF_M, b, INK, BG, 0);
    hexs(b, c->dbg);
    draw_text_line(10, 102, 220, &AF_M, b, INK, BG, 0);
    hexs(b, c->rets);
    draw_text_line(10, 120, 220, &AF_M, b, INK, BG, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

/* the flash: the expected 1 MiB part, else the firmware stays RAM-only (flash_ok 0: st_* and the update refuse) */
static void flash_boot(void)
{
#if FELUCCA_FLASH
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    irq_restore(f);
    if (flash_ok)
        fl_plain_window_init();                 /* flash above 0x93000 reads as plaintext through XIP */
#endif
}

static void uboot_now(const char *why)          /* the chip's boot mode (USB device WL80UBOOT); never returns */
{
    app_audio_stop();
    fm1_audio_stop();
    lcd_wake_now();
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 110, 240, &AF_M, why, T_THEME, 1);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                      /* intentional reset: not a failed boot */
    fm1_enter_uboot();
}

/* OCT- + OCT+ held 5 s: UBOOT with RAM intact (recovery, debug). A countdown shows from 2 s over the whole screen;
 * letting go cancels it and hands the screen back to the app */
static void uboot_hold(void)
{
    static uint32_t t0;
    static uint8_t shown;
    uint32_t both = 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_OCTUP];
    if ((fm1_in.buttons & both) != both) {
        if (shown) {
            shown = 0;
            app_redraw = 1;
        }
        t0 = fm1_ms;
    } else if (fm1_ms - t0 > 5000u)
        uboot_now("UBOOT");
    else if (fm1_ms - t0 > 2000u) {
        uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
        if (left != shown) {
            char b[12] = "UBOOT IN ";
            shown = (uint8_t)left;
            b[9] = (char)('0' + left);
            b[10] = 0;
            lcd_fill(0, 0, 240, 240, T_BG);
            draw_text_box(0, 110, 240, &AF_M, b, T_THEME, 1);
        }
    }
}

static void fm1_main(void)
{
    flash_boot();
#if FELUCCA_OTA
    if (flash_ok)
        ota_boot_cleanup();                     /* staging area left by an update */
#endif
    palette_set(UI_GREY_INDEX);                 /* the platform's screens (UBOOT, update); the app may pick another */
    lcd_init();
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    felucca_dbg.max_us = 0;
    felucca_dbg.in_audio = 0;                   /* .noinit: a reset inside the audio ISR left it set, and TIMER5
                                                   would treat every tick as nested (no USB poll) */
    felucca_dbg.prev_stage = felucca_dbg.stage; /* a WDT reset leaves the last breadcrumb here */
    felucca_dbg.prev_frames = felucca_dbg.ui_frames;
    felucca_dbg.prev_rst = fm1_boot.p3_rst;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    app_init();
    audio_init();
    usb_start();
#if FELUCCA_UART
    uart_midi_init();
#endif
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();

    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        aud_mute = 0;                           /* the sound back after a save's erases (audio.c aud_fade_half) */
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) {     /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        uboot_hold();
#if FELUCCA_OTA
        ota_service();                          /* M-UPGRADE handshake: the identity */
        if (usb.ota_req) {                      /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
            app_audio_stop();
            lcd_wake_now();                     /* (its progress on the screen) */
            if (flash_ok)
                ota_session();                  /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, T_BG);
            app_redraw = 1;
        }
#endif
        if (usb.uboot_req)                      /* SysEx F0 22 24 35 7D F7 from the host */
            uboot_now("UBOOT (USB)");
        felucca_dbg.ui_frames++;
        felucca_dbg.stage = 1;
        app_frame();
        felucca_dbg.stage = 9;
        while (fm1_ms - m < 15u)                /* ~60 frames/s at most */
            ;
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                /* flash driver code that must run from RAM */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
