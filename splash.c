#include "splash.h"

#include <stdio.h>
#include <string.h>

#include "pico/time.h"
#include "display.h"
#include "keyboard.h"
#include "rtc.h"
#include "settings.h"
#include "font_10x16.h"
#include "images/image.h"
#include "hardware/watchdog.h"
#include "png1bit.h"
#include "statefs.h"

#define BITMAP_SIZE (DISPLAY_STRIDE * DISPLAY_HEIGHT)

/* The image shown while off: the built-in one (images/splash.png, compiled
 * in), or a user PNG from /SPLASH decoded into active_bits. preview_bits
 * holds one being looked at in the settings menu. */
static uint8_t active_bits[BITMAP_SIZE];
static bool active_custom = false;
static uint8_t preview_bits[BITMAP_SIZE];
static bool preview_custom = false;

static int file_read(void *ctx, uint8_t *buf, int len) {
    size_t n = fread(buf, 1, (size_t) len, (FILE *) ctx);
    return n == 0 && ferror((FILE *) ctx) ? -1 : (int) n;
}

static void feed_watchdog(void) {
    watchdog_update();
}

static bool decode_file(const char *name, uint8_t *bits, char *error, size_t error_size) {
    char path[48];
    snprintf(path, sizeof(path), "%s/%s.png", STATEFS_SPLASH_DIR, name);
    FILE *f = statefs_fopen(path, "rb");
    if (f == NULL) {
        snprintf(error, error_size, "can't open the file");
        return false;
    }
    png_source_t src = {file_read, f, feed_watchdog};
    bool ok = png_decode_1bit(&src, bits, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE, error, error_size);
    fclose(f);
    return ok;
}

void splash_reload(void) {
    char error[48];
    active_custom = px_settings.splash_image[0] != 0
                    && decode_file(px_settings.splash_image, active_bits, error, sizeof(error));
}

bool splash_preview_load(const char *name, char *error, size_t error_size) {
    preview_custom = name != NULL;
    return name == NULL || decode_file(name, preview_bits, error, error_size);
}

void splash_preview_use(void) {
    if (preview_custom)
        memcpy(active_bits, preview_bits, BITMAP_SIZE);
    active_custom = preview_custom;
}

static void draw_image(bool custom, const uint8_t *bits) {
    if (custom)
        display_draw_bitmap(bits, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
    else
        display_draw_bitmap(splash_image.data, splash_image.width, splash_image.height, 0, 0);
}

/* Date/time bar across the top of the image: white on black, so it reads
 * on any picture. "TUE 2026-10-06 14:35" is 20 glyphs. */
#define BAR_HEIGHT 20
#define BAR_TEXT_Y 2

/* The date/time bar, over whatever is in the framebuffer. */
static void draw_bar(const rtc_datetime_t *now) {
    static const char *const weekday_names[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

    display_fill_rect(0, 0, DISPLAY_WIDTH - 1, BAR_HEIGHT - 1, true);

    /* "FRI 2026-10-09 15:43", in the Date & Time formats: at most 22
     * characters ("FRI 10/09/2026 12:43 PM" drops a space), 220 pixels. */
    char date[12], time[10], buf[28];
    rtc_format_date(date, sizeof(date), now);
    rtc_format_time(time, sizeof(time), now, false);
    if (px_settings.date_format == RTC_WMD) /* "FRI-OCT-9" has the weekday already */
        snprintf(buf, sizeof(buf), "%s %s", date, time);
    else
        snprintf(buf, sizeof(buf), "%s %s %s", weekday_names[now->weekday], date, time);
    if (strlen(buf) > 22)
        snprintf(buf, sizeof(buf), "%s %s%s", weekday_names[now->weekday], date, time);
    int text_width = (int) strlen(buf) * font_10x16.advance;
    display_draw_string(&font_10x16, buf, (uint8_t) ((DISPLAY_WIDTH - text_width) / 2), BAR_TEXT_Y, 0xFF);
}

/* The image plus the date/time bar (Show Time), into the framebuffer. */
static void draw_screen(bool custom, const uint8_t *bits, const rtc_datetime_t *now) {
    display_clear();
    draw_image(custom, bits);
    if (px_settings.splash_clock)
        draw_bar(now);
}

void splash_preview_draw(void) {
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    draw_screen(preview_custom, preview_bits, &now);
}

static void splash_draw(const rtc_datetime_t *now) {
    draw_screen(active_custom, active_bits, now);
    display_flush();
}

/* After drawing the first splash at the normal 8Hz, wait a couple of
 * refreshes (125ms each) before slowing to 1Hz, so the whole image is on
 * the glass before the scan slows down. */
#define FAST_SETTLE_MS 300

void splash_update_clock(void) {
    if (!px_settings.splash_clock)
        return; /* the image alone: nothing changes */
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    draw_bar(&now);
    display_flush();
}

void splash_show_static(void) {
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    splash_draw(&now);
    sleep_ms(FAST_SETTLE_MS);
    display_set_slow_refresh(true);
}
