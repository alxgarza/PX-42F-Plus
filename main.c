#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "hardware/irq.h"
#include "free42_app.h"
#include "display.h"
#include "keyboard.h"
#include "debug_checkpoint.h"
#include "font_24x10.h"
#include "font_10x16.h"
#include "font_8x12.h"
#include "rtc.h"
#include "battery.h"
#include "speaker.h"
#include "settings.h"
#include "settings_menu.h"
#include "watchdog_cfg.h"
#include "pico/flash.h"
#include "statefs.h"
#include "crash_report.h"
#include "splash.h"
#include "printer.h"
#include "power.h"
#include "cpuclock.h"
#include "idle.h"
#include "sounds.h"
#include "fonts.h"
#include "screenshot.h"
#include "keyhold.h"
#include "irtx.h"
#include "remote.h"
#include "tusb.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/powman.h"

/* powman_hw->scratch[7] while the splash image loads at boot, and [6]
 * while the sounds load (see there). */
#define SPLASH_LOADING_FLAG 0x5A1A5Au
#define FONTS_LOADING_FLAG  0xF0A75Au
#define SOUNDS_LOADING_FLAG 0x50DA5Au

/* Free42's core is a full desktop-oriented C++ calculator engine (BID128
 * decimal string parsing/formatting, nested command dispatch, etc.), and a
 * single call chain during ordinary digit entry can run several frames
 * deep (core_keydown -> keydown -> keydown_number_entry -> string2phloat
 * -> bid128_from_string, plus redisplay() -> phloat2string() ->
 * bid128_to_string() on top). The Pico SDK's default core0 stack is only
 * 0x800 (2KB), and even the most that fits in RP2350's dedicated SCRATCH_Y
 * bank (where the SDK's linker script places the core0 stack) is 4KB --
 * both proved too small: repeated digit keystrokes reliably hung inside
 * core_keydown() (confirmed by an isolated test showing the BID library
 * itself is fine in a *shallow* call, so it's the combined depth of the
 * real dispatch chain that overflows). Rather than fight the linker script
 * to relocate the stack out of that 4KB bank, run all of Free42 on core 1
 * with an explicit, generously-sized stack in ordinary SRAM instead --
 * RP2350 has 520KB of it, so there's no cost to being generous here. */
#define CORE1_STACK_WORDS (32 * 1024 / sizeof(uint32_t))
/* Must be at least 8-byte aligned: the ARM calling convention (AAPCS)
 * requires sp % 8 == 0, and compiled code relies on it -- e.g. functions
 * that keep 16-byte-aligned BID128 numbers (Phloat) in their frame carve
 * them out of the reserved space assuming it, and with sp % 8 == 4 the last
 * one overruns into the saved registers. A plain uint32_t array is only
 * guaranteed 4 bytes; this one used to land on an 8-byte boundary by luck,
 * until other variables moved it -- and then x crashed (mul_rr() clobbered
 * its caller's r4 with the top word of a number). */
static uint32_t core1_stack[CORE1_STACK_WORDS] __attribute__((aligned(16)));

/* This boot came from the RESET button (core1_main()): Memory Clear. */
static bool reset_button;

/* ===================== GPIO pin assignment =====================
 * Board: the PX-42F's own (boards/px42f.h) -- RP2350A, 16MB flash, no
 * wireless chip. GP23-25 and GP29 carry WL_* net labels on the PCB but
 * connect to nothing; they're free.
 * GP8 is the PSRAM footprint's chip select (no PSRAM is fitted).
 * GP28 (ADC2) is battery sense -- see battery.h.
 * GP26, GP27: I2C1, planned -- nothing in the firmware uses them yet.
 * Display pins (SPI0, RS, RST, CS) are defined in display.h.
 * Keyboard matrix pins (rows GP0-7, columns GP9-13) are defined in
 * keyboard.h.
 * ================================================================= */

/* --- PSRAM chip select (GP8) ---
 * No PSRAM is fitted, but its chip select keeps R3 (10k to +3V3) on the
 * PCB, while the RP2350's GP8 resets with its internal pull-down on: the
 * two fight, ~55uA all the time, on or off. Turn the internal pull off --
 * R3 alone holds the line high, and nothing flows. */
#define PSRAM_CS_PIN 8

static void release_psram_cs(void) {
    gpio_disable_pulls(PSRAM_CS_PIN);
}

/* --- Debug serial (UART0, separate from the USB CDC console) --- */
#define SERIAL_UART     uart0
#define SERIAL_BAUD     115200
#define SERIAL_TX_PIN   16
#define SERIAL_RX_PIN   17

static void init_ir_pin(void) {
    gpio_init(IR_LED_PIN);
    gpio_set_dir(IR_LED_PIN, GPIO_OUT);
    gpio_put(IR_LED_PIN, 0);
}

static void init_serial(void) {
    uart_init(SERIAL_UART, SERIAL_BAUD);
    gpio_set_function(SERIAL_TX_PIN, GPIO_FUNC_UART);
    gpio_disable_pulls(SERIAL_TX_PIN); /* idles high: no fight with the reset-default pull-down */
    gpio_set_function(SERIAL_RX_PIN, GPIO_FUNC_UART);
    /* A UART line idles high; and the pad's default pull-down on an
     * enabled input with nothing connected is RP2350 erratum E9's leak. */
    gpio_pull_up(SERIAL_RX_PIN);
}

/* Console commands over the USB serial link (separate from the debug
 * UART above), for things that don't belong on the calculator keyboard.
 * Currently just setting the RTC -- '!' isn't a key free42_process_char()
 * (see key_for_char() in free42_app.cc) ever maps to anything, so using
 * it to enter command mode can't collide with ordinary digit/operator
 * typing. Line-buffered: "!SETTIME HH:MM[:SS]\n", 24-hour, or
 * "!SETDATE YYYY-MM-DD\n". */
/* !HELP -- and shown by itself whenever a terminal opens the console port. */
static void print_help(void) {
    printf("\nPX-42F Console\n"
           "Commands (upper case, then Enter):\n"
           "  !HELP                  this list (also !?)\n"
           "  !STATUS                clock, core voltage, USB, battery, time, idle counts\n"
           "  !PRINTER               printer state and byte counters\n"
           "  !PRINTTEST             send a test line to the printer port\n"
           "  !SCREENSHOT            the screen as a PNG, in base64 (tools/screenshot.py saves it)\n"
           "  !SETDATE YYYY-MM-DD    set the date\n"
           "  !SETTIME HH:MM[:SS]    set the time (24h)\n"
           "Anything else is a keypress: 0-9 + - * /  . or , (decimal point)  e (EEX)\n"
           "  Enter (ENTER)\n\n");
}

static void handle_serial_command(const char *cmd) {
    int h, m, s;
    int y, mo, d;
    bool ok = false;
    if (strcmp(cmd, "HELP") == 0 || strcmp(cmd, "?") == 0) {
        print_help();
        return;
    }
    if (strcmp(cmd, "SCREENSHOT") == 0) {
        screenshot_to_console();
        return;
    }
    if (strcmp(cmd, "PRINTER") == 0) {
        printf("printer: Free42 flag 55 (PRON)=%d\n", (int) free42_printer_on());
        printer_report();
        return;
    }
    if (strcmp(cmd, "STATUS") == 0) {
        rtc_datetime_t now;
        rtc_get_datetime(&now);
        printf("clock: %lu MHz (%s), peripherals %lu MHz\n", (unsigned long) (clock_get_hz(clk_sys) / 1000000),
               clock_get_hz(clk_sys) > 48000000 ? "fast" : "idle", (unsigned long) (clock_get_hz(clk_peri) / 1000000));
        uint vsel = (uint) vreg_get_voltage();
        printf("core voltage: %u mV\n", vsel <= 15 ? 550 + 50 * vsel : 0); /* 50mV steps up to 1.30V */
        printf("usb: %s (mounted=%d suspended=%d) -> %s\n", idle_usb_host_active() ? "computer active" : "no computer",
               (int) tud_mounted(), (int) tud_suspended(),
               idle_usb_host_active() ? "full speed, 10ms polling" : "48MHz when idle, keyboard interrupt");
        printf("battery: %d mV, level %d%s\n", battery_read_mv(), battery_level(), battery_low() ? " (low)" : "");
        printf("time: %04d-%02d-%02d %02d:%02d:%02d, up %lu s\n", now.year, now.month, now.day, now.hour, now.min,
               now.sec, (unsigned long) (time_us_64() / 1000000));
        int trim = px_settings.time_trim, drift = px_settings.cal_drift;
        printf("clock correction: %c%d.%d s/day; measured drift: ", trim < 0 ? '-' : '+', abs(trim) / 10,
               abs(trim) % 10);
        if (px_settings.cal_span_h)
            printf("%s %d.%d s/day over %u h\n", drift < 0 ? "loses" : "gains", abs(drift) / 10, abs(drift) % 10,
                   px_settings.cal_span_h);
        else
            printf("none yet\n");
        uint32_t at_fast, at_slow;
        uint32_t off_hz = rtc_lposc_hz_at(POWER_OFF_MV, &at_fast, &at_slow);
        printf("low-power oscillator (the clock while off): %lu Hz at %d mV, %lu Hz at %d mV -> %lu Hz at %d mV "
               "(off, with the correction)\n",
               (unsigned long) at_fast, CPUCLOCK_FAST_MV, (unsigned long) at_slow, CPUCLOCK_SLOW_MV,
               (unsigned long) off_hz, POWER_OFF_MV);
        uint32_t why = powman_hw->chip_reset;
        printf("last reset: 0x%08lx (%s)\n", (unsigned long) why,
               reset_button                                           ? "RESET button: Memory Clear"
               : why & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS             ? "a restart since the RESET button"
               : power_woke_from_off()                                ? "ON from OFF"
               : why & POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS           ? "a restart since the last OFF"
               : why & POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS ? "watchdog or bootloader restart"
               : why & POWMAN_CHIP_RESET_HAD_POR_BITS                 ? "power-up"
                                                                      : "other");
        uint32_t waits, key_waits;
        idle_stats(&waits, &key_waits);
        printf("idle waits on battery: %lu (ended by a key: %lu)\n", (unsigned long) waits, (unsigned long) key_waits);
        return;
    }
    if (strcmp(cmd, "PRINTTEST") == 0) {
        static const char test[] = "PX-42F printer test \xc3\xb7 \xc3\x97 \xce\xa3\r\n";
        printer_write(test, (int) sizeof(test) - 1);
        printf("printer: test line queued\n");
        printer_report();
        return;
    }
    if (sscanf(cmd, "SETDATE %d-%d-%d", &y, &mo, &d) == 3) {
        if (rtc_set_date(y, mo, d))
            printf("RTC date set to %04d-%02d-%02d\n", y, mo, d);
        else
            printf("SETDATE: not a valid date (expected YYYY-MM-DD, 1970 or later)\n");
        return;
    }
    if (sscanf(cmd, "SETTIME %d:%d:%d", &h, &m, &s) == 3) {
        ok = true;
    } else if (sscanf(cmd, "SETTIME %d:%d", &h, &m) == 2) {
        s = 0;
        ok = true;
    }
    if (!ok) {
        printf("Unknown command: !%s (!HELP lists them)\n", cmd);
    } else if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
        printf("SETTIME: value out of range (expected HH:MM[:SS], 24h)\n");
    } else {
        rtc_set_hms(h, m, s);
        printf("RTC set to %02d:%02d:%02d\n", h, m, s);
    }
}

/* After the RESET button the Hello screen stays up until a key is pressed
 * (it only dismisses the screen) -- true -- or the Sleep Timer runs out:
 * false, turn off. Not on a computer's USB, as in the main loop. */
static bool hello_wait(void) {
    absolute_time_t since = get_absolute_time();
    uint8_t row, col;
    while (!keyboard_scan(&row, &col)) {
        watchdog_update();
        absolute_time_t until = make_timeout_time_ms(60000);
        if (px_settings.sleep_minutes != 0 && !idle_usb_host_active()) {
            absolute_time_t off = delayed_by_ms(since, (uint32_t) px_settings.sleep_minutes * 60000u);
            if (time_reached(off))
                return false;
            until = idle_earliest(until, off);
        }
        idle_wait(until);
    }
    while (keyboard_scan(&row, &col)) { /* not a keypress for the calculator */
        watchdog_update();
        sleep_ms(10);
    }
    return true;
}

static void core1_main(void)
{
    /* First thing: the speaker MOSFET's gate must be actively held low
     * (transistor off) before anything else runs -- see speaker_init(). */
    speaker_init();
    release_psram_cs();
    settings_load();

    bool clock_stopped = rtc_clock_init();
    battery_init();
    display_init();

    /* ON after OFF is a reboot too (power.c), but to the user it's just
     * turning the calculator on: straight back to the stack, no boot screen
     * -- unless there's a crash to report. */
    bool woke_from_off = power_woke_from_off();

    /* If the last reset was a crash (or a watchdog timeout), say where --
     * on the screen, since the serial console usually isn't attached yet.
     * See crash_report.h. */
    char crash1[32], crash2[32], crash3[32];
    /* A hang is our own watchdog timing out: watchdog_enable() left the
     * SDK's magic in scratch[4]. watchdog_caused_reboot() is also true for
     * the boot ROM's restarts (leaving the bootloader after picotool or a
     * UF2 copy) and for watchdog_reboot() -- those showed "WATCHDOG RESET"
     * with whatever checkpoint was left from before. */
    bool hung = watchdog_enable_caused_reboot();
    /* The RESET button pulls the chip's RUN pin low (PINOUT.html), which
     * POWMAN records -- unlike every other restart: ON, a crash, the
     * watchdog, leaving the bootloader after a flash. Pressed on purpose,
     * so it means Memory Clear (free42_init()); the others bring the
     * memory back. But the record is of POWMAN's last reset, and only RUN,
     * a power-up or a brown-out reset POWMAN: after a RESET press it said
     * RUN pin through every flash and crash restart that followed, and the
     * first flash after a RESET cleared the memory. Those reset the
     * always-on timer too, the others don't -- so a RESET press is the RUN
     * record with the clock stopped. */
    reset_button = clock_stopped && (powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) != 0;
    bool crashed = crash_report_take(crash1, crash2, crash3, sizeof(crash1),
                                     debug_checkpoint_name(watchdog_hw->scratch[0]), hung);
    keyboard_init();
    if (!woke_from_off || crashed) {
        display_clear();
        /* "Hello!" in the middle of the screen -- or, with a crash report
         * under it, along the top -- centred by its ink. */
        static const char hello[] = "Hello!";
        int left = 0, right = 0;
        display_text_ink(&font_24x10, hello, &left, &right);
        int hello_y = crashed ? 0 : (DISPLAY_HEIGHT - font_24x10.height) / 2;
        display_draw_string(&font_24x10, hello, (uint8_t) ((DISPLAY_WIDTH - (right - left + 1)) / 2 - left),
                            (uint8_t) hello_y, 0x00);
        if (crashed) {
            display_draw_string(&font_8x12, "Last reset:", 0, 56, 0x00);
            display_draw_string(&font_8x12, crash1, 0, 69, 0x00);
            display_draw_string(&font_8x12, crash2, 0, 82, 0x00);
            display_draw_string(&font_8x12, crash3, 0, 95, 0x00);
            display_draw_string(&font_8x12, "Press Any Key", 0, 108, 0x00);
        }
        display_flush();
        /* After a crash, leave the report up until a key is pressed -- long
         * enough to read or photograph it (a second wasn't) -- but a minute
         * at most, so one that crashes at every boot, unattended, still
         * gets on with it. The watchdog isn't running yet. Otherwise the
         * boot sound plays once the drive is up (below), the screen still up. */
        if (crashed) {
            uint8_t row, col;
            while (keyboard_scan(&row, &col)) /* the ON press that woke it, still held */
                sleep_ms(10);
            for (int i = 0; i < 6000 && !keyboard_scan(&row, &col); i++)
                sleep_ms(10);
        }
    }
    /* Every start but ON after OFF (that's ON.wav): a reset, a flash, a
     * crash's restart. No setting: it's the calculator's own sound, but
     * /SOUNDS/BOOT.wav can be replaced. */
    bool boot_sound = !woke_from_off;

    /* Swallow the ON press that woke it (or the key that dismissed the
     * crash report): the main loop would otherwise take it, still held, as
     * a fresh keypress. */
    if (woke_from_off || crashed) {
        uint8_t row, col;
        while (keyboard_scan(&row, &col))
            sleep_ms(10);
    }
    init_ir_pin();
    init_serial();

    if (hung) {
        printf("Rebooted by Watchdog!\n");
    }
    /* Report and clear the last checkpoint (see debug_checkpoint.h)
     * unconditionally, not just after a hang: a power-cycle or a picotool
     * reflash can also leave watchdog_hw's scratch registers holding
     * whatever was last written (they aren't cleared by every reset path).
     * Printed either way, then cleared, so each boot's report reflects only
     * what happened since the previous one. */
    uint32_t last_checkpoint = watchdog_hw->scratch[0];
    uint32_t last_detail = watchdog_hw->scratch[1];
    if (last_checkpoint != CKPT_NONE) {
        printf("Last checkpoint before this boot: %s (detail=%lu / 0x%lx)\n",
               debug_checkpoint_name(last_checkpoint),
               (unsigned long) last_detail, (unsigned long) last_detail);
    }
    debug_checkpoint(CKPT_NONE, 0);

    /* Start the watchdog: WATCHDOG_MS (watchdog_cfg.h) between
     * watchdog_update() calls or the chip reboots. */
    watchdog_resume();
    watchdog_update();

    /* The state-file volume (formatted on the very first boot). Only now:
     * its flash writes pause and then resume the watchdog, which must
     * already be running by then. */
    if (!statefs_init())
        printf("State disk unavailable (program image overlaps it?)\n");
    watchdog_update(); /* between the boot's stages: each is short, together they aren't */
    /* The user's splash image, if one is chosen -- guarded against boot
     * loops: POWMAN's scratch registers survive a crash or watchdog reset,
     * so a flag still set at the next boot means that boot died while
     * loading the image. Then fall back to the built-in image for good
     * (the setting is cleared) instead of crashing again. */
    if (powman_hw->scratch[7] == SPLASH_LOADING_FLAG) {
        printf("The last boot died loading splash image \"%s\" -- using the built-in image.\n",
               px_settings.splash_image);
        px_settings.splash_image[0] = 0;
        settings_save();
    }
    powman_hw->scratch[7] = SPLASH_LOADING_FLAG;
    debug_checkpoint(CKPT_BOOT_SPLASH, 0);
    splash_reload();
    powman_hw->scratch[7] = 0;
    watchdog_update();

    /* The calculator screen's fonts (/FONTS), under the same guard: a boot
     * that died loading one goes back to the built-in fonts. */
    if (powman_hw->scratch[7] == FONTS_LOADING_FLAG) {
        printf("The last boot died loading a font (\"%s\", \"%s\") -- using the built-in fonts.\n",
               px_settings.small_font, px_settings.large_font);
        px_settings.small_font[0] = 0;
        px_settings.large_font[0] = 0;
        settings_save();
    }
    powman_hw->scratch[7] = FONTS_LOADING_FLAG;
    debug_checkpoint(CKPT_BOOT_FONTS, 0);
    fonts_init();
    powman_hw->scratch[7] = 0;
    statefs_mkdir(IR_DIR); /* where remotes go (remote.h) */
    statefs_mkdir(PROGRAMS_DIR); /* .raw program files (Settings -> Programs) */
    watchdog_update();

    /* The sounds (/SOUNDS, made with the built-ins on first boot), with the
     * same guard: should a boot die loading one -- a broken WAV -- the
     * folder is put aside as /SOUNDS.BAD (the files kept, for the user to
     * fix) and made again from the built-ins. */
    bool use_sound_files = true;
    if (powman_hw->scratch[6] == SOUNDS_LOADING_FLAG) {
        printf("The last boot died loading a sound -- /SOUNDS moved to /SOUNDS.BAD.\n");
        use_sound_files = statefs_rename(SOUNDS_DIR, SOUNDS_DIR ".BAD") == 0;
    }
    powman_hw->scratch[6] = SOUNDS_LOADING_FLAG;
    debug_checkpoint(CKPT_BOOT_SOUNDS, 0);
    sounds_init(use_sound_files);
    debug_checkpoint(CKPT_BOOT_SOUND, 0);
    if (boot_sound)
        sounds_play_boot();
    powman_hw->scratch[6] = 0;
    watchdog_update();

    bool hello_off = reset_button && !crashed && !hello_wait();

    /* Start the Free42 core -- with its memory from before OFF, if any --
     * and draw the stack. */
    free42_init(crashed, reset_button);
    if (hello_off)
        free42_request_powerdown(); /* the main loop's first pass turns it off */
    debug_checkpoint(CKPT_ON_SOUND, 0);
    if (woke_from_off && !crashed && px_settings.on_sound)
        sounds_play_on(); /* turned on with ON: the stack is already back on screen */
    free42_set_printer(px_settings.printer != PRINTER_OFF);
    printf("Ready. Type !HELP for commands.\n");

    bool cmd_mode = false;
    char cmd_buf[32];
    int cmd_len = 0;
    int prev_serial_char = 0;

    absolute_time_t last_activity = get_absolute_time();
    int shown_hour, shown_min, shown_sec;
    rtc_get_hms(&shown_hour, &shown_min, &shown_sec);

    while (true) {
        watchdog_update();
        debug_checkpoint(CKPT_MAIN_LOOP_TOP, 0);

        /* SHIFT+EXIT was pressed: the settings menu takes over the screen
         * and keyboard until it's left -- by EXIT, back to the calculator,
         * or by turning off (handled just below). */
        if (free42_settings_requested()) {
            if (settings_menu_run() == SETTINGS_MENU_POWER_OFF)
                free42_request_powerdown();
            else
                free42_redisplay();
            last_activity = get_absolute_time();
        }

        /* OFF was pressed (or executed) since the last pass, or the
         * keyboard has been idle for the Sleep Timer setting -- auto
         * power-off, 10 minutes by default like the HP-42S. Not while
         * Free42's "continuous on" is set (XEQ "ON", flag 44), same as the
         * real calculator. See free42_sleep(). The watchdog can't be fed
         * while this core is halted waiting for ON, so stop it for the
         * duration. */
        int64_t idle_ms = absolute_time_diff_us(last_activity, get_absolute_time()) / 1000;
        /* ... and not while a computer has it on USB: it's on USB power then,
         * and off it would drop off USB -- no console, no drive, and no
         * flashing without the BOOT button. */
        bool auto_off = free42_auto_off_enabled() && px_settings.sleep_minutes != 0
                        && idle_ms >= (int64_t) px_settings.sleep_minutes * 60000 && !idle_usb_host_active();
        /* And before the battery gives out -- whatever the Sleep Timer says,
         * and in the middle of a program too: a brown-out can cut a flash
         * write short, and loses the memory. Not on a computer's USB (it's
         * charging). Checked every 10 seconds. */
        static absolute_time_t next_battery_check;
        bool battery_off = false;
        if (time_reached(next_battery_check)) {
            next_battery_check = make_timeout_time_ms(10000);
            battery_off = !idle_usb_host_active() && battery_empty();
        }
        if (free42_powerdown_requested() || auto_off || battery_off) {
            watchdog_pause();
            free42_sleep(battery_off); /* no off sound on an empty battery: it draws 200mA */
            watchdog_resume();
            last_activity = get_absolute_time();
            rtc_get_hms(&shown_hour, &shown_min, &shown_sec);
        }

        /* Redraw once a minute so the header clock keeps time even when
         * nothing else is happening. */
        int hour, min, sec;
        rtc_get_hms(&hour, &min, &sec);
        if (hour != shown_hour || min != shown_min) {
            shown_hour = hour;
            shown_min = min;
            free42_redisplay();
        }

        /* A terminal has just opened the console port (it raised DTR):
         * greet it with the command list. */
        static bool console_open = false;
        bool open = tud_cdc_n_connected(0);
        if (open && !console_open)
            print_help();
        console_open = open;

        /* USB serial console input (kept for convenient testing/typing).
         * '!' switches to line-buffered command mode (see
         * handle_serial_command()); anything else, character by character,
         * goes straight to the calculator as a keystroke, same as before. */
        int c = getchar_timeout_us(0);
        /* Terminals send Enter as CR, LF or CR LF. Treat CR LF as a single
         * Enter: drop the LF that directly follows a CR -- otherwise it was
         * a second ENTER keystroke (or, after a ! command, a stray one). */
        if (c == '\n' && prev_serial_char == '\r') {
            prev_serial_char = 0; /* only the one LF of the pair */
            c = PICO_ERROR_TIMEOUT;
        } else if (c != PICO_ERROR_TIMEOUT) {
            prev_serial_char = c;
        }
        if (c != PICO_ERROR_TIMEOUT) {
            if (!cmd_mode && c == '!') {
                cmd_mode = true;
                cmd_len = 0;
            } else if (cmd_mode) {
                if (c == '\r' || c == '\n') {
                    cmd_buf[cmd_len] = 0;
                    handle_serial_command(cmd_buf);
                    cmd_mode = false;
                    free42_redisplay(); /* show a new date/time right away */
                } else if (cmd_len < (int) sizeof(cmd_buf) - 1) {
                    cmd_buf[cmd_len++] = (char) c;
                }
            } else {
                debug_checkpoint(CKPT_SERIAL_CHAR, (uint32_t) (unsigned char) c);
                free42_process_char((char) c);
                last_activity = get_absolute_time();
            }
        }

        /* Physical keyboard matrix input. Split into down/hold/up instead
         * of one shot, so we can drive the real HP-42S/Free42 "hold to
         * confirm" timing: hold 250ms and the key's function name appears
         * on the Y row (free42_key_timeout1()); keep holding to 2000ms
         * and it switches to "NULL", meaning release now cancels the key
         * instead of performing it (free42_key_timeout2()). See the call
         * sequence documented in free42_app.h. */
        uint8_t row, col;
        if (keyboard_scan(&row, &col)) {
            debug_checkpoint(CKPT_KEY_DOWN, ((uint32_t) row << 8) | col);
            cpuclock_set_fast(true); /* work: full speed until it's idle again */
            key_hold_begin(row, col);
            sounds_key_play(free42_key_sound(row, col));
            free42_key_down(row, col);

            bool enqueued = free42_key_is_enqueued();
            int repeat_mode = free42_key_repeat_mode();
            absolute_time_t press_time = get_absolute_time();
            absolute_time_t next_repeat_time =
                    make_timeout_time_ms(repeat_mode == 1 ? 1000 : 500);
            bool shown_name = false;
            bool shown_null = false;

            /* Must feed the watchdog in here too: any ordinary human
             * keypress (even just 100-200ms, completely normal) already
             * exceeds the 100ms watchdog window, so without this every
             * single button press -- not just a stuck/miswired one --
             * would starve the watchdog and reboot. The serial input path
             * never hits this because it has no release-wait at all.
             *
             * This must NOT give up early on a still-held key: an earlier
             * version capped the wait at 2s, but a press held longer than
             * that fell through to the top of the main loop while the key
             * was still down, and the very next scan saw it as a brand
             * new press -- firing the same key repeatedly for as long as
             * it was held. The KEY_STUCK_MS (60s) cap here is only a
             * safety net for a truly stuck/miswired key, not something a
             * real press should ever reach.
             *
             * Must check this *specific* (row, col), not re-scan for
             * "any key down" -- re-scanning here was the actual cause of
             * an intermittent dropped/lagging keystroke bug: if a second
             * key got pressed (e.g. fast typing, fingers briefly
             * overlapping) before this one was released, keyboard_scan()
             * would report that other key as still-pressed "evidence"
             * that the original key was being held, so this loop kept
             * running the original key's hold-timeout logic while the
             * second key's own press was never dispatched at all --
             * silently swallowed until it, too, was released. */
            for (int i = 0; i < KEY_STUCK_MS / 10 && keyboard_is_held(row, col); i++) {
                watchdog_update();

                /* Keys held down (keyhold.h): a screenshot has been
                 * taken and both keys are up again (free42_key_up() below
                 * redraws the screen); or EXIT kept down turns the
                 * calculator off (having done its EXIT at key-down) -- not
                 * after SHIFT, where it's opening the settings menu.
                 * power_deep_off() waits for the key to be let go. */
                key_hold_t hold = key_hold_poll();
                if (hold == KEY_HOLD_SCREENSHOT)
                    break;
                if (hold == KEY_HOLD_OFF && !free42_settings_pending()) {
                    free42_request_powerdown();
                    break;
                }

                if (!enqueued && repeat_mode != 0) {
                    if (absolute_time_diff_us(get_absolute_time(), next_repeat_time) <= 0) {
                        debug_checkpoint(CKPT_KEY_REPEAT_TICK, ((uint32_t) row << 8) | col);
                        free42_key_repeat_tick();
                        repeat_mode = free42_key_repeat_mode();
                        next_repeat_time = make_timeout_time_ms(repeat_mode == 1 ? 200 : 100);
                    }
                } else if (!enqueued) {
                    int64_t held_ms = absolute_time_diff_us(press_time, get_absolute_time()) / 1000;
                    if (!shown_name && held_ms >= 250) {
                        debug_checkpoint(CKPT_KEY_TIMEOUT1, ((uint32_t) row << 8) | col);
                        free42_key_timeout1();
                        shown_name = true;
                    } else if (shown_name && !shown_null && held_ms >= 2000) {
                        debug_checkpoint(CKPT_KEY_TIMEOUT2, ((uint32_t) row << 8) | col);
                        free42_key_timeout2();
                        shown_null = true;
                    }
                }

                sleep_ms(10);
            }

            debug_checkpoint(CKPT_KEY_UP, ((uint32_t) row << 8) | col);
            free42_key_up();
            last_activity = get_absolute_time();
        }

        /* A running program gets its next time slice; while one runs,
         * don't idle, and don't let the auto power-off fire (the real
         * calculator doesn't turn off mid-program either). Otherwise, slow
         * the clock right down until there's work again (idle.h). */
        debug_checkpoint(CKPT_PROGRAM_SLICE, 0);
        if (free42_service()) {
            cpuclock_set_fast(true); /* e.g. a program carrying on after PSE */
            last_activity = get_absolute_time();
        } else if (!free42_settings_pending() && !free42_powerdown_requested()) {
            /* Nothing to do: wait for a key (idle.h), or until the next
             * thing that's due -- the end of a PSE, the header clock's next
             * minute. The auto power-off is checked each time round. */
            rtc_datetime_t now;
            rtc_get_datetime(&now);
            absolute_time_t until = make_timeout_time_ms((uint32_t) (60000 - (now.sec * 1000 + now.ms)));
            absolute_time_t timeout;
            if (free42_next_timeout(&timeout))
                until = idle_earliest(until, timeout);
            idle_wait(until);
        }
    }
}

/* picotool's "restart into the bootloader" request (pico_usb_reset, in core
 * 0's USB interrupt) ends in the boot ROM arming the watchdog to fire 10ms
 * later. But core 1 went on feeding the watchdog -- every 10ms or less
 * while a computer is connected -- which puts 100ms back each time, so the
 * restart only happened when the timing was lucky: flashing took retries,
 * or the BOOT button, and never worked from USB Drive mode. So stop core 1
 * first. The SDK's function is linked with --wrap (CMakeLists.txt). */
void __real_rom_reset_usb_boot_extra(int pin, uint32_t disable_mask, bool active_low) __attribute__((noreturn));

void __wrap_rom_reset_usb_boot_extra(int pin, uint32_t disable_mask, bool active_low) {
    multicore_reset_core1();
    __real_rom_reset_usb_boot_extra(pin, disable_mask, active_low);
}

int main()
{
    cpuclock_init(); /* first: the peripheral clock the UART and SPI are set up with */
    /* One of the once-a-minute wakes while off with the splash showing:
     * update its clock and power down again, nothing else (power.h). */
    power_off_wake();
    stdio_init_all();

    /* Everything else runs on core 1, with the large explicit stack
     * declared above -- see the comment there for why. */
    /* pico_stdio_usb runs TinyUSB (and with it the state-file drive's
     * callbacks, usb_msc.c) in a user IRQ it claims during stdio_init_all()
     * -- at the default priority, the same as the multicore-lockout and
     * timer IRQs. Equal priorities can't preempt each other, so while the
     * USB task is busy, core 1 could neither lock this core out to write
     * flash nor wake from sleep_ms() (the timer IRQ that wakes it runs
     * here). Drop the USB task to the lowest priority: everything else on
     * this core can then interrupt it. It's the only user IRQ claimed. */
    for (uint irq = FIRST_USER_IRQ; irq < FIRST_USER_IRQ + NUM_USER_IRQS; irq++)
        if (user_irq_is_claimed(irq)) {
            irq_set_priority(irq, PICO_LOWEST_IRQ_PRIORITY);
            printer_set_usb_task_irq(irq); /* the pump below holds it off while it runs */
        }

    crash_watch_core1_stack(core1_stack, CORE1_STACK_WORDS);
    multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof(core1_stack));

    /* Lets core 1 park this core in RAM while it writes the settings
     * sector (settings_save() -> flash_safe_execute()) -- code running
     * from flash here during an erase would crash. */
    flash_safe_execute_core_init();

    /* Nothing to do on this core but service its interrupts (USB stdio)
     * and send printer output to its USB port, so halt in between instead
     * of spinning -- matters for sleep. pico_stdio_usb's 1ms timer wakes
     * this core, so printing goes out within a millisecond. */
    /* WFE rather than WFI: it ends on any interrupt just the same, but also
     * on an event (SEV) from core 1 -- which a deep power-off uses to get
     * this core's attention when no interrupt is coming (on battery, with
     * no USB traffic). */
    while (true) {
        cpuclock_core0_before_sleep(); /* deep only during core 1's idle wait */
        __wfe();
        printer_usb_pump();
        power_core0_poll(); /* parks this core for good before a deep power-off */
    }
}
