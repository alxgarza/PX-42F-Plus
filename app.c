#include "app.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/powman.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "appdec.h"
#include "appfile.h"
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
#include "keyboard.h"
#include "keyhold.h"
#include "px_app.h"
#include "retained.h"
#include "rtc.h"
#include "settings.h"
#include "sounds.h"
#include "statefs.h"

_Static_assert(PX_APP_BASE == RETAINED_RAMSTATE, "apps run in the RAM that keeps the memory while off");
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
    bool power_off;              /* ... to turn off */
    uint32_t random;
    FILE *files[PX_FILES_OPEN];
    bool written[PX_FILES_OPEN];
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

/* ON, waited out: held for EXIT_HOLD_OFF_MS it quits and turns off; let
 * go sooner it quits -- or, for an app that asked (px->on_key()), it's an
 * ordinary key. True if it's the key. */
static bool on_pressed(uint8_t row, uint8_t col) {
    key_hold_begin(row, col);
    while (keyboard_is_held(row, col)) {
        watchdog_update();
        if (key_hold_poll() == KEY_HOLD_OFF) {
            quit(true);
            return false;
        }
        sleep_ms(10);
    }
    if (run.on_is_key)
        return true;
    quit(false);
    return false;
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
                result |= PX_KEY_RELEASED;
            } else {
                run.idle_since = get_absolute_time();
                if (result == PX_KEY_SHIFT)
                    key_hold_begin((uint8_t) row, (uint8_t) col); /* the screenshot gesture starts here */
                if (result == PX_KEY_ON && !on_pressed((uint8_t) row, (uint8_t) col))
                    result = PX_KEY_QUIT;
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

static void api_pixel(int x, int y, int black) {
    if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT)
        display_set_pixel((uint8_t) x, (uint8_t) y, black != 0);
}

static int api_get_pixel(int x, int y) {
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT)
        return 0;
    return display_fb[y][x >> 3] >> (7 - (x & 7)) & 1;
}

static void api_fill(int x, int y, int w, int h, int black) {
    int x1 = x + w - 1, y1 = y + h - 1;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 >= DISPLAY_WIDTH)
        x1 = DISPLAY_WIDTH - 1;
    if (y1 >= DISPLAY_HEIGHT)
        y1 = DISPLAY_HEIGHT - 1;
    if (x <= x1 && y <= y1)
        display_fill_rect((uint8_t) x, (uint8_t) y, (uint8_t) x1, (uint8_t) y1, black != 0);
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
        api_pixel(x0, y0, black);
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

static void api_bitmap(int x, int y, int w, int h, const uint8_t *bits, int stride) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (bits[j * stride + i / 8] & (0x80 >> (i % 8)))
                api_pixel(x + i, y + j, 1);
}

static const display_font_t *font_by_number(int font) {
    switch (font) {
        case PX_FONT_SMALL: return &font_8x12;
        case PX_FONT_LARGE: return &font_10x16;
        case PX_FONT_MINI: return &font_6x8;
        case PX_FONT_CHOSEN_SMALL: return &font_small;
        case PX_FONT_CHOSEN_LARGE: return &font_large;
        case PX_FONT_CHOSEN_MINI: return &font_mini;
        default: return NULL;
    }
}

static int api_text(int x, int y, const char *s, int font, int inverse) {
    const display_font_t *f = font_by_number(font);
    if (f == NULL)
        return x;
    /* display_draw_char() takes 0-255 and clips each pixel: skip what's
     * left of the screen, stop at its right edge. */
    for (; *s != 0 && x < DISPLAY_WIDTH; s++) {
        if (x > -f->advance && y > -f->height && y < DISPLAY_HEIGHT) {
            if (x >= 0 && y >= 0) {
                display_draw_char(f, *s, (uint8_t) x, (uint8_t) y, inverse ? 0xFF : 0x00);
            } else { /* partly off the top or left: pixel by pixel */
                uint8_t c = (uint8_t) *s;
                if (c >= f->first_char && c <= f->last_char) {
                    const uint8_t *glyph = f->data + (c - f->first_char) * f->bytes_per_char;
                    for (int bit = 0; bit < f->width * f->height; bit++)
                        api_pixel(x + bit % f->width, y + bit / f->width,
                                  ((glyph[bit / 8] >> (7 - bit % 8)) & 1) ^ (inverse != 0));
                }
            }
        }
        x += f->advance;
    }
    return x;
}

static int api_text_width(const char *s, int font) {
    const display_font_t *f = font_by_number(font);
    return f == NULL ? 0 : (int) strlen(s) * f->advance;
}

static int api_font_height(int font) {
    const display_font_t *f = font_by_number(font);
    return f == NULL ? 0 : f->height;
}

static uint32_t api_millis(void) {
    return to_ms_since_boot(get_absolute_time());
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

static int api_open(const char *path, const char *mode) {
    char full[PX_PATH_MAX + 70];
    bool write = strcmp(mode, "w") == 0;
    if ((!write && strcmp(mode, "r") != 0) || !full_path(path, full, sizeof(full)))
        return -1;
    for (int i = 0; i < PX_FILES_OPEN; i++) {
        if (run.files[i] == NULL) {
            if (write && path[0] != '/')
                statefs_mkdir(run.dir);
            run.files[i] = statefs_fopen(full, write ? "wb" : "rb");
            run.written[i] = write;
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
    return n == 0 && size > 0 ? -1 : (int) n;
}

static int api_close(int file) {
    FILE *f = file_of(file);
    if (f == NULL)
        return -1;
    bool ok = fclose(f) == 0;
    if (run.written[file])
        ok = statefs_sync() && ok;
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
    char full[PX_PATH_MAX + 70];
    bool ok = full_path(path, full, sizeof(full)) && statefs_remove(full) == 0;
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

static int api_usb_connected(void) {
    return idle_usb_host_active();
}

static void api_on_key(int on_is_key) {
    run.on_is_key = on_is_key != 0;
}

static const px_api_t api = {
        .version = PX_APP_API_VERSION,
        .screen = (uint8_t *) RETAINED_DISPLAY_FB,
        .show = api_show,
        .clear = api_clear,
        .pixel = api_pixel,
        .get_pixel = api_get_pixel,
        .fill = api_fill,
        .line = api_line,
        .bitmap = api_bitmap,
        .text = api_text,
        .text_width = api_text_width,
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
};

void app_folders(void) {
    statefs_mkdir(BIN_DIR);
    statefs_mkdir(APPS_DIR);
    statefs_mkdir(GAMES_DIR);
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
    uint8_t *ram = (uint8_t *) PX_APP_BASE;
    uint32_t got = 0;
    for (size_t n; f != NULL && got < h.image_size && (n = fread(ram + got, 1, 4096, f)) > 0;) {
        watchdog_update(); /* 128 KB in one read could outlast the watchdog */
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
