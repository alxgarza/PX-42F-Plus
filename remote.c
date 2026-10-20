#include "remote.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "cpuclock.h"
#include "display.h"
#include "font_8x12.h"
#include "idle.h"
#include "irtx.h"
#include "keyboard.h"
#include "keyhold.h"
#include "settings.h"

/* The map: a title bar, then the keyboard's 7 rows. */
#define TITLE_HEIGHT 13
#define ROW_Y0       14
#define ROW_HEIGHT   15
#define CHAR_W       8 /* font_8x12 */

/* A key's box on the map, laid out like the keyboard: 6 keys in the top
 * two rows, ENTER twice as wide, 5 wider keys below. */
static void key_box(int row, int col, int *x, int *w) {
    if (row < 2) {
        *w = 41;
        *x = 2 + col * 41;
    } else if (row == 2) {
        *w = col == 0 ? 82 : 41;
        *x = col == 0 ? 2 : 2 + 82 + (col - 1) * 41;
    } else {
        *w = 49;
        *x = 2 + col * 49;
    }
}

static void draw_key(int row, int col, const char *label, bool inverted) {
    int x, w;
    key_box(row, col, &x, &w);
    int y = ROW_Y0 + row * ROW_HEIGHT;
    if (inverted)
        display_fill_rect((uint8_t) x, (uint8_t) y, (uint8_t) (x + w - 2), (uint8_t) (y + ROW_HEIGHT - 3), true);
    int n = (int) strlen(label);
    if (n > w / CHAR_W)
        n = w / CHAR_W;
    char text[IR_LABEL_MAX + 1];
    memcpy(text, label, (size_t) n);
    text[n] = 0;
    display_draw_string(&font_8x12, text, (uint8_t) (x + (w - 1 - n * CHAR_W) / 2), (uint8_t) (y + 1),
                        inverted ? 0xFF : 0x00);
}

/* The whole map; pressed_row/col (or -1) shown pressed, and the name of
 * what was last sent in the title bar. */
static void draw(const ir_remote_t *remote, const char *name, int pressed_row, int pressed_col, const char *sent) {
    display_clear();
    display_fill_rect(0, 0, DISPLAY_WIDTH - 1, TITLE_HEIGHT - 2, true);
    char title[16];
    snprintf(title, sizeof(title), "%.14s", name);
    display_draw_string(&font_8x12, title, 2, 0, 0xFF);
    char right[20];
    snprintf(right, sizeof(right), "%.15s", sent);
    display_draw_string(&font_8x12, right, (uint8_t) (DISPLAY_WIDTH - 2 - (int) strlen(right) * CHAR_W), 0, 0xFF);

    for (int row = 0; row < IR_ROWS; row++) {
        if (row > 0)
            display_draw_dotted_hline(0, DISPLAY_WIDTH - 1, (uint8_t) (ROW_Y0 + row * ROW_HEIGHT - 2));
        for (int col = 0; col < ir_row_keys(row); col++) {
            const ir_key_t *key = &remote->keys[row][col];
            if (row == 6 && col == 0)
                draw_key(row, col, "Calc", true); /* ON: back to the calculator */
            else if (key->used)
                draw_key(row, col, key->label, row == pressed_row && col == pressed_col);
        }
    }
    display_flush();
}

/* The keyboard matrix is wired as the keys sit, except XEQ and TAN -- the
 * top rows' sixth keys -- which are on an eighth row. */
static void key_place(uint8_t row, uint8_t col, int *key_row, int *key_col) {
    if (row == 7) {
        *key_row = col;
        *key_col = 5;
    } else {
        *key_row = row;
        *key_col = col;
    }
}

/* Waits for a key to go down, sleeping (idle.h) meanwhile. False when the
 * Sleep Timer runs out instead (not on a computer's USB, as everywhere). */
static bool wait_key_down(uint8_t *row, uint8_t *col) {
    absolute_time_t sleep_at = px_settings.sleep_minutes != 0
                                       ? make_timeout_time_ms((uint32_t) px_settings.sleep_minutes * 60000u)
                                       : at_the_end_of_time;
    while (true) {
        watchdog_update();
        if (keyboard_scan(row, col))
            return true;
        if (time_reached(sleep_at) && !idle_usb_host_active())
            return false;
        idle_wait(idle_earliest(sleep_at, make_timeout_time_ms(60000)));
    }
}

/* While a key with nothing on it is down -- or ON, until it's let go or
 * held long enough to turn off. */
static key_hold_t wait_key_up(uint8_t row, uint8_t col) {
    for (int i = 0; i < KEY_STUCK_MS / 10 && keyboard_is_held(row, col); i++) {
        watchdog_update();
        key_hold_t hold = key_hold_poll();
        if (hold != KEY_HOLD_NONE)
            return hold;
        sleep_ms(10);
    }
    return KEY_HOLD_NONE;
}

bool remote_run(const ir_remote_t *remote, const char *name) {
    static bool toggle; /* RC5 and RC6 tell presses apart by it */
    const char *sent = "";
    draw(remote, name, -1, -1, sent);
    while (true) {
        uint8_t row, col;
        if (!wait_key_down(&row, &col))
            return true;
        cpuclock_set_fast(true);
        key_hold_begin(row, col);
        int key_row, key_col;
        key_place(row, col, &key_row, &key_col);
        const ir_key_t *key = &remote->keys[key_row][key_col];

        if (key_row == 6 && key_col == 0) /* ON */
            return wait_key_up(row, col) == KEY_HOLD_OFF;
        if (!key->used) {
            if (wait_key_up(row, col) == KEY_HOLD_SCREENSHOT)
                draw(remote, name, -1, -1, sent);
            continue;
        }

        /* A short press sends what the protocol wants for one (Sony: the
         * code 3 times), a held key goes on repeating until let go. */
        draw(remote, name, key_row, key_col, key->name);
        toggle = !toggle;
        for (int n = 0; n < ir_presses(key->code.protocol) || keyboard_is_held(row, col); n++) {
            ir_burst_t burst;
            ir_encode(&key->code, toggle, n, &burst);
            irtx_send(&burst);
            if (key_hold_poll() == KEY_HOLD_SCREENSHOT || n > KEY_STUCK_MS / 100)
                break;
        }
        wait_key_up(row, col); /* (only a stuck key is still down) */
        sent = key->name;
        draw(remote, name, -1, -1, sent);
    }
}
