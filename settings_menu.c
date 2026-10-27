#include "settings_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "tusb.h"
#include "battery.h"
#include "display.h"
#include "font_10x16.h"
#include "font_8x12.h"
#include "free42_app.h"
#include "keyboard.h"
#include "rtc.h"
#include "settings.h"
#include "speaker.h"
#include "statefs.h"
#include "flashdisk.h"
#include "usb_msc.h"
#include "debug_checkpoint.h"
#include "crash_report.h"
#include "splash.h"
#include "printer.h"
#include "cpuclock.h"
#include "idle.h"
#include "sounds.h"
#include "fonts.h"
#include "keyhold.h"
#include "remote.h"
#include "icons.h"

#ifndef PICO_PROGRAM_VERSION_STRING
#define PICO_PROGRAM_VERSION_STRING "?"
#endif

/* The Plus42 release src/core was taken from (plus42-upstream/HISTORY),
 * and the Intel Decimal Floating-Point Math Library it computes with
 * (third_party/IntelRDFPMathLib20U1). */
#define PLUS42_VERSION  "1.4a"
#define INTEL_LIB_VERSION "2.0 Update 1"

/* HP-42S key codes, as free42_key_code() returns them. */
#define K_ENTER FREE42_KEY_ENTER
#define K_BSP   FREE42_KEY_BSP
#define K_UP    FREE42_KEY_UP
#define K_DOWN  FREE42_KEY_DOWN
#define K_SHIFT FREE42_KEY_SHIFT
#define K_EXIT  FREE42_KEY_EXIT

/* Pseudo key codes from wait_key(). */
#define K_NONE     0  /* timed out (only with a timeout) */
#define K_TICK     -1 /* the minute changed: redraw the clock */
#define K_SLEEP    -2 /* sleep timer ran out */
#define K_POWEROFF -3 /* SHIFT then EXIT */

/* Layout, in font_8x12 rows: a title bar, up to LIST_ROWS items, and a
 * hint line. */
#define TITLE_HEIGHT 13
#define ITEM_Y0      15
#define ITEM_STRIDE  13
#define LIST_ROWS    7
#define HINT_Y       (DISPLAY_HEIGHT - 12)
#define CHAR_W       8 /* font_8x12.advance */

#define MAX_STATES 32

static bool shift_on = false;

/* Details of the key wait_key() last returned, for screens that need more
 * than its key code -- name entry wants its letter, and whether SHIFT was
 * pressed before it. */
static uint8_t last_row, last_col;
static bool last_shifted;
static int last_key; /* the key wait_key() last returned */

/* Back out of a screen: EXIT, or the backspace arrow. (Where digits or a
 * name are typed, the arrow deletes first, and backs out once it's empty.) */
static bool is_back(int key) {
    return key == K_EXIT || key == K_BSP;
}

static int digit_of(int key) {
    switch (key) {
        case 34: return 0;
        case 29: return 1;
        case 30: return 2;
        case 31: return 3;
        case 24: return 4;
        case 25: return 5;
        case 26: return 6;
        case 19: return 7;
        case 20: return 8;
        case 21: return 9;
        default: return -1;
    }
}

/* Waits for one keypress (returned on release, so a held key never
 * repeats) and returns its key code -- or a K_* pseudo code. SHIFT is
 * handled here: it toggles shift_on, and SHIFT then EXIT is K_POWEROFF;
 * shift is dropped by any other key. timeout_ms = 0 waits indefinitely;
 * otherwise K_NONE after that long. K_SLEEP once no key has been pressed
 * for the Sleep Timer -- counted from the last keypress (idle_since), not
 * from this call: the screens come back here every minute to redraw their
 * clock (K_TICK), which used to start the count over, so the menu never
 * went to sleep. */
static absolute_time_t idle_since;

static int wait_key_timeout(uint32_t timeout_ms) {
    absolute_time_t deadline = timeout_ms ? make_timeout_time_ms(timeout_ms) : at_the_end_of_time;
    int start_hour, start_min, sec;
    rtc_get_hms(&start_hour, &start_min, &sec);

    while (true) {
        watchdog_update();

        uint8_t row, col;
        if (keyboard_scan(&row, &col)) {
            cpuclock_set_fast(true); /* work: full speed until it's idle again */
            key_hold_begin(row, col);
            sounds_key_play(free42_key_code(row, col) == FREE42_KEY_ENTER ? KEY_SOUND_ENTER : KEY_SOUND_ENTRY);
            for (int i = 0; i < KEY_STUCK_MS / 10 && keyboard_is_held(row, col); i++) {
                watchdog_update();
                /* Keys held down (keyhold.h). After a screenshot the keys
                 * are up and SHIFT wasn't toggled; K_TICK redraws. */
                key_hold_t hold = key_hold_poll();
                if (hold == KEY_HOLD_SCREENSHOT) {
                    idle_since = get_absolute_time();
                    return K_TICK;
                }
                if (hold == KEY_HOLD_OFF) {
                    shift_on = false;
                    return K_POWEROFF;
                }
                sleep_ms(10);
            }
            idle_since = get_absolute_time();

            int key = free42_key_code(row, col);
            if (key == K_SHIFT) {
                shift_on = !shift_on;
                return K_SHIFT;
            }
            last_row = row;
            last_col = col;
            last_shifted = shift_on;
            shift_on = false;
            if (last_shifted && key == K_EXIT)
                return K_POWEROFF;
            if (key != 0) {
                last_key = key;
                return key;
            }
            continue;
        }

        if (px_settings.sleep_minutes != 0 && !idle_usb_host_active() /* not on a computer's USB: see main.c */
                && absolute_time_diff_us(idle_since, get_absolute_time()) / 1000
                   >= (int64_t) px_settings.sleep_minutes * 60000)
            return K_SLEEP;

        int hour, min;
        rtc_get_hms(&hour, &min, &sec);
        if (hour != start_hour || min != start_min)
            return K_TICK;

        if (time_reached(deadline))
            return K_NONE;

        /* Wait for a key (idle.h) -- or for the deadline, the next minute
         * (for K_TICK), or the sleep timer, whichever is first. */
        rtc_datetime_t now;
        rtc_get_datetime(&now);
        absolute_time_t until = idle_earliest(deadline,
                                              make_timeout_time_ms((uint32_t) (60000 - (now.sec * 1000 + now.ms))));
        if (px_settings.sleep_minutes != 0)
            until = idle_earliest(until, delayed_by_ms(idle_since, (uint32_t) px_settings.sleep_minutes * 60000u));
        idle_wait(until);
    }
}

static int wait_key(void) {
    return wait_key_timeout(0);
}

static void error_beep(void) {
    sounds_play_tone(10); /* Free42's own error squeak (shell_beeper(10)) */
}

/* Black title bar with white text, the shift indicator, and the time at
 * the right -- same look as the calculator's status header. */
static void draw_title(const char *title) {
    display_fill_rect(0, 0, DISPLAY_WIDTH - 1, TITLE_HEIGHT - 2, true);
    display_draw_string(&font_8x12, title, 2, 0, 0xFF);
    if (shift_on)
        display_draw_char(&font_8x12, '\r', 174, 0, 0xFF); /* ↵, code 13, like the calculator's */

    char buf[8];
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    rtc_format_time(buf, sizeof(buf), &now, true);
    display_draw_string(&font_8x12, buf, (uint8_t) (DISPLAY_WIDTH - 2 - (int) strlen(buf) * CHAR_W), 0, 0xFF);
}

static void draw_hint(const char *hint) {
    display_draw_dotted_hline(0, DISPLAY_WIDTH - 1, HINT_Y - 2);
    display_draw_string(&font_8x12, hint, 2, HINT_Y, 0x00);
}

static void draw_line(int line, const char *text) {
    display_draw_string(&font_8x12, text, 2, (uint8_t) (ITEM_Y0 + line * ITEM_STRIDE), 0x00);
}

static void show_busy(const char *title, const char *text) {
    display_clear();
    draw_title(title);
    draw_line(1, text);
    display_flush();
}

static void format_minutes(char *buf, size_t size, uint16_t minutes) {
    if (minutes == 0)
        snprintf(buf, size, "Off");
    else
        snprintf(buf, size, "%u min", (unsigned) minutes);
}

typedef enum { EDIT_OK, EDIT_CANCEL, EDIT_POWER_OFF } edit_result_t;

/* A screen of up to six lines of text and a hint; waits for ENTER (OK) or
 * EXIT (cancel). For messages, both just mean "seen". */
static edit_result_t show_message(const char *title, const char *const *lines, int count, const char *hint) {
    while (true) {
        display_clear();
        draw_title(title);
        for (int i = 0; i < count; i++)
            draw_line(i, lines[i]);
        draw_hint(hint);
        display_flush();

        int key = wait_key();
        if (key == K_POWEROFF || key == K_SLEEP)
            return EDIT_POWER_OFF;
        if (key == K_ENTER)
            return EDIT_OK;
        if (is_back(key))
            return EDIT_CANCEL;
    }
}

/* ===================== lists ===================== */

#define LIST_BACK      -1
#define LIST_POWER_OFF -2

typedef void (*item_value_fn)(int item, char *buf, size_t size);

/* A scrolling list: ▲/▼ move (wrapping), ENTER returns the highlighted
 * index, EXIT returns LIST_BACK. *selected is kept across calls, so a
 * screen reopened after an item returns with the same row highlighted.
 * value, if non-NULL, gives a right-aligned value for each row. */
static int run_list(const char *title, const char *const *names, int count, item_value_fn value,
                    int *selected, const char *hint) {
    while (true) {
        if (*selected >= count)
            *selected = count - 1;
        if (*selected < 0)
            *selected = 0;
        int first = *selected - (LIST_ROWS - 1);
        if (first < 0)
            first = 0;

        display_clear();
        draw_title(title);
        for (int row = 0; row < LIST_ROWS && first + row < count; row++) {
            int i = first + row;
            int y = ITEM_Y0 + row * ITEM_STRIDE;
            uint8_t color = 0x00;
            if (i == *selected) {
                display_fill_rect(0, (uint8_t) (y - 1), DISPLAY_WIDTH - 1, (uint8_t) (y + 11), true);
                color = 0xFF;
            }
            display_draw_string(&font_8x12, names[i], 4, (uint8_t) y, color);
            if (value != NULL) {
                char buf[24];
                value(i, buf, sizeof(buf));
                int x = DISPLAY_WIDTH - 4 - (int) strlen(buf) * CHAR_W;
                display_draw_string(&font_8x12, buf, (uint8_t) x, (uint8_t) y, color);
            }
        }
        /* More above / below the visible rows. */
        if (first > 0)
            display_draw_string(&font_8x12, "\x5e", DISPLAY_WIDTH - 20, (uint8_t) (HINT_Y), 0x00); /* up arrow */
        if (first + LIST_ROWS < count)
            display_draw_string(&font_8x12, "\x0e", DISPLAY_WIDTH - 10, (uint8_t) (HINT_Y), 0x00); /* down arrow */
        draw_hint(hint);
        display_flush();

        int key = wait_key();
        if (key == K_POWEROFF || key == K_SLEEP)
            return LIST_POWER_OFF;
        if (is_back(key))
            return LIST_BACK;
        if (key == K_UP)
            *selected = (*selected + count - 1) % count;
        else if (key == K_DOWN)
            *selected = (*selected + 1) % count;
        else if (key == K_ENTER)
            return *selected;
    }
}

/* ===================== number entry ===================== */

/* Digit entry against a mask: each '#' is one digit slot, anything else is
 * shown literally -- "##:##" for a time, "####-##-##" for a date. With
 * require_full, ENTER only accepts a completely filled mask; otherwise at
 * least one digit. note is a line of context above the entry (e.g. the
 * current value); error, if non-NULL, is shown below it. On EDIT_OK the
 * typed digits (no literals) are in digits[], NUL-terminated. */
static edit_result_t edit_number(const char *title, const char *note, const char *error,
                                 const char *mask, bool require_full, char *digits) {
    int slots = 0;
    for (const char *p = mask; *p; p++)
        slots += *p == '#';
    int len = 0;
    digits[0] = 0;

    while (true) {
        display_clear();
        draw_title(title);
        display_draw_string(&font_8x12, note, 2, 20, 0x00);
        if (error != NULL)
            display_draw_string(&font_8x12, error, 2, 76, 0x00);

        /* The entry line, big: typed digits, '_' for the slots still empty. */
        char line[24];
        int n = 0, d = 0;
        for (const char *p = mask; *p && n < (int) sizeof(line) - 1; p++) {
            if (*p == '#') {
                line[n++] = d < len ? digits[d] : '_';
                d++;
            } else {
                line[n++] = *p;
            }
        }
        line[n] = 0;
        int width = n * font_10x16.advance;
        display_draw_string(&font_10x16, line, (uint8_t) ((DISPLAY_WIDTH - width) / 2), 44, 0x00);

        draw_hint("Enter:OK Bsp:Del Exit:Cancel");
        display_flush();

        int key = wait_key();
        if (key == K_POWEROFF || key == K_SLEEP)
            return EDIT_POWER_OFF;
        if (key == K_EXIT || (key == K_BSP && len == 0))
            return EDIT_CANCEL;
        if (key == K_BSP) {
            digits[--len] = 0;
            error = NULL;
        } else if (key == K_ENTER) {
            if (len == slots || (!require_full && len > 0))
                return EDIT_OK;
            error_beep();
        } else if (digit_of(key) >= 0) {
            if (len < slots) {
                digits[len++] = (char) ('0' + digit_of(key));
                digits[len] = 0;
                error = NULL;
            }
        }
    }
}

static int parse_digits(const char *s, int count) {
    int v = 0;
    for (int i = 0; i < count; i++)
        v = v * 10 + (s[i] - '0');
    return v;
}

/* ===================== name entry ===================== */

/* Typing a state name: each key types its ALPHA-mode letter (the same
 * layout as the calculator's own ALPHA mode); SHIFT then a digit key types
 * the digit. Only A-Z, 0-9 and '-' -- characters that are safe in a file
 * name on every computer. name holds the initial text and receives the
 * result. */
static edit_result_t edit_name(const char *title, char *name) {
    int len = (int) strlen(name);
    while (true) {
        display_clear();
        draw_title(title);
        draw_line(0, "Name (letters as in ALPHA mode,");
        draw_line(1, "SHIFT+digit key for digits):");

        char line[STATEFS_NAME_MAX + 2];
        snprintf(line, sizeof(line), "%s%s", name, len < STATEFS_NAME_MAX ? "_" : "");
        display_draw_string(&font_10x16, line, 4, 50, 0x00);

        draw_hint("Enter:OK Bsp:Del Exit:Cancel");
        display_flush();

        int key = wait_key();
        if (key == K_POWEROFF || key == K_SLEEP)
            return EDIT_POWER_OFF;
        if (key == K_EXIT || (key == K_BSP && len == 0))
            return EDIT_CANCEL;
        if (key == K_BSP) {
            name[--len] = 0;
            continue;
        }
        if (key == K_ENTER) {
            if (len > 0)
                return EDIT_OK;
            error_beep();
            continue;
        }
        if (key <= 0)
            continue;

        char c = 0;
        if (last_shifted) {
            if (digit_of(key) >= 0)
                c = (char) ('0' + digit_of(key));
        } else {
            c = free42_alpha_char(last_row, last_col);
            if (!((c >= 'A' && c <= 'Z') || c == '-'))
                c = 0;
        }
        if (c == 0 || len >= STATEFS_NAME_MAX) {
            error_beep();
            continue;
        }
        name[len++] = c;
        name[len] = 0;
    }
}

/* ===================== the items ===================== */

/* ===================== Date & Time ===================== */

/* A time of day, typed in: HH:MM, or HH:MM:SS with seconds. 12-hour: the
 * hour 1-12, then AM or PM. On EDIT_OK, *hour is 0-23. */
static edit_result_t enter_time(const char *title, const char *hint_end, bool seconds, int *hour, int *min,
                                int *sec) {
    const char *error = NULL;
    while (true) {
        rtc_datetime_t now;
        rtc_get_datetime(&now);
        char current[16], note[40];
        if (!seconds)
            rtc_format_time(current, sizeof(current), &now, false);
        else if (px_settings.clock_24h)
            snprintf(current, sizeof(current), "%02d:%02d:%02d", now.hour, now.min, now.sec);
        else
            snprintf(current, sizeof(current), "%d:%02d:%02d %s", now.hour % 12 ? now.hour % 12 : 12, now.min,
                     now.sec, now.hour >= 12 ? "PM" : "AM");
        snprintf(note, sizeof(note), "Now %s. %s", current, hint_end);

        char digits[8];
        edit_result_t r = edit_number(title, note, error, seconds ? "##:##:##" : "##:##", true, digits);
        if (r != EDIT_OK)
            return r;
        int h = parse_digits(digits, 2), m = parse_digits(digits + 2, 2);
        int sc = seconds ? parse_digits(digits + 4, 2) : 0;
        if (m < 60 && sc < 60 && px_settings.clock_24h && h < 24) {
            *hour = h, *min = m, *sec = sc;
            return EDIT_OK;
        }
        if (m < 60 && sc < 60 && !px_settings.clock_24h && h >= 1 && h <= 12) {
            static const char *const ampm[] = {"AM", "PM"};
            int selected = now.hour >= 12;
            int pick = run_list(title, ampm, 2, NULL, &selected, "Enter:Select  Exit:Back");
            if (pick == LIST_POWER_OFF)
                return EDIT_POWER_OFF;
            if (pick == LIST_BACK)
                continue;
            *hour = h % 12 + (pick ? 12 : 0), *min = m, *sec = sc;
            return EDIT_OK;
        }
        error_beep();
        error = "Invalid time";
    }
}

/* To the minute: the seconds start at 0 on ENTER. */
static edit_result_t do_set_time(void) {
    int h, m, s;
    edit_result_t r = enter_time("Set Time", "Enter at :00", false, &h, &m, &s);
    if (r == EDIT_OK)
        rtc_set_hms(h, m, s);
    return r;
}

/* Entered in the Date Format's order (DOW-MON-DAY, with no year, as
 * YYYY-MM-DD). */
static edit_result_t do_set_date(void) {
    static const char *const masks[] = {"####-##-##", "##.##.####", "##/##/####", "####-##-##"};
    const char *error = NULL;
    while (true) {
        rtc_datetime_t now;
        rtc_get_datetime(&now);
        char current[12], note[32];
        rtc_format_date(current, sizeof(current), &now);
        snprintf(note, sizeof(note), "Now %s", current);

        char digits[12];
        uint8_t f = px_settings.date_format;
        edit_result_t r = edit_number("Set Date", note, error, masks[f], true, digits);
        if (r != EDIT_OK)
            return r;
        int y, m, d;
        if (f == RTC_YMD || f == RTC_WMD) {
            y = parse_digits(digits, 4), m = parse_digits(digits + 4, 2), d = parse_digits(digits + 6, 2);
        } else {
            int a = parse_digits(digits, 2), b = parse_digits(digits + 2, 2);
            y = parse_digits(digits + 4, 4);
            d = f == RTC_DMY ? a : b;
            m = f == RTC_DMY ? b : a;
        }
        if (rtc_set_date(y, m, d))
            return EDIT_OK;
        error_beep();
        error = "Invalid date";
    }
}

/* Indexed by rtc_date_format_t. */
static const char *const date_format_names[] = {"YYYY-MM-DD", "DD.MM.YYYY", "MM/DD/YYYY", "DOW-MON-DAY"};
static const char *const time_format_names[] = {"24-Hour", "12-Hour"};

/* Both are Free42's formats too (YMD/DMY/MDY, CLK24/CLK12). */
static void set_formats(uint8_t date_format, bool clock_24h) {
    px_settings.date_format = date_format;
    px_settings.clock_24h = clock_24h;
    free42_set_formats(date_format, clock_24h);
    settings_save();
}

static edit_result_t do_date_format(void) {
    int selected = px_settings.date_format;
    int pick = run_list("Date Format", date_format_names, 4, NULL, &selected, "Enter:Select  Exit:Back");
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick >= 0)
        set_formats((uint8_t) pick, px_settings.clock_24h);
    return EDIT_OK;
}

static edit_result_t do_time_format(void) {
    int selected = px_settings.clock_24h ? 0 : 1;
    int pick = run_list("Time Format", time_format_names, 2, NULL, &selected, "Enter:Select  Exit:Back");
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick >= 0)
        set_formats(px_settings.date_format, pick == 0);
    return EDIT_OK;
}

/* Tenths of a second per day, as "+1.5 s/day". */
static void format_tenths(char *buf, size_t size, int tenths) {
    int a = tenths < 0 ? -tenths : tenths;
    snprintf(buf, size, "%c%d.%d s/day", tenths < 0 ? '-' : '+', a / 10, a % 10);
}

enum { CAL_CORRECTION, CAL_MEASURED, CAL_COUNT };
static const char *const cal_items[CAL_COUNT] = {"Correction", "Measured"};

static void cal_value(int item, char *buf, size_t size) {
    switch (item) {
        case CAL_CORRECTION:
            format_tenths(buf, size, px_settings.time_trim);
            break;
        case CAL_MEASURED:
            if (px_settings.cal_span_h == 0)
                snprintf(buf, size, "None Yet");
            else
                snprintf(buf, size, "%s %d.%d s/day", px_settings.cal_drift >= 0 ? "gains" : "loses",
                         abs(px_settings.cal_drift) / 10, abs(px_settings.cal_drift) % 10);
            break;
    }
}

static void set_trim(int tenths) {
    if (tenths > 9999)
        tenths = 9999;
    if (tenths < -9999)
        tenths = -9999;
    px_settings.time_trim = (int16_t) tenths;
    rtc_apply_trim();
    settings_save();
}

/* By hand: whether the clock runs slow or fast, then by how much. */
static edit_result_t edit_correction(void) {
    static const char *const ways[] = {"Clock Runs Slow", "Clock Runs Fast", "No Correction"};
    int selected = px_settings.time_trim < 0 ? 1 : px_settings.time_trim > 0 ? 0 : 2;
    int pick = run_list("Correction", ways, 3, NULL, &selected, "Enter:Select  Exit:Back");
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick < 0)
        return EDIT_CANCEL;
    if (pick == 2) {
        set_trim(0);
        return EDIT_OK;
    }
    char digits[8];
    edit_result_t r = edit_number("Correction", pick == 0 ? "Seconds a day it loses" : "Seconds a day it gains",
                                   NULL, "##.#", true, digits);
    if (r != EDIT_OK)
        return r;
    int tenths = parse_digits(digits, 3);
    set_trim(pick == 0 ? tenths : -tenths);
    return EDIT_OK;
}

/* "It gained 3.2 s/day over 2.1 days." */
static void measured_lines(char *line1, char *line2, size_t size) {
    snprintf(line1, size, "It %s %d.%d s/day over", px_settings.cal_drift >= 0 ? "gained" : "lost",
             abs(px_settings.cal_drift) / 10, abs(px_settings.cal_drift) % 10);
    snprintf(line2, size, "%u.%u days.", px_settings.cal_span_h / 24, px_settings.cal_span_h % 24 * 10 / 24);
}

/* The measured drift becomes part of the correction. */
static edit_result_t apply_measured(void) {
    char line1[32], line2[32], line3[32];
    measured_lines(line1, line2, sizeof(line1));
    set_trim(px_settings.time_trim - px_settings.cal_drift);
    format_tenths(line3, sizeof(line3), px_settings.time_trim);
    px_settings.cal_span_h = 0; /* used */
    settings_save();
    const char *lines[] = {"Applied.", line1, line2, "", "Correction now:", line3};
    return show_message("Time Calibration", lines, 6, "Exit:Back");
}

/* Auto calibration: the exact time, to the second. The clock is set to it,
 * and the drift since the last exact setting measured (6 hours or more
 * apart) -- offered to apply. */
static edit_result_t enter_exact_time(void) {
    int h, m, s;
    edit_result_t r = enter_time("Exact Time", "Enter on it", true, &h, &m, &s);
    if (r != EDIT_OK)
        return r;
    rtc_set_hms(h, m, s);
    if (!rtc_set_measured()) {
        const char *lines[] = {"Clock set: the reference.", "", "Enter the exact time again",
                               "after a day or more (6 hours", "at least) to measure the", "drift."};
        return show_message("Time Calibration", lines, 6, "Exit:Back");
    }
    char line1[32], line2[32];
    measured_lines(line1, line2, sizeof(line1));
    const char *lines[] = {"Clock set. Since the last", "exact time:", line1, line2, "",
                           "Apply it as the correction?"};
    r = show_message("Time Calibration", lines, 6, "Enter:Apply  Exit:Keep");
    return r == EDIT_OK ? apply_measured() : r;
}

/* Measured: apply what's measured, or enter the exact time (to measure
 * it; that also offers to apply). */
static edit_result_t do_measured(void) {
    if (px_settings.cal_span_h == 0)
        return enter_exact_time();
    char apply[32];
    snprintf(apply, sizeof(apply), "Apply (%s %d.%d s/day)", px_settings.cal_drift >= 0 ? "gains" : "loses",
             abs(px_settings.cal_drift) / 10, abs(px_settings.cal_drift) % 10);
    const char *choices[] = {apply, "Enter Exact Time"};
    int selected = 0;
    int pick = run_list("Measured", choices, 2, NULL, &selected, "Enter:Select  Exit:Back");
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick == 0)
        return apply_measured();
    if (pick == 1)
        return enter_exact_time();
    return EDIT_CANCEL;
}

/* Correction: the clock's rate, + speeding it up -- by hand. Measured:
 * from the exact time entered twice (or set from a computer), at least 6
 * hours apart (rtc.h). */
static edit_result_t do_time_calibration(void) {
    static int selected = 0;
    while (true) {
        int pick = run_list("Time Calibration", cal_items, CAL_COUNT, cal_value, &selected,
                            "Enter:Select  Exit:Back");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;
        edit_result_t r = pick == CAL_CORRECTION ? edit_correction() : do_measured();
        if (r == EDIT_POWER_OFF)
            return r;
    }
}

enum { DT_TIME, DT_TIME_FORMAT, DT_DATE, DT_DATE_FORMAT, DT_CALIBRATION, DT_COUNT };
static const char *const dt_items[DT_COUNT] = {"Set Time", "Time Format", "Set Date", "Date Format",
                                               "Time Calibration"};

static void dt_value(int item, char *buf, size_t size) {
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    switch (item) {
        case DT_TIME:
            rtc_format_time(buf, size, &now, false);
            break;
        case DT_DATE:
            rtc_format_date(buf, size, &now);
            break;
        case DT_DATE_FORMAT:
            snprintf(buf, size, "%s", date_format_names[px_settings.date_format]);
            break;
        case DT_TIME_FORMAT:
            snprintf(buf, size, "%s", time_format_names[px_settings.clock_24h ? 0 : 1]);
            break;
        case DT_CALIBRATION:
            format_tenths(buf, size, px_settings.time_trim);
            break;
    }
}

static edit_result_t do_date_time(void) {
    static int selected = 0;
    while (true) {
        int pick = run_list("Date & Time", dt_items, DT_COUNT, dt_value, &selected, "Enter:Select  Exit:Back");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;
        edit_result_t r = EDIT_OK;
        switch (pick) {
            case DT_TIME:
                r = do_set_time();
                break;
            case DT_DATE:
                r = do_set_date();
                break;
            case DT_DATE_FORMAT:
                r = do_date_format();
                break;
            case DT_TIME_FORMAT:
                r = do_time_format();
                break;
            case DT_CALIBRATION:
                r = do_time_calibration();
                break;
        }
        if (r == EDIT_POWER_OFF)
            return r;
    }
}

/* Minutes, 0-999, 0 = off. Saved to flash on OK. */
static edit_result_t do_minutes(const char *title, const char *what, uint16_t *value) {
    char current[16], note[40];
    format_minutes(current, sizeof(current), *value);
    snprintf(note, sizeof(note), "%s: %s. 0 = Off", what, current);

    char digits[8];
    edit_result_t r = edit_number(title, note, NULL, "### min", false, digits);
    if (r != EDIT_OK)
        return r;
    *value = (uint16_t) parse_digits(digits, (int) strlen(digits));
    settings_save();
    return EDIT_OK;
}

static edit_result_t do_info(void) {
    while (true) {
        display_clear();
        draw_title("Info");

        char buf[40];
        int y = 18;
        snprintf(buf, sizeof(buf), "Plus42    %s, Thomas Okken", PLUS42_VERSION);
        display_draw_string(&font_8x12, buf, 2, (uint8_t) y, 0x00);
        y += 13;
        display_draw_string(&font_8x12, "Port      Alex Garza", 2, (uint8_t) y, 0x00);
        y += 13;
        snprintf(buf, sizeof(buf), "Decimal   Intel %s", INTEL_LIB_VERSION);
        display_draw_string(&font_8x12, buf, 2, (uint8_t) y, 0x00);
        y += 13;
        snprintf(buf, sizeof(buf), "Firmware  %s, %s", PICO_PROGRAM_VERSION_STRING, __DATE__);
        display_draw_string(&font_8x12, buf, 2, (uint8_t) y, 0x00);
        y += 13;
        int mv = battery_read_mv();
        snprintf(buf, sizeof(buf), "Battery   %d.%02d V", mv / 1000, mv % 1000 / 10);
        display_draw_string(&font_8x12, buf, 2, (uint8_t) y, 0x00);
        y += 13;
        snprintf(buf, sizeof(buf), "Stack     %lu / %u bytes", (unsigned long) crash_core1_stack_peak(), 32u * 1024u);
        display_draw_string(&font_8x12, buf, 2, (uint8_t) y, 0x00);

        draw_hint("Exit:Back");
        display_flush();

        int key = wait_key();
        if (key == K_POWEROFF || key == K_SLEEP)
            return EDIT_POWER_OFF;
        if (is_back(key) || key == K_ENTER)
            return EDIT_OK;
    }
}

/* ===================== Splash Image ===================== */

#define MAX_SPLASHES 32

/* The image full screen as it will look while off, with a hint bar over
 * the bottom; ENTER = use it. */
static edit_result_t preview_splash(void) {
    while (true) {
        splash_preview_draw();
        display_fill_rect(0, HINT_Y - 2, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1, true);
        display_draw_string(&font_8x12, "Enter:Use  Exit:Back", 2, HINT_Y, 0xFF);
        display_flush();

        int key = wait_key();
        if (key == K_POWEROFF || key == K_SLEEP)
            return EDIT_POWER_OFF;
        if (key == K_ENTER)
            return EDIT_OK;
        if (is_back(key))
            return EDIT_CANCEL;
    }
}

static edit_result_t do_splash_image(void) {
    static char names[MAX_SPLASHES][SPLASH_NAME_MAX + 1];
    int count = statefs_list_files(STATEFS_SPLASH_DIR, ".png", (char *) names, SPLASH_NAME_MAX + 1, MAX_SPLASHES);

    const char *items[MAX_SPLASHES + 1];
    items[0] = "Built-In";
    int selected = 0;
    for (int i = 0; i < count; i++) {
        items[i + 1] = names[i];
        if (strcmp(names[i], px_settings.splash_image) == 0)
            selected = i + 1;
    }

    while (true) {
        int pick = run_list("Splash Image", items, count + 1, NULL, &selected, "Enter:Preview  Exit:Back");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;

        const char *name = pick == 0 ? NULL : names[pick - 1];
        char error[48];
        show_busy("Splash Image", "Loading...");
        if (!splash_preview_load(name, error, sizeof(error))) {
            error_beep();
            char line[40];
            snprintf(line, sizeof(line), "Can't use %.24s:", name);
            const char *const lines[] = {line, error, "", "Use a non-interlaced PNG;",
                                         "best at 250 x 122 pixels."};
            if (show_message("Splash Image", lines, 5, "Exit:Back") == EDIT_POWER_OFF)
                return EDIT_POWER_OFF;
            continue;
        }

        edit_result_t r = preview_splash();
        if (r == EDIT_POWER_OFF)
            return r;
        if (r == EDIT_OK) {
            snprintf(px_settings.splash_image, sizeof(px_settings.splash_image), "%s", name != NULL ? name : "");
            splash_preview_use();
            settings_save();
            return EDIT_OK;
        }
    }
}

/* ===================== Printer ===================== */

/* Indexed by printer_target_t. */
static const char *const printer_names[] = {
        "Off",
        "USB Serial",
};
#define PRINTER_CHOICES ((int) (sizeof(printer_names) / sizeof(printer_names[0])))

static edit_result_t do_printer(void) {
    int selected = px_settings.printer < PRINTER_CHOICES ? px_settings.printer : 0;
    int pick = run_list("Printer", printer_names, PRINTER_CHOICES, NULL, &selected, "Enter:Select  Exit:Back");
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick == LIST_BACK)
        return EDIT_CANCEL;

    px_settings.printer = (uint8_t) pick;
    settings_save();
    /* Choosing a printer also turns Free42's printer on (PRON, flag 55),
     * Off turns it off -- so the print commands just work. */
    free42_set_printer(pick != PRINTER_OFF);

    if (pick == PRINTER_USB_SERIAL) {
        static const char *const lines[] = {"Printing to the USB serial", "port \"PX-42F Printer\".", "",
                                            "Open it in a terminal on the", "computer to see the output."};
        edit_result_t r = show_message("Printer", lines, 5, "Exit:Back");
        if (r == EDIT_POWER_OFF)
            return r;
    }
    return EDIT_OK;
}

/* ===================== Key Sound ===================== */

/* "Off", then the sounds (sounds_key_list()): built-ins and /SOUNDS/KEY. */
#define KEY_SOUND_CHOICES 40
static char key_sound_names[KEY_SOUND_CHOICES][SOUND_NAME_MAX + 1];
static const char *key_sound_rows[KEY_SOUND_CHOICES];

static const char *key_sound_shown(void) {
    return px_settings.key_sound[0] ? px_settings.key_sound : "Off";
}

static void key_sound_mark(int item, char *buf, size_t size) {
    snprintf(buf, size, "%s", strcasecmp(key_sound_rows[item], key_sound_shown()) == 0 ? "<" : "");
}

/* ENTER picks a sound and plays it, staying in the list, so they can be
 * tried one after another; EXIT goes back. The current one is marked.
 * (ENTER itself makes the current key sound as it goes down, so a newly
 * picked sound plays right after it -- and re-picking the same one adds
 * nothing.) A file that can't be used says why, and the choice stays. */
static edit_result_t do_key_sound(void) {
    strcpy(key_sound_names[0], "Off");
    int count = 1 + sounds_key_list(&key_sound_names[1], KEY_SOUND_CHOICES - 1);
    int selected = 0;
    for (int i = 0; i < count; i++) {
        key_sound_rows[i] = key_sound_names[i];
        if (strcasecmp(key_sound_names[i], key_sound_shown()) == 0)
            selected = i;
    }
    while (true) {
        int pick = run_list("Key Sound", key_sound_rows, count, key_sound_mark, &selected,
                            "Enter:Choose+Play  Exit:Back");
        if (pick == LIST_POWER_OFF || pick == LIST_BACK) {
            settings_save(); /* once, on the way out: no flash write per try */
            return pick == LIST_POWER_OFF ? EDIT_POWER_OFF : EDIT_OK;
        }
        const char *name = pick == 0 ? "" : key_sound_names[pick];
        if (strcasecmp(name, px_settings.key_sound) == 0)
            continue;
        char error[40];
        if (!sounds_key_select(name, error, sizeof(error))) {
            const char *lines[] = {key_sound_names[pick], "can't be used:", error, "",
                                   "Key sounds: WAV files in", "/SOUNDS/KEY on the drive."};
            sounds_key_select(px_settings.key_sound, error, sizeof(error)); /* back to the current one */
            if (show_message("Key Sound", lines, 6, "Exit:Back") == EDIT_POWER_OFF)
                return EDIT_POWER_OFF;
            continue;
        }
        snprintf(px_settings.key_sound, sizeof(px_settings.key_sound), "%s", name);
        /* The whole set: typing, an operation, ENTER. */
        for (int kind = 0; kind < KEY_SOUND_KINDS; kind++) {
            if (kind > 0)
                for (int i = 0; i < 15; i++) { /* 150ms apart */
                    sleep_ms(10);
                    watchdog_update();
                }
            sounds_key_play((key_sound_t) kind);
        }
    }
}

/* ===================== Sounds ===================== */

enum {
    SOUND_KEY,
    SOUND_BOOT,
    SOUND_ON,
    SOUND_OFF,
    SOUND_COUNT
};

static const char *const sound_items[SOUND_COUNT] = {
        "Key Sound",
        "Boot Sound",
        "On Sound",
        "Off Sound",
};

static void sound_value(int item, char *buf, size_t size) {
    switch (item) {
        case SOUND_KEY:
            if (strlen(key_sound_shown()) <= 15)
                snprintf(buf, size, "%s", key_sound_shown());
            else
                snprintf(buf, size, "%.13s..", key_sound_shown());
            break;
        case SOUND_BOOT:
            snprintf(buf, size, "%s", px_settings.boot_sound ? "On" : "Off");
            break;
        case SOUND_ON:
            snprintf(buf, size, "%s", px_settings.on_sound ? "On" : "Off");
            break;
        case SOUND_OFF:
            snprintf(buf, size, "%s", px_settings.off_sound ? "On" : "Off");
            break;
    }
}

/* All the sound settings. Boot, On and Off Sound toggle with ENTER, playing
 * the sound when turned on; the files are in /SOUNDS on the drive. */
static edit_result_t do_sounds(void) {
    static int selected = 0;
    while (true) {
        int pick = run_list("Sounds", sound_items, SOUND_COUNT, sound_value, &selected, "Enter:Select  Exit:Back");
        if (pick == LIST_POWER_OFF || pick == LIST_BACK) {
            settings_save(); /* once, on the way out */
            return pick == LIST_POWER_OFF ? EDIT_POWER_OFF : EDIT_CANCEL;
        }
        switch (pick) {
            case SOUND_KEY:
                if (do_key_sound() == EDIT_POWER_OFF)
                    return EDIT_POWER_OFF;
                break;
            case SOUND_BOOT:
                px_settings.boot_sound = !px_settings.boot_sound;
                if (px_settings.boot_sound)
                    sounds_play_boot();
                break;
            case SOUND_ON:
                px_settings.on_sound = !px_settings.on_sound;
                if (px_settings.on_sound)
                    sounds_play_on();
                break;
            case SOUND_OFF:
                px_settings.off_sound = !px_settings.off_sound;
                if (px_settings.off_sound)
                    sounds_play_off();
                break;
        }
    }
}

/* ===================== States, USB Drive ===================== */

static edit_result_t no_disk(void) {
    static const char *const lines[] = {"The drive is not available."};
    edit_result_t r = show_message("Drive", lines, 1, "Exit:Back");
    return r == EDIT_POWER_OFF ? r : EDIT_CANCEL;
}

static edit_result_t do_save_state(void) {
    if (!statefs_ready())
        return no_disk();

    /* Default name: the current date and time, YYYYMMDD-HHMMSS -- unique,
     * and sorts oldest-first in the lists. BSP to type another. */
    char name[STATEFS_NAME_MAX + 1];
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    rtc_format_stamp(name, sizeof(name), &now);
    edit_result_t r = edit_name("Save State", name);
    if (r != EDIT_OK)
        return r;

    if (statefs_state_exists(name)) {
        char line[40];
        snprintf(line, sizeof(line), "%s already exists.", name);
        const char *const lines[] = {line, "Replace it?"};
        r = show_message("Save State", lines, 2, "Enter:Replace  Exit:Cancel");
        if (r != EDIT_OK)
            return r;
    }

    show_busy("Save State", "Saving...");
    debug_checkpoint(CKPT_STATE_SAVE, 0);
    uint32_t errors_before = flashdisk_error_count();
    char path[48];
    statefs_state_path(name, path, sizeof(path));
    free42_save_state(path);
    statefs_sync();
    debug_checkpoint(CKPT_MENU, 1);

    char line[40];
    bool ok = statefs_state_exists(name) && flashdisk_error_count() == errors_before;
    snprintf(line, sizeof(line), ok ? "Saved %s" : "Could not save %s", name);
    if (!ok)
        error_beep();
    const char *const lines[] = {line};
    r = show_message("Save State", lines, 1, "Exit:Back");
    return r == EDIT_POWER_OFF ? r : EDIT_OK;
}

/* Lets the user pick one of the saved states; its name goes to name[].
 * EDIT_OK once one is picked; EDIT_CANCEL for EXIT, or when there are none
 * (after saying so). */
static edit_result_t pick_state(const char *title, const char *hint, char *name) {
    if (!statefs_ready())
        return no_disk();

    static char names[MAX_STATES][STATEFS_NAME_MAX + 1];
    int count = statefs_list_states(names, MAX_STATES);
    if (count == 0) {
        static const char *const lines[] = {"No saved states.", "", "Save one first, or copy .p42",
                                            "files into the STATE folder", "with USB Drive."};
        edit_result_t r = show_message(title, lines, 5, "Exit:Back");
        return r == EDIT_POWER_OFF ? r : EDIT_CANCEL;
    }

    const char *items[MAX_STATES];
    for (int i = 0; i < count; i++)
        items[i] = names[i];
    int selected = 0;
    int pick = run_list(title, items, count, NULL, &selected, hint);
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick == LIST_BACK)
        return EDIT_CANCEL;
    strcpy(name, names[pick]);
    return EDIT_OK;
}

/* A one-line result ("Deleted X" / "Could not ..."), with the error beep
 * on failure; then back. */
static edit_result_t show_result(const char *title, bool ok, const char *line) {
    if (!ok)
        error_beep();
    const char *const lines[] = {line};
    edit_result_t r = show_message(title, lines, 1, "Exit:Back");
    return r == EDIT_POWER_OFF ? r : EDIT_OK;
}

/* Returns EDIT_OK only once a state has actually been loaded. */
static edit_result_t do_load_state(void) {
    char name[STATEFS_NAME_MAX + 1];
    edit_result_t r = pick_state("Load State", "Enter:Load  Exit:Back", name);
    if (r != EDIT_OK)
        return r;

    char line[40];
    snprintf(line, sizeof(line), "Load %s?", name);
    const char *const lines[] = {line, "", "This replaces everything in", "the calculator's memory."};
    r = show_message("Load State", lines, 4, "Enter:Load  Exit:Cancel");
    if (r != EDIT_OK)
        return r;

    show_busy("Load State", "Loading...");
    debug_checkpoint(CKPT_STATE_LOAD, 0);
    char path[48];
    statefs_state_path(name, path, sizeof(path));
    free42_load_state(path);
    statefs_sync();
    return EDIT_OK;
}

static edit_result_t do_rename_state(void) {
    char old_name[STATEFS_NAME_MAX + 1];
    edit_result_t r = pick_state("Rename State", "Enter:Rename  Exit:Back", old_name);
    if (r != EDIT_OK)
        return r;

    char new_name[STATEFS_NAME_MAX + 1];
    strcpy(new_name, old_name);
    r = edit_name("Rename State", new_name);
    if (r != EDIT_OK || strcmp(new_name, old_name) == 0)
        return r;

    char line[40];
    if (statefs_state_exists(new_name)) {
        snprintf(line, sizeof(line), "%s already exists", new_name);
        return show_result("Rename State", false, line);
    }
    bool ok = statefs_rename_state(old_name, new_name);
    snprintf(line, sizeof(line), ok ? "Renamed to %s" : "Could not rename %s", ok ? new_name : old_name);
    return show_result("Rename State", ok, line);
}

static edit_result_t do_delete_state(void) {
    char name[STATEFS_NAME_MAX + 1];
    edit_result_t r = pick_state("Delete State", "Enter:Delete  Exit:Back", name);
    if (r != EDIT_OK)
        return r;

    char line[40];
    snprintf(line, sizeof(line), "Delete %s?", name);
    const char *const lines[] = {line, "", "This cannot be undone."};
    r = show_message("Delete State", lines, 3, "Enter:Delete  Exit:Cancel");
    if (r != EDIT_OK)
        return r;

    bool ok = statefs_delete_state(name);
    snprintf(line, sizeof(line), ok ? "Deleted %s" : "Could not delete %s", name);
    return show_result("Delete State", ok, line);
}

/* USB Drive -- like the DM42's USB disk mode: the computer gets the drive,
 * the calculator stays out of it, until the drive is ejected or EXIT. */
static edit_result_t do_usb_disk(void) {
    if (!statefs_ready() && flashdisk_block_count() == 0)
        return no_disk();

    debug_checkpoint(CKPT_USB_DISK, 0);
    statefs_usb_begin();
    edit_result_t result = EDIT_OK;
    bool redraw = true;
    bool was_mounted = !tud_mounted();

    while (true) {
        statefs_usb_poll();

        bool host = tud_mounted();
        if (redraw || host != was_mounted) {
            was_mounted = host;
            redraw = false;
            display_clear();
            draw_title("USB Drive");
            draw_line(0, host ? "Connected: drive \"PX-42F\"" : "Waiting for a computer...");
            draw_line(2, "States: STATE/<name>.p42");
            draw_line(3, "Splash images: SPLASH/*.png");
            draw_line(5, "Eject the drive when done.");
            draw_hint("Exit:Back (Eject First!)");
            display_flush();
        }

        if (usb_msc_ejected())
            break;

        int key = wait_key_timeout(10);
        if (key == K_POWEROFF) {
            result = EDIT_POWER_OFF;
            break;
        }
        if (is_back(key))
            break;
        if (key == K_TICK || key == K_SHIFT)
            redraw = true;
        /* The sleep timer is paused while the computer has the drive. */
        idle_since = get_absolute_time();
    }

    show_busy("USB Drive", "Writing to flash...");
    statefs_usb_end();
    splash_reload(); /* the chosen splash image may have been replaced */
    sounds_init(true); /* likewise the sounds -- and /SOUNDS made again if it was deleted */
    fonts_init(); /* and the fonts */
    return result;
}

enum {
    STATE_SAVE,
    STATE_LOAD,
    STATE_RENAME,
    STATE_DELETE,
    STATE_COUNT
};

static const char *const state_items[STATE_COUNT] = {
        "Save State",
        "Load State",
        "Rename State",
        "Delete State",
};

/* Returns EDIT_POWER_OFF to power off, EDIT_OK when a state was loaded
 * (back to the calculator, to show it), EDIT_CANCEL to go back. */
static edit_result_t do_states(void) {
    static int selected = 0;
    while (true) {
        int pick = run_list("States", state_items, STATE_COUNT, NULL, &selected, "Enter:Select  Exit:Back");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;

        edit_result_t r = EDIT_CANCEL;
        switch (pick) {
            case STATE_SAVE:
                r = do_save_state();
                break;
            case STATE_LOAD:
                r = do_load_state();
                if (r == EDIT_OK)
                    return EDIT_OK;
                break;
            case STATE_RENAME:
                r = do_rename_state();
                break;
            case STATE_DELETE:
                r = do_delete_state();
                break;
        }
        if (r == EDIT_POWER_OFF)
            return r;
    }
}

/* ===================== Splash ===================== */

enum { SPLASH_TIMER, SPLASH_IMAGE, SPLASH_COUNT };
static const char *const splash_items[SPLASH_COUNT] = {"Timer", "Image"};

static void splash_value(int item, char *buf, size_t size) {
    if (item == SPLASH_TIMER)
        format_minutes(buf, size, px_settings.splash_minutes);
    else if (px_settings.splash_image[0] == 0)
        snprintf(buf, size, "Built-In");
    else if (strlen(px_settings.splash_image) <= 20)
        snprintf(buf, size, "%s", px_settings.splash_image);
    else
        snprintf(buf, size, "%.18s..", px_settings.splash_image);
}

/* The picture shown while off: for how long, and which. */
static edit_result_t do_splash(void) {
    static int selected = 0;
    while (true) {
        int pick = run_list("Splash Image", splash_items, SPLASH_COUNT, splash_value, &selected,
                            "Enter:Select  Exit:Back");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;
        edit_result_t r = pick == SPLASH_TIMER
                                  ? do_minutes("Splash Timer", "Shown for", &px_settings.splash_minutes)
                                  : do_splash_image();
        if (r == EDIT_POWER_OFF)
            return r;
    }
}

/* ===================== Fonts ===================== */

#define MAX_FONTS 32

enum { FONT_ITEM_LARGE, FONT_ITEM_SMALL, FONT_ITEM_MINI, FONT_ITEM_COUNT };
static const char *const font_items[FONT_ITEM_COUNT] = {"Large Font (10x16)", "Small Font (8x12)",
                                                        "Mini Font (6x8)"};
static const font_kind_t font_item_kind[FONT_ITEM_COUNT] = {FONT_LARGE, FONT_SMALL, FONT_MINI};

static char *font_setting(font_kind_t kind) {
    return kind == FONT_SMALL ? px_settings.small_font : kind == FONT_LARGE ? px_settings.large_font
                                                                             : px_settings.mini_font;
}

static display_font_t *font_of(font_kind_t kind) {
    return kind == FONT_SMALL ? &font_small : kind == FONT_LARGE ? &font_large : &font_mini;
}

static void font_value(int item, char *buf, size_t size) {
    /* 11 characters: what's left of the row beside "Large Font (10x16)". */
    const char *name = font_setting(font_item_kind[item]);
    if (name[0] == 0)
        snprintf(buf, size, "Built-In");
    else if (strlen(name) <= 11)
        snprintf(buf, size, "%s", name);
    else
        snprintf(buf, size, "%.9s..", name);
}

/* Every character of the font, as the calculator will draw it, with a hint
 * bar (in the menu's own font) over the bottom; ENTER = use it. */
static edit_result_t preview_font(const display_font_t *font) {
    int per_row = (DISPLAY_WIDTH - 2) / font->advance;
    while (true) {
        display_clear();
        for (int code = 0; code < 128; code += per_row) {
            char row[64]; /* the mini font: 41 a row */
            int n = 0;
            while (n < per_row && n < (int) sizeof(row) && code + n < 128) {
                row[n] = (char) (code + n);
                n++;
            }
            display_draw_chars(font, row, n, 1, (uint8_t) (code / per_row * (font->height + 1)), 0x00);
        }
        display_fill_rect(0, HINT_Y - 2, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1, true);
        display_draw_string(&font_8x12, "Enter:Use  Exit:Back", 2, HINT_Y, 0xFF);
        display_flush();

        int key = wait_key();
        if (key == K_POWEROFF || key == K_SLEEP)
            return EDIT_POWER_OFF;
        if (key == K_ENTER)
            return EDIT_OK;
        if (is_back(key))
            return EDIT_CANCEL;
    }
}

/* "Built-In", then the sheets in /FONTS/LARGE, SMALL or MINI. ENTER loads
 * one and shows it; it's only kept if accepted there. */
static edit_result_t do_font(font_kind_t kind) {
    static char names[MAX_FONTS][FONT_NAME_MAX + 1];
    const char *title = font_items[kind == FONT_LARGE ? FONT_ITEM_LARGE : kind == FONT_SMALL ? FONT_ITEM_SMALL
                                                                                             : FONT_ITEM_MINI];
    char *setting = font_setting(kind);
    int count = fonts_list(kind, names, MAX_FONTS);

    const char *items[MAX_FONTS + 1];
    items[0] = "Built-In";
    int selected = 0;
    for (int i = 0; i < count; i++) {
        items[i + 1] = names[i];
        if (strcmp(names[i], setting) == 0)
            selected = i + 1;
    }

    while (true) {
        int pick = run_list(title, items, count + 1, NULL, &selected, "Enter:Preview  Exit:Back");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;

        const char *name = pick == 0 ? "" : names[pick - 1];
        char error[40];
        show_busy(title, "Loading...");
        if (!fonts_select(kind, name, error, sizeof(error))) {
            error_beep();
            char line[40];
            snprintf(line, sizeof(line), "Can't use %.20s:", name);
            const char *const small[] = {line, error, "", "A small font is a PNG of",
                                         "128 x 96 pixels: 16 x 8", "glyphs of 8 x 12, no gaps."};
            const char *const large[] = {line, error, "", "A large font is a PNG of",
                                         "175 x 135 pixels: 16 x 8", "glyphs of 10 x 16, 1 apart."};
            const char *const mini[] = {line, error, "", "A mini font is a PNG of",
                                        "96 x 64 pixels: 16 x 8", "glyphs of 6 x 8, no gaps."};
            const char *const *help = kind == FONT_SMALL ? small : kind == FONT_LARGE ? large : mini;
            if (show_message(title, help, 6, "Exit:Back") == EDIT_POWER_OFF)
                return EDIT_POWER_OFF;
            continue;
        }

        edit_result_t r = preview_font(font_of(kind));
        if (r == EDIT_OK) {
            snprintf(setting, FONT_SETTING_MAX + 1, "%s", name);
            settings_save();
            return EDIT_OK;
        }
        /* Not taken: back to the current one (the built-in font, should its
         * file have gone). */
        if (!fonts_select(kind, setting, error, sizeof(error)))
            fonts_select(kind, "", error, sizeof(error));
        if (r == EDIT_POWER_OFF)
            return r;
    }
}

/* The calculator screen's fonts (fonts.h); this menu keeps its own. */
static edit_result_t do_fonts(void) {
    static int selected = 0;
    while (true) {
        int pick = run_list("Fonts", font_items, FONT_ITEM_COUNT, font_value, &selected, "Enter:Select  Exit:Back");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;
        if (do_font(font_item_kind[pick]) == EDIT_POWER_OFF)
            return EDIT_POWER_OFF;
    }
}

/* ===================== IR Remote ===================== */

#define MAX_REMOTES     32
#define REMOTE_NAME_MAX 28 /* file name without ".ir" */

/* The remotes in /IR: one is read, and the calculator becomes that remote
 * (remote.h) until ON. EDIT_OK when it's left that way -- back to the
 * calculator, not to this menu. */
static edit_result_t do_ir_remote(void) {
    static char names[MAX_REMOTES][REMOTE_NAME_MAX + 1];
    static ir_remote_t remote;
    int count = statefs_list_files(IR_DIR, ".ir", (char *) names, REMOTE_NAME_MAX + 1, MAX_REMOTES);
    if (count == 0) {
        const char *const lines[] = {"No remotes yet.", "", "Copy .ir files to the IR", "folder in USB Drive mode."};
        return show_message("IR Remote", lines, 4, "Exit:Back") == EDIT_POWER_OFF ? EDIT_POWER_OFF : EDIT_CANCEL;
    }
    const char *items[MAX_REMOTES];
    for (int i = 0; i < count; i++)
        items[i] = names[i];

    static int selected = 0;
    while (true) {
        int pick = run_list("IR Remote", items, count, NULL, &selected, "Enter:Use  Exit:Back");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;

        char path[48], error[32] = "Can't open the file";
        int line = 0;
        snprintf(path, sizeof(path), "%s/%s.ir", IR_DIR, names[pick]);
        FILE *f = statefs_fopen(path, "rb");
        bool ok = f != NULL && ir_remote_load(f, &remote, &line, error, sizeof(error));
        if (f != NULL)
            fclose(f);
        if (!ok) {
            error_beep();
            char head[40], where[16];
            snprintf(head, sizeof(head), "Can't use %.20s:", names[pick]);
            snprintf(where, sizeof(where), "Line %d:", line);
            const char *const lines[] = {head, line > 0 ? where : "", error};
            if (show_message("IR Remote", lines, 3, "Exit:Back") == EDIT_POWER_OFF)
                return EDIT_POWER_OFF;
            continue;
        }
        return remote_run(&remote, names[pick]) ? EDIT_POWER_OFF : EDIT_OK;
    }
}

/* ===================== the main list ===================== */

enum {
    ITEM_OFF,
    ITEM_IR_REMOTE,
    ITEM_DATE_TIME,
    ITEM_SOUNDS,
    ITEM_FONTS,
    ITEM_SLEEP,
    ITEM_SPLASH,
    ITEM_PRINTER,
    ITEM_STATES,
    ITEM_USB,
    ITEM_INFO,
    ITEM_COUNT
};

static const char *const item_names[ITEM_COUNT] = {
        "Off",
        "IR Remote",
        "Date & Time",
        "Sounds",
        "Fonts",
        "Sleep Timer",
        "Splash Image",
        "Printer",
        "States",
        "USB Drive",
        "Info",
};

static void item_value(int item, char *buf, size_t size) {
    buf[0] = 0;
    switch (item) {
        case ITEM_DATE_TIME:
            snprintf(buf, size, ">");
            break;
        case ITEM_IR_REMOTE:
        case ITEM_SOUNDS:
        case ITEM_FONTS:
            snprintf(buf, size, ">");
            break;
        case ITEM_SLEEP:
            format_minutes(buf, size, px_settings.sleep_minutes);
            break;
        case ITEM_SPLASH:
        case ITEM_STATES:
        case ITEM_USB:
            snprintf(buf, size, ">");
            break;
        case ITEM_PRINTER:
            snprintf(buf, size, "%s", px_settings.printer < PRINTER_CHOICES ? printer_names[px_settings.printer] : "?");
            break;
    }
}

settings_menu_result_t settings_menu_run(void) {
    int selected = ITEM_OFF;
    shift_on = false;
    idle_since = get_absolute_time();
    debug_checkpoint(CKPT_MENU, 0);

    while (true) {
        int pick = run_list("Settings", item_names, ITEM_COUNT, item_value, &selected, "Enter:Select  Exit:Back");
        if (pick == LIST_POWER_OFF)
            return SETTINGS_MENU_POWER_OFF;
        if (pick == LIST_BACK) {
            /* EXIT is also the OFF key: on "Off" it turns the calculator
             * off, like ENTER. The backspace arrow still just leaves. */
            if (selected == ITEM_OFF && last_key == K_EXIT)
                return SETTINGS_MENU_POWER_OFF;
            return SETTINGS_MENU_EXIT;
        }

        edit_result_t r = EDIT_OK;
        switch (pick) {
            case ITEM_OFF:
                return SETTINGS_MENU_POWER_OFF;
            case ITEM_IR_REMOTE:
                r = do_ir_remote();
                if (r == EDIT_OK)
                    return SETTINGS_MENU_EXIT; /* ON in the remote: back to the calculator */
                break;
            case ITEM_DATE_TIME:
                r = do_date_time();
                break;
            case ITEM_SOUNDS:
                r = do_sounds();
                break;
            case ITEM_FONTS:
                r = do_fonts();
                break;
            case ITEM_SLEEP:
                r = do_minutes("Sleep Timer", "Off after", &px_settings.sleep_minutes);
                break;
            case ITEM_SPLASH:
                r = do_splash();
                break;
            case ITEM_PRINTER:
                r = do_printer();
                break;
            case ITEM_STATES:
                r = do_states();
                if (r == EDIT_OK)
                    return SETTINGS_MENU_EXIT; /* a state was loaded: show it */
                break;
            case ITEM_USB:
                r = do_usb_disk();
                break;
            case ITEM_INFO:
                r = do_info();
                break;
        }
        if (r == EDIT_POWER_OFF)
            return SETTINGS_MENU_POWER_OFF;
    }
}
