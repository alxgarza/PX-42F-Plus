#include "keyhold.h"

#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "free42_app.h"
#include "keyboard.h"
#include "screenshot.h"

static int code;                 /* the key down, as free42_key_code() */
static uint8_t key_row, key_col;
static absolute_time_t down_since;
static bool together;            /* SHIFT and "-" both down ... */
static absolute_time_t together_since; /* ... since then */

static int64_t ms_since(absolute_time_t t) {
    return absolute_time_diff_us(t, get_absolute_time()) / 1000;
}

/* Where "-" is on the keyboard matrix. */
static bool minus_held(void) {
    for (uint8_t r = 0; r < KB_ROWS; r++)
        for (uint8_t c = 0; c < KB_COLS; c++)
            if (free42_key_code(r, c) == FREE42_KEY_MINUS)
                return keyboard_is_held(r, c);
    return false;
}

void key_hold_begin(uint8_t row, uint8_t col) {
    code = free42_key_code(row, col);
    key_row = row;
    key_col = col;
    down_since = get_absolute_time();
    together = false;
    if (code == FREE42_KEY_SHIFT)
        screenshot_remember(); /* before SHIFT changes the screen */
}

key_hold_t key_hold_poll(void) {
    if (code == FREE42_KEY_EXIT)
        return ms_since(down_since) >= EXIT_HOLD_OFF_MS ? KEY_HOLD_OFF : KEY_HOLD_NONE;
    if (code != FREE42_KEY_SHIFT)
        return KEY_HOLD_NONE;

    if (!minus_held()) {
        together = false;
        return KEY_HOLD_NONE;
    }
    if (!together) {
        together = true;
        together_since = get_absolute_time();
    }
    if (ms_since(together_since) < SCREENSHOT_HOLD_MS)
        return KEY_HOLD_NONE;

    free42_key_cancel();
    screenshot_save();
    for (int i = 0; i < KEY_STUCK_MS / 10 && (keyboard_is_held(key_row, key_col) || minus_held()); i++) {
        watchdog_update();
        sleep_ms(10);
    }
    together = false;
    return KEY_HOLD_SCREENSHOT;
}
