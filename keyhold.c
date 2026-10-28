#include "keyhold.h"

#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "free42_app.h"
#include "keyboard.h"
#include "screenshot.h"
#include "sounds.h"

static int code;                 /* the key down, as free42_key_code() */
static uint8_t key_row, key_col;
static absolute_time_t down_since;
static bool together;            /* SHIFT and "-" both down ... */
static absolute_time_t together_since; /* ... since then */
static bool arrow_typed;         /* an arrow went down too soon after SHIFT: SHIFT *then* the arrow */

static int64_t ms_since(absolute_time_t t) {
    return absolute_time_diff_us(t, get_absolute_time()) / 1000;
}

/* Whether the key with free42_key_code() `key` is down, wherever it is
 * on the keyboard matrix. */
static bool key_held(int key) {
    for (uint8_t r = 0; r < KB_ROWS; r++)
        for (uint8_t c = 0; c < KB_COLS; c++)
            if (free42_key_code(r, c) == key)
                return keyboard_is_held(r, c);
    return false;
}

static bool minus_held(void) {
    return key_held(FREE42_KEY_MINUS);
}

/* SHIFT held and an arrow pressed, in ALPHA: BST / SST (DM42), done here
 * while the arrow is down -- BST again and again if it stays down, as the
 * HP-42S's does. Waiting for it to come up means the main loop never sees
 * it as a key of its own, after SHIFT. False if it's not that -- an arrow
 * going down within SHIFT_ARROW_HOLD_MS of SHIFT is quick typing that
 * overlapped, SHIFT *then* the arrow: left to the main loop, after SHIFT. */
static bool shift_arrow(void) {
    int arrow = key_held(FREE42_KEY_UP) ? FREE42_KEY_UP : key_held(FREE42_KEY_DOWN) ? FREE42_KEY_DOWN : 0;
    if (arrow == 0 || arrow_typed)
        return false;
    if (ms_since(down_since) < SHIFT_ARROW_HOLD_MS) {
        arrow_typed = true;
        return false;
    }
    if (!free42_shift_arrow(arrow))
        return false;
    sounds_key_play(KEY_SOUND_ENTRY);
    absolute_time_t next = make_timeout_time_ms(500);
    for (int i = 0; i < KEY_STUCK_MS / 10 && key_held(arrow); i++) {
        watchdog_update();
        if (arrow == FREE42_KEY_UP && time_reached(next)) {
            free42_shift_arrow(arrow);
            next = make_timeout_time_ms(200);
        }
        sleep_ms(10);
    }
    return true;
}

void key_hold_begin(uint8_t row, uint8_t col) {
    code = free42_key_code(row, col);
    key_row = row;
    key_col = col;
    down_since = get_absolute_time();
    together = false;
    arrow_typed = false;
    if (code == FREE42_KEY_SHIFT)
        screenshot_remember(); /* before SHIFT changes the screen */
}

/* For apps (app.c): the screenshot gesture alone, SHIFT and "-" held
 * together, while SHIFT is the key last pressed. Not the core's part: no
 * SHIFT cancel (free42_key_cancel()) and no BST/SST. True when it has
 * taken the screenshot, and waited for both keys to come up. */
bool key_hold_app_screenshot(void) {
    if (code != FREE42_KEY_SHIFT || !keyboard_is_held(key_row, key_col) || !minus_held()) {
        together = false;
        return false;
    }
    if (!together) {
        together = true;
        together_since = get_absolute_time();
    }
    if (ms_since(together_since) < SCREENSHOT_HOLD_MS)
        return false;
    screenshot_save();
    for (int i = 0; i < KEY_STUCK_MS / 10 && (keyboard_is_held(key_row, key_col) || minus_held()); i++) {
        watchdog_update();
        sleep_ms(10);
    }
    together = false;
    return true;
}

key_hold_t key_hold_poll(void) {
    if (code == FREE42_KEY_EXIT)
        return ms_since(down_since) >= EXIT_HOLD_OFF_MS ? KEY_HOLD_OFF : KEY_HOLD_NONE;
    if (code != FREE42_KEY_SHIFT)
        return KEY_HOLD_NONE;
    if (shift_arrow())
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
