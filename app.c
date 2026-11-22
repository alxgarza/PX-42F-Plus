#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/powman.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "appdec.h"
#include "appfile.h"
#include "ramstate.h"
#include "battery.h"
#include "cpuclock.h"
#include "debug_checkpoint.h"
#include "display.h"
#include "display_fb.h"
#include "font_10x16.h"
#include "font_6x8.h"
#include "font_8x12.h"
#include "fonts.h"
#include "free42_app.h"
#include "idle.h"
#include "irtx.h"
#include "safefile.h"
#include "printer.h"
#include "keyboard.h"
#include "keyhold.h"
#include "px_app.h"
#include "px_draw.h"
#include "retained.h"
#include "rtc.h"
#include "settings.h"
#include "sounds.h"
#include "statefs.h"
#include "timemodule.h"
#include "ui.h"
#include "ui_screen.h"
#include "alarms.h"

_Static_assert(PX_APP_BASE >= RETAINED_BASE + 4096 && APP_OLD_BASE == RETAINED_RAMSTATE,
               "apps run above the retained 4 KB, over the RAM that keeps the memory while off");
_Static_assert(PX_SCREEN_W == DISPLAY_WIDTH && PX_SCREEN_H == DISPLAY_HEIGHT && PX_SCREEN_STRIDE == DISPLAY_STRIDE,
               "px_app.h's screen is the display's framebuffer");
_Static_assert(KB_ROWS * KB_COLS <= 64, "keyboard_held()");

/* The app that's running. */
static struct {
    char dir[64];                /* its own folder */
    uint64_t held;               /* the keys down that key() has reported */
    absolute_time_t idle_since;  /* the last key down: the Sleep Timer */
    absolute_time_t battery_check;
    bool quit;                   /* key() has said PX_KEY_QUIT */
    bool on_is_key;              /* px->on_key(1): a short ON press is PX_KEY_ON */
    bool asking;                 /* a dialog is up: ON there is a key (its right button) */
    uint32_t paused_ms;          /* time spent in dialogs: millis() stands still meanwhile */
    uint64_t swallow;            /* keys pressed in a dialog: their letting go isn't the app's */
    bool power_off;              /* ... to turn off */
    uint32_t random;
    FILE *files[PX_FILES_OPEN];
    bool written[PX_FILES_OPEN];
    bool write_failed[PX_FILES_OPEN];
    char paths[PX_FILES_OPEN][PX_PATH_MAX + 70]; /* a file being written: its name; the bytes go to name.NEW */
} run;

/* The keyboard matrix is wired as the keys sit, except XEQ and TAN -- the
 * top rows' sixth keys -- which are on an eighth row. */
static int key_place(int row, int col) {
    return row == 7 ? PX_KEY(col, 5) : PX_KEY(row, col);
}

/* Each call that can take a while ends with this, so a watchdog reset
 * names the app as what was running, not the last wait in the firmware. */
static void back_to_app(void) {
    debug_checkpoint(CKPT_APP, 0);
}

static void quit(bool power_off) {
    run.quit = true;
    run.power_off = power_off;
}

static bool ask_quit(void);
static const char *app_name(void);

/* ON, waited out: held for EXIT_HOLD_OFF_MS it quits and turns off; let
 * go sooner, it asks "Quit NAME?" and quits on QUIT -- or, for an app that
 * asked (px->on_key()) and in a dialog, it's an ordinary key. ON_KEY: it's
 * the key; ON_QUIT; ON_STAY: nothing (STAY in the question). */
enum { ON_KEY, ON_QUIT, ON_STAY };

static int on_pressed(uint8_t row, uint8_t col) {
    key_hold_begin(row, col);
    while (keyboard_is_held(row, col)) {
        watchdog_update();
        if (key_hold_poll() == KEY_HOLD_OFF) {
            quit(true);
            return ON_QUIT;
        }
        sleep_ms(10);
    }
    if (run.on_is_key || run.asking)
        return ON_KEY;
    if (ask_quit())
        return ON_QUIT;
    run.swallow |= 1ull << (row * KB_COLS + col); /* its letting go: the question's */
    return ON_STAY;
}

/* Asleep until `until` or a key going down -- or the Sleep Timer or an
 * empty battery, as in the main loop (not on a computer's USB): those end
 * the app. False if it's to end. */
static bool wait(absolute_time_t until) {
    absolute_time_t sleep_at = at_the_end_of_time;
    if (px_settings.sleep_minutes != 0 && !idle_usb_host_active()) {
        sleep_at = delayed_by_ms(run.idle_since, (uint32_t) px_settings.sleep_minutes * 60000u);
        if (time_reached(sleep_at)) {
            quit(true);
            return false;
        }
    }
    if (time_reached(run.battery_check)) {
        run.battery_check = make_timeout_time_ms(10000);
        if (!idle_usb_host_active() && battery_empty()) {
            quit(true);
            return false;
        }
    }
    /* With a key down, a second on its column makes no edge to wake on:
     * look every 10ms. */
    absolute_time_t wake = idle_earliest(idle_earliest(until, sleep_at), run.battery_check);
    if (run.held != 0)
        wake = idle_earliest(wake, make_timeout_time_ms(10));
    idle_wait(wake);
    cpuclock_set_fast(true); /* the app works at full speed: frames take the same time, key or not */
    return true;
}

static int api_key(int wait_ms) {
    absolute_time_t until = wait_ms < 0 ? at_the_end_of_time : make_timeout_time_ms((uint32_t) wait_ms);
    int result = PX_KEY_NONE;
    while (true) {
        watchdog_update();
        if (run.quit) {
            result = PX_KEY_QUIT;
            break;
        }
        /* SHIFT and "-" together: a screenshot, and neither key reaches the
         * app. SHIFT then comes as pressed again, which cancels its first
         * press -- as the calculator's own SHIFT does (free42_key_cancel). */
        if (key_hold_app_screenshot()) {
            run.held = keyboard_held();
            result = PX_KEY_SHIFT;
            break;
        }

        /* One change at a time, lowest first; the rest are found again on
         * the next calls, without waiting. */
        uint64_t changed = keyboard_held() ^ run.held;
        if (changed != 0) {
            int bit = __builtin_ctzll(changed);
            run.held ^= 1ull << bit;
            int row = bit / KB_COLS, col = bit % KB_COLS;
            result = key_place(row, col);
            if (!(run.held & 1ull << bit)) {
                if (run.swallow & 1ull << bit) { /* pressed in a dialog */
                    run.swallow &= ~(1ull << bit);
                    result = PX_KEY_NONE;
                    continue;
                }
                result |= PX_KEY_RELEASED;
            } else {
                run.idle_since = get_absolute_time();
                if (key_hold_app_minus((uint8_t) row, (uint8_t) col)) { /* the screenshot gesture's "-" */
                    run.held = keyboard_held();
                    result = PX_KEY_SHIFT; /* as above */
                    break;
                }
                if (result == PX_KEY_SHIFT)
                    key_hold_begin((uint8_t) row, (uint8_t) col); /* the screenshot gesture starts here */
                if (result == PX_KEY_ON) {
                    int on = on_pressed((uint8_t) row, (uint8_t) col);
                    if (on == ON_QUIT)
                        result = PX_KEY_QUIT;
                    if (on == ON_STAY) {
                        result = PX_KEY_NONE;
                        if (time_reached(until))
                            break;
                        continue;
                    }
                }
            }
            break;
        }
        if (time_reached(until))
            break;
        wait(until);
    }
    back_to_app();
    return result;
}

static void api_sleep(int ms) {
    absolute_time_t until = make_timeout_time_ms(ms > 0 ? (uint32_t) ms : 0);
    while (!time_reached(until) && !run.quit) {
        watchdog_update();
        wait(until);
    }
    back_to_app();
}

static void api_show(void) {
    watchdog_update();
    display_flush();
    back_to_app();
}

static void api_clear(void) {
    display_clear();
}

static int api_get_pixel(int x, int y) {
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT)
        return 0;
    return display_fb[y][x >> 3] >> (7 - (x & 7)) & 1;
}

static int clamp(int v) { /* a line's ends: far enough off the screen to keep its slope, near enough to be quick */
    return v < -4096 ? -4096 : v > 4096 ? 4096 : v;
}

static void api_line(int x0, int y0, int x1, int y1, int black) { /* Bresenham */
    x0 = clamp(x0), y0 = clamp(y0), x1 = clamp(x1), y1 = clamp(y1);
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        screen_pixel(x0, y0, black);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static const px_canvas_t screen_canvas = {(uint8_t *) RETAINED_DISPLAY_FB, DISPLAY_WIDTH, DISPLAY_HEIGHT, PX_SCREEN_STRIDE};

static void api_box(int x, int y, int w, int h, int t, int black) {
    px_draw_box(&screen_canvas, x, y, w, h, t, black);
}

static void api_ellipse(int x, int y, int rx, int ry, int t, int black) {
    px_draw_ellipse(&screen_canvas, x, y, rx, ry, t, black);
}

static void api_thick_line(int x0, int y0, int x1, int y1, int t, int black) {
    px_draw_line(&screen_canvas, x0, y0, x1, y1, t, black);
}

static void api_polygon(const px_point_t *points, int n, int t, int black) {
    px_draw_polygon(&screen_canvas, points, n, t, black);
}

static void api_sprite(int x, int y, int w, int h, const uint8_t *bits, const uint8_t *mask, int stride,
                       int mode, int flags) {
    px_draw_sprite(&screen_canvas, x, y, w, h, bits, mask, stride, mode, flags);
}

static void api_bitmap(int x, int y, int w, int h, const uint8_t *bits, int stride) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (bits[j * stride + i / 8] & (0x80 >> (i % 8)))
                screen_pixel(x + i, y + j, 1);
}

static int api_font_height(int font) {
    const display_font_t *f = screen_font(font);
    return f == NULL ? 0 : f->height;
}

/* ----- the standard screens (ui.c), drawn on the app's screen ----- */

static bool ui_shift;

/* A key that goes down, as ui.c wants it: SHIFT is held over to the next
 * key (UI_SHIFTED), its sign in the title bar meanwhile. */
static int ui_key(void) {
    while (true) {
        int k = api_key(-1);
        if (k == PX_KEY_QUIT)
            return PX_UI_QUIT;
        if (k < 0 || (k & PX_KEY_RELEASED))
            continue;
        if (k == PX_KEY_SHIFT) {
            ui_shift = !ui_shift;
            return UI_REDRAW;
        }
        if (ui_shift)
            k |= UI_SHIFTED;
        ui_shift = false;
        return k;
    }
}

static int ui_shifted(void) {
    return ui_shift;
}

static void ui_clock(char *buf, int size) {
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    rtc_format_time(buf, (size_t) size, &now, true);
}

static void ui_beep(void) {
    sounds_play_tone(10, false);
}

static const ui_port_t app_ui = {screen_fill, screen_text, screen_text_width, api_show, ui_key,
                                 ui_shifted, ui_clock, ui_beep};

static int api_ui_message(const char *title, const char *const *lines, int count, const char *const *tabs) {
    return ui_message(&app_ui, title, lines, count, tabs);
}

static int api_ui_list(px_ui_list_t *list) {
    return ui_list(&app_ui, list);
}

static int api_ui_form(px_ui_form_t *form) {
    return ui_form(&app_ui, form);
}

static int api_ui_pulldown(int y, int h, const char *const *items, int count, int *choice) {
    return ui_pulldown(&app_ui, y, h, items, count, choice);
}

static int api_ui_text(const char *title, const char *prompt, char *text, int max, int chars) {
    return ui_text(&app_ui, title, prompt, text, max, chars);
}

static int api_ui_number(const char *title, const char *prompt, int *value, int min, int max) {
    return ui_number(&app_ui, title, prompt, value, min, max);
}

static int api_ui_date(const char *title, int *year, int *month, int *day) {
    return ui_date(&app_ui, title, year, month, day);
}

static void api_ui_title(const char *title, const char *right) {
    ui_title(&app_ui, title, right);
}

static void api_ui_tabs(const char *const *tabs) {
    ui_tabs(&app_ui, tabs);
}

static int api_ui_view(const char *title, const char *text) {
    return ui_view(&app_ui, title, text);
}

static int api_ui_view_leave(const char *title, const char *text, const char *leave) {
    return ui_view_leave(&app_ui, title, text, leave != NULL ? leave : "EXIT");
}

/* Over the app's screen, which comes back after it; millis() stands still
 * meanwhile, so a game doesn't jump on after it, and the keys pressed in it
 * don't come to the app when they're let go. */
static int api_ui_dialog(int icon, const char *text, const char *left, const char *right) {
    static uint8_t under[DISPLAY_HEIGHT * DISPLAY_STRIDE];
    display_copy(under);
    uint64_t before = run.held;
    bool was_asking = run.asking;
    absolute_time_t since = get_absolute_time();
    run.asking = true;
    int r = ui_dialog(&app_ui, icon, text, left, right);
    run.asking = was_asking;
    run.paused_ms += (uint32_t) (absolute_time_diff_us(since, get_absolute_time()) / 1000);
    run.swallow |= run.held & ~before;
    /* A key down before it and let go in it: the dialog took that release,
     * so it's given to the app again (the next key() finds it up). Else a
     * game paused with an arrow held kept moving after GO ON. */
    run.held |= before;
    ui_shift = false;
    if (r != PX_UI_QUIT) {
        display_draw_bitmap(under, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
        display_flush();
    }
    return r;
}

/* A short ON press in an app that hasn't taken ON as a key: true to quit. */
static bool ask_quit(void) {
    char question[48];
    snprintf(question, sizeof(question), "Quit %.32s?", app_name());
    return api_ui_dialog(PX_UI_ICON_CAUTION, question, "QUIT", "STAY") != PX_UI_BACK;
}

static int api_ui_time(const char *title, const char *prompt, int *hours, int *minutes, int *seconds, int flags) {
    return ui_time(&app_ui, title, prompt, hours, minutes, seconds, flags);
}

static uint32_t api_millis(void) {
    return to_ms_since_boot(get_absolute_time()) - run.paused_ms;
}

static void api_datetime(px_datetime_t *now) {
    rtc_datetime_t dt;
    rtc_get_datetime(&dt);
    *now = (px_datetime_t) {dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec, dt.ms, dt.weekday};
}

static uint32_t api_random(void) { /* xorshift32 */
    run.random ^= run.random << 13;
    run.random ^= run.random >> 17;
    run.random ^= run.random << 5;
    return run.random;
}

static void api_tone(int tone) {
    sounds_play_tone(tone, true);
    back_to_app();
}

static void api_tone_async(int tone) {
    sounds_play_tone(tone, false);
    back_to_app();
}

static int api_sound_playing(void) {
    return sounds_playing();
}

static void api_sound_stop(void) {
    sounds_stop();
}

/* "/X/Y" as it is; "Y" in the app's folder. */
static bool full_path(const char *path, char *full, size_t size) {
    size_t n = strnlen(path, PX_PATH_MAX + 1);
    if (n > PX_PATH_MAX)
        return false;
    if (path[0] == '/')
        snprintf(full, size, "%s", path);
    else if (n == 0)
        snprintf(full, size, "%s", run.dir);
    else
        snprintf(full, size, "%s/%s", run.dir, path);
    return true;
}

static int play_file(const char *path, bool wait) {
    char full[PX_PATH_MAX + 70];
    bool ok = full_path(path, full, sizeof(full)) && sounds_play_file(full, wait);
    back_to_app();
    return ok ? 0 : -1;
}

static int api_play(const char *path) {
    return play_file(path, true);
}

static int api_play_async(const char *path) {
    return play_file(path, false);
}

/* A file written is crash-safe (safefile.h): its bytes go to name.NEW, which
 * takes the old one's place only once it has all been written and closed. A
 * cut before that leaves the old file; a cut during the swap leaves the .NEW,
 * which the next read takes in. */
static int api_open(const char *path, const char *mode) {
    char full[PX_PATH_MAX + 70], fresh[PX_PATH_MAX + 74];
    bool write = strcmp(mode, "w") == 0;
    if ((!write && strcmp(mode, "r") != 0) || !full_path(path, full, sizeof(full)))
        return -1;
    snprintf(fresh, sizeof(fresh), "%s.NEW", full);
    for (int i = 0; i < PX_FILES_OPEN; i++) {
        if (run.files[i] == NULL) {
            if (write && path[0] != '/')
                statefs_mkdir(run.dir);
            if (!write)
                safefile_recover(full);
            run.files[i] = statefs_fopen(write ? fresh : full, write ? "wb" : "rb");
            run.written[i] = write;
            run.write_failed[i] = false;
            snprintf(run.paths[i], sizeof(run.paths[i]), "%s", full);
            back_to_app();
            return run.files[i] != NULL ? i : -1;
        }
    }
    return -1;
}

static FILE *file_of(int file) {
    return file >= 0 && file < PX_FILES_OPEN ? run.files[file] : NULL;
}

static int api_read(int file, void *data, int size) {
    FILE *f = file_of(file);
    if (f == NULL || size < 0)
        return -1;
    size_t n = fread(data, 1, (size_t) size, f);
    back_to_app();
    return n == 0 && ferror(f) ? -1 : (int) n;
}

static int api_write(int file, const void *data, int size) {
    FILE *f = file_of(file);
    if (f == NULL || size < 0 || !run.written[file])
        return -1;
    size_t n = fwrite(data, 1, (size_t) size, f);
    back_to_app();
    if (n < (size_t) size)
        run.write_failed[file] = true;
    return n == 0 && size > 0 ? -1 : (int) n;
}

static int api_close(int file) {
    FILE *f = file_of(file);
    if (f == NULL)
        return -1;
    bool ok = fclose(f) == 0;
    if (run.written[file]) { /* whole: it takes the old one's place; not: it goes, and the old one stays */
        ok = statefs_sync() && ok && !run.write_failed[file];
        char fresh[PX_PATH_MAX + 74];
        snprintf(fresh, sizeof(fresh), "%s.NEW", run.paths[file]);
        if (ok)
            ok = safefile_take_new(run.paths[file]);
        else
            statefs_remove(fresh);
    }
    run.files[file] = NULL;
    back_to_app();
    return ok ? 0 : -1;
}

static int api_load(const char *path, void *data, int size) {
    int f = api_open(path, "r");
    if (f < 0)
        return -1;
    int n = api_read(f, data, size);
    api_close(f);
    return n;
}

static int api_save(const char *path, const void *data, int size) {
    int f = api_open(path, "w");
    if (f < 0)
        return -1;
    bool ok = api_write(f, data, size) == size;
    return api_close(f) == 0 && ok ? 0 : -1;
}

static int api_remove(const char *path) {
    char full[PX_PATH_MAX + 70], fresh[PX_PATH_MAX + 74];
    bool ok = full_path(path, full, sizeof(full)) && statefs_remove(full) == 0;
    snprintf(fresh, sizeof(fresh), "%s.NEW", full);
    if (ok && statefs_file_exists(fresh)) /* or the next read would bring it back */
        statefs_remove(fresh);
    ok = statefs_sync() && ok;
    back_to_app();
    return ok ? 0 : -1;
}

static int api_mkdir(const char *path) {
    char full[PX_PATH_MAX + 70];
    if (!full_path(path, full, sizeof(full)))
        return -1;
    if (path[0] != '/')
        statefs_mkdir(run.dir);
    statefs_mkdir(full);
    bool ok = statefs_file_exists(full);
    statefs_sync();
    back_to_app();
    return ok ? 0 : -1;
}

static int api_list(const char *dir, const char *ext, char (*names)[PX_FILE_NAME_MAX + 1], int max) {
    char full[PX_PATH_MAX + 70];
    if (max <= 0 || strnlen(ext, 8) >= 8 || !full_path(dir, full, sizeof(full)))
        return 0;
    int n = statefs_list_files(full, ext, (char *) names, PX_FILE_NAME_MAX + 1, max);
    back_to_app();
    return n;
}

static int api_serial_write(const void *data, int size) {
    if (size <= 0)
        return 0;
    stdio_put_string((const char *) data, size, false, false);
    back_to_app();
    return size;
}

static int api_serial_read(void *data, int size) {
    int n = 0;
    for (int c; n < size && (c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT;)
        ((uint8_t *) data)[n++] = (uint8_t) c;
    return n;
}

static void api_ir_send(const px_ir_burst_t *burst) {
    if (burst->count < 2 || burst->count > PX_IR_MAX_TIMINGS || burst->carrier_hz < 10000
        || burst->carrier_hz > 100000)
        return;
    cpuclock_set_fast(true); /* irtx.h: at full speed */
    irtx_send(burst);
    back_to_app();
}

static int api_battery_mv(void) {
    return battery_read_mv();
}

static int api_midi_play(const char *path) {
    char full[PX_PATH_MAX + 70];
    int r = full_path(path, full, sizeof(full)) ? sounds_midi_play(full) : SOUNDS_MIDI_NOT_FOUND;
    back_to_app();
    return r;
}

static void api_midi_status(px_midi_status_t *st) {
    memset(st, 0, sizeof(*st));
    sounds_midi_status(&st->length_ms, &st->elapsed_ms, &st->channels, &st->voices);
}

static int api_usb_connected(void) {
    return idle_usb_host_active();
}

static void api_on_key(int on_is_key) {
    run.on_is_key = on_is_key != 0;
}

/* The app's name, its file's: what its alarms are kept under. */
static const char *app_name(void) {
    const char *slash = strrchr(run.dir, '/');
    return slash != NULL ? slash + 1 : run.dir;
}

static int api_alarm_set(const px_datetime_t *when, const char *text) {
    size_t n = strnlen(text, PX_ALARM_TEXT_MAX + 1);
    const px_datetime_t *w = when;
    if (n == 0 || n > PX_ALARM_TEXT_MAX || w->year < 1970 || w->year > 2199 || w->month < 1 || w->month > 12
        || w->day < 1 || w->day > time_days_in_month(w->year, w->month) || w->hour < 0 || w->hour > 23 || w->min < 0
        || w->min > 59 || w->sec < 0 || w->sec > 59)
        return -1;
    uint64_t at = (uint64_t) time_days_from_civil(w->year, w->month, w->day) * 86400000u
                  + (uint64_t) ((w->hour * 60 + w->min) * 60 + w->sec) * 1000u;
    uint64_t now = rtc_now_ms();
    if (at <= now)
        return -1;
    return alarms_add_owned(at, text, (int) n, app_name(), now) ? 0 : -1;
}

static int api_alarm_clear(void) {
    return alarms_remove_owned(app_name());
}

static int api_printer(void) {
    switch (printer_target()) {
        case PRINTER_USB_SERIAL: return PX_PRINTER_USB;
        case PRINTER_HP82240B: return PX_PRINTER_IR;
        case PRINTER_ESCPOS: return PX_PRINTER_THERMAL;
        default: return PX_PRINTER_OFF;
    }
}

static int api_print_ready(void) {
    return (printer_target() != PRINTER_HP82240B && printer_target() != PRINTER_ESCPOS) || printer_ready();
}

static int api_print_text(const char *text, int length) {
    if (printer_target() == PRINTER_OFF)
        return -1;
    free42_print_app_text(text, length < 0 ? 0 : length);
    return 0;
}

static int (*print_stop)(void); /* print_screen()'s, while it runs */
static bool print_stop_asked(void) {
    return print_stop != NULL && print_stop() != 0;
}

static int api_print_screen(int (*stop)(void)) {
    switch (printer_target()) {
        case PRINTER_USB_SERIAL:
            printer_write_screen();
            return 0;
        case PRINTER_HP82240B: {
            print_stop = stop;
            bool printed = printer_hp82240b_screen(print_stop_asked);
            print_stop = NULL;
            return printed ? 0 : 1;
        }
        case PRINTER_ESCPOS: {
            print_stop = stop;
            bool printed = printer_escpos_screen(px_settings.screen_print == SCREEN_PRINT_AS_IS, print_stop_asked);
            print_stop = NULL;
            return printed ? 0 : 1;
        }
        default: return -1;
    }
}

static const px_api_t api = {
        .version = PX_APP_API_VERSION,
        .screen = (uint8_t *) RETAINED_DISPLAY_FB,
        .show = api_show,
        .clear = api_clear,
        .pixel = screen_pixel,
        .get_pixel = api_get_pixel,
        .fill = screen_fill,
        .line = api_line,
        .bitmap = api_bitmap,
        .text = screen_text,
        .text_width = screen_text_width,
        .font_height = api_font_height,
        .key = api_key,
        .millis = api_millis,
        .sleep = api_sleep,
        .datetime = api_datetime,
        .random = api_random,
        .tone = api_tone,
        .play = api_play,
        .tone_async = api_tone_async,
        .play_async = api_play_async,
        .sound_playing = api_sound_playing,
        .sound_stop = api_sound_stop,
        .load = api_load,
        .save = api_save,
        .open = api_open,
        .read = api_read,
        .write = api_write,
        .close = api_close,
        .remove = api_remove,
        .mkdir = api_mkdir,
        .list = api_list,
        .serial_write = api_serial_write,
        .serial_read = api_serial_read,
        .ir_send = api_ir_send,
        .battery_mv = api_battery_mv,
        .usb_connected = api_usb_connected,
        .dec = appdec_op,
        .dec_from_int = appdec_from_int,
        .dec_to_int = appdec_to_int,
        .dec_from_string = appdec_from_string,
        .dec_to_string = appdec_to_string,
        .dec_cmp = appdec_cmp,
        .dec_class = appdec_class,
        .on_key = api_on_key,
        .midi_play = api_midi_play,
        .midi_status = api_midi_status,
        .box = api_box,
        .ellipse = api_ellipse,
        .thick_line = api_thick_line,
        .polygon = api_polygon,
        .alarm_set = api_alarm_set,
        .alarm_clear = api_alarm_clear,
        .printer = api_printer,
        .print_ready = api_print_ready,
        .print_text = api_print_text,
        .print_screen = api_print_screen,
        .sprite = api_sprite,
        .ui_message = api_ui_message,
        .ui_list = api_ui_list,
        .ui_form = api_ui_form,
        .ui_pulldown = api_ui_pulldown,
        .ui_text = api_ui_text,
        .ui_number = api_ui_number,
        .ui_date = api_ui_date,
        .ui_time = api_ui_time,
        .ui_title = api_ui_title,
        .ui_tabs = api_ui_tabs,
        .ui_view = api_ui_view,
        .ui_dialog = api_ui_dialog,
        .ui_view_leave = api_ui_view_leave,
};

void app_folders(void) {
    statefs_mkdir(BIN_DIR);
    statefs_mkdir(APPS_DIR);
    statefs_mkdir(GAMES_DIR);
}

bool app_icon(const char *dir, const char *name, uint8_t bits[128]) {
    char path[100];
    snprintf(path, sizeof(path), "%s/%s.bin", dir, name);
    FILE *f = statefs_fopen(path, "rb");
    if (f == NULL)
        return false;
    uint8_t head[sizeof(px_app_header_t) + sizeof(px_app_icon_t)];
    size_t n = fread(head, 1, sizeof(head), f);
    fclose(f);
    return app_icon_from(head, n, bits);
}

/* Reads the whole file once to check it -- before anything is changed. */
static const char *check_file(const char *path, px_app_header_t *h) {
    FILE *f = statefs_fopen(path, "rb");
    if (f == NULL)
        return "Can't open the file";
    uint32_t size = (uint32_t) fread(h, 1, sizeof(*h), f), crc = 0;
    static uint8_t buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && size <= PX_APP_RAM) {
        watchdog_update();
        crc = app_crc32(crc, buf, n);
        size += (uint32_t) n;
    }
    fclose(f);
    const char *error = app_header_check(h, size);
    if (error == NULL && crc != h->crc32)
        error = "The file is damaged";
    return error;
}

/* TEMPORARY heap measurement (app.h). malloc() returns NULL when it can't
 * (PICO_MALLOC_PANIC=0), so halving between what fits and what doesn't
 * finds the largest block to within 64 bytes. */
extern char __end__[], __HeapLimit[];
static uint32_t heap_at_launch;

uint32_t app_heap_total(void) {
    return (uint32_t) (__HeapLimit - __end__);
}

uint32_t app_heap_largest(void) {
    uint32_t fits = 0, fails = app_heap_total() + 1;
    while (fails - fits > 64) {
        uint32_t try = fits + (fails - fits) / 2;
        void *p = malloc(try);
        if (p != NULL) {
            free(p);
            fits = try;
        } else
            fails = try;
    }
    return fits;
}

uint32_t app_heap_at_launch(void) {
    return heap_at_launch;
}

const char *app_run(const char *dir, const char *name, bool *power_off) {
    *power_off = false;
    char path[100];
    snprintf(path, sizeof(path), "%s/%s.bin", dir, name);
    px_app_header_t h;
    const char *error = check_file(path, &h);
    if (error != NULL)
        return error;

    /* Its RAM is the RAM copy of the calculator's memory: that goes to the
     * drive first, where a restart after a crash takes it from. */
    if (!free42_save_to_drive())
        return "Can't save the memory";
    FILE *f = statefs_fopen(path, "rb");
    uint8_t *ram = (uint8_t *) app_base(&h);
    uint32_t got = 0;
    for (size_t n; f != NULL && got < h.image_size && (n = fread(ram + got, 1, 4096, f)) > 0;) {
        watchdog_update(); /* 160 KB in one read could outlast the watchdog */
        got += (uint32_t) n;
    }
    bool loaded = got == h.image_size && app_crc32(0, ram + sizeof(h), h.image_size - sizeof(h)) == h.crc32;
    if (f != NULL)
        fclose(f);
    if (!loaded) {
        free42_memory_to_ram();
        return "Can't read the file";
    }
    memset(ram + h.image_size, 0, h.bss_size);
    __dsb();
    __isb();

    memset(&run, 0, sizeof(run));
    snprintf(run.dir, sizeof(run.dir), "%s/%s", dir, name);
    run.held = keyboard_held(); /* ENTER, from the menu: no key up for it */
    run.idle_since = get_absolute_time();
    run.battery_check = get_absolute_time();
    run.random = (uint32_t) time_us_64() ^ (uint32_t) powman_timer_get_ms() ^ 0x9E3779B9u;
    if (run.random == 0)
        run.random = 1;

    back_to_app();
    heap_at_launch = app_heap_largest();
    int result = ((int (*)(const px_api_t *)) h.entry)(&api);
    debug_checkpoint(CKPT_MENU, 0);

    sounds_stop(); /* its last sound, if still playing */
    for (int i = 0; i < PX_FILES_OPEN; i++)
        if (run.files[i] != NULL)
            api_close(i);
    free42_memory_to_ram();
    *power_off = run.power_off || result == PX_EXIT_OFF;
    return NULL;
}
