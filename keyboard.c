#include "keyboard.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

static const uint kb_row_pins[KB_ROWS] = {0, 1, 2, 3, 4, 5, 6, 7};
static const uint kb_col_pins[KB_COLS] = {8, 9, 10, 11, 12};

void keyboard_init(void) {
    for (int i = 0; i < KB_ROWS; i++) {
        gpio_init(kb_row_pins[i]);
        gpio_set_dir(kb_row_pins[i], GPIO_OUT);
        gpio_put(kb_row_pins[i], 1); /* idle inactive; scan drives one row low at a time */
    }
    for (int i = 0; i < KB_COLS; i++) {
        gpio_init(kb_col_pins[i]);
        gpio_set_dir(kb_col_pins[i], GPIO_IN);
        gpio_pull_up(kb_col_pins[i]); /* internal pull-up; a pressed key reads low */
    }
}

bool keyboard_scan(uint8_t *row, uint8_t *col) {
    for (int r = 0; r < KB_ROWS; r++) {
        gpio_put(kb_row_pins[r], 0); /* drive this row active (low) */
        sleep_us(5); /* let the line settle before sampling the columns */

        for (int c = 0; c < KB_COLS; c++) {
            if (!gpio_get(kb_col_pins[c])) { /* pulled low by the pressed key */
                gpio_put(kb_row_pins[r], 1); /* restore idle before returning */
                *row = (uint8_t) r;
                *col = (uint8_t) c;
                return true;
            }
        }

        gpio_put(kb_row_pins[r], 1); /* restore idle before scanning the next row */
    }
    return false;
}

bool keyboard_is_held(uint8_t row, uint8_t col) {
    if (row >= KB_ROWS || col >= KB_COLS)
        return false;

    gpio_put(kb_row_pins[row], 0);
    sleep_us(5); /* let the line settle before sampling, same as keyboard_scan() */
    bool held = !gpio_get(kb_col_pins[col]);
    gpio_put(kb_row_pins[row], 1);
    return held;
}
