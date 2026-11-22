#include "screenshot.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "display.h"
#include "display_fb.h"
#include "font_8x12.h"
#include "keyboard.h"
#include "printer.h"
#include "px_app.h"
#include "settings.h"
#include "settings_menu.h"
#include "usbprint.h"
#include "pngwrite.h"
#include "rtc.h"
#include "sounds.h"
#include "statefs.h"

/* The screen as screenshot_remember() last found it. */
static uint8_t shot[DISPLAY_STRIDE * DISPLAY_HEIGHT];

static bool file_write(void *ctx, const uint8_t *data, size_t len) {
    return fwrite(data, 1, len, (FILE *) ctx) == len;
}

/* Writes shot to the drive. NULL, with the file's name (no folder) in name,
 * or what went wrong -- at most 19 characters, to fit the message line
 * after "Screenshot: ". */
static const char *save(char *name, size_t name_size) {
    if (!statefs_ready())
        return "Drive Unavailable"; /* none, or the computer has it (USB Drive) */
    statefs_mkdir(SCREENSHOT_DIR);

    /* Named for the moment, like the states. Two can't come from the keys
     * in one second, but a full chip reset starts the clock again at the
     * same default time: never write over an older one. */
    rtc_datetime_t now;
    char stamp[24], path[64];
    rtc_get_datetime(&now);
    rtc_format_stamp(stamp, sizeof(stamp), &now);
    for (int n = 1;; n++) {
        if (n == 1)
            snprintf(name, name_size, "%s.png", stamp);
        else
            snprintf(name, name_size, "%s-%d.png", stamp, n);
        snprintf(path, sizeof(path), "%s/%s", SCREENSHOT_DIR, name);
        if (!statefs_file_exists(path))
            break;
        if (n == 9)
            return "Name In Use";
    }

    FILE *f = statefs_fopen(path, "wb");
    if (f == NULL)
        return "Can't Create File";
    bool ok = png_write_1bit(shot, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE, file_write, f);
    ok = fclose(f) == 0 && ok;
    if (!ok)
        statefs_remove(path); /* half a file */
    ok = statefs_sync() && ok;
    return ok ? NULL : "Write Failed";
}

void screenshot_remember(void) {
    display_copy(shot);
}

static void save_and_say(void) {
    char name[32], line[40];
    const char *error = save(name, sizeof(name));
    if (error == NULL)
        snprintf(line, sizeof(line), "Saved %s", name);
    else
        snprintf(line, sizeof(line), "Screenshot: %s", error);
    display_fill_rect(0, 0, DISPLAY_WIDTH - 1, 12, true);
    display_draw_string(&font_8x12, line, 2, 0, 0xFF);
    display_flush();
    if (error != NULL)
        sounds_play_tone(10, true); /* the error sound */
    for (int i = 0; i < 100; i++) { /* a second to read it */
        watchdog_update();
        sleep_ms(10);
    }
}

/* The printers that put it on paper (the user's choice): not Off, and not
 * the USB serial port, a terminal's text. */
static bool real_printer(void) {
    return px_settings.printer == PRINTER_HP82240B ||
           (px_settings.printer == PRINTER_ESCPOS && !usbprint_unavailable());
}

static bool key_down(void) {
    return keyboard_held() != 0;
}

static void restore(void) {
    memcpy(&display_fb[0][0], shot, sizeof(shot));
}

void screenshot_take(void) {
    /* Asked at once, while the keys are still down (the user's choice):
     * SAVE first, the default; PRINT only with a printer that puts it on
     * paper; CANCEL last, as EXIT and <-. */
    static const char *const with_print[] = {"SAVE", "PRINT", "CANCEL"};
    static const char *const without[] = {"SAVE", "CANCEL"};
    bool paper = real_printer();
    int choice = paper ? settings_menu_dialog(PX_UI_ICON_NOTE, "Save the screenshot,\nor print it?", with_print, 3)
                       : settings_menu_dialog(PX_UI_ICON_NOTE, "Save the screenshot?", without, 2);
    const char *picked = choice >= 0 ? (paper ? with_print : without)[choice] : "CANCEL";
    restore(); /* the dialog's gone; the screen as it was is also what prints */
    if (strcmp(picked, "PRINT") == 0) {
        display_fill_rect(0, 0, DISPLAY_WIDTH - 1, 12, true);
        display_draw_string(&font_8x12, "Printing... (a key stops)", 2, 0, 0xFF);
        display_flush();
        restore(); /* the picture without that line */
        if (px_settings.printer == PRINTER_HP82240B)
            printer_hp82240b_screen(key_down); /* the HP's turned picture */
        else
            printer_escpos_screen(true, key_down);
        while (key_down()) /* the key that stopped it isn't for the caller */
            watchdog_update();
    } else if (strcmp(picked, "SAVE") == 0) {
        save_and_say();
    }
    restore();
    display_flush();
}

/* Base64, a line of 76 characters at a time. */
typedef struct {
    uint8_t pending[3];
    int pending_len;
    char line[80];
    int line_len;
} base64_t;

static void base64_group(base64_t *b) {
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t v = (uint32_t) b->pending[0] << 16 | (uint32_t) b->pending[1] << 8 | b->pending[2];
    for (int i = 0; i < 4; i++)
        b->line[b->line_len++] = i <= b->pending_len ? digits[(v >> (18 - 6 * i)) & 63] : '=';
    b->pending_len = 0;
    memset(b->pending, 0, sizeof(b->pending));
}

static void base64_line(base64_t *b) {
    b->line[b->line_len] = 0;
    puts(b->line);
    b->line_len = 0;
    watchdog_update();
}

static bool base64_write(void *ctx, const uint8_t *data, size_t len) {
    base64_t *b = (base64_t *) ctx;
    for (size_t i = 0; i < len; i++) {
        b->pending[b->pending_len++] = data[i];
        if (b->pending_len == 3) {
            base64_group(b);
            if (b->line_len == 76)
                base64_line(b);
        }
    }
    return true;
}

void screenshot_to_console(void) {
    static base64_t b;
    memset(&b, 0, sizeof(b));
    display_copy(shot); /* free: console commands aren't read while a key is down (keyhold.c) */
    puts("SCREENSHOT BEGIN");
    png_write_1bit(shot, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE, base64_write, &b);
    if (b.pending_len > 0)
        base64_group(&b);
    if (b.line_len > 0)
        base64_line(&b);
    puts("SCREENSHOT END");
}
