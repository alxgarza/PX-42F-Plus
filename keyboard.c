#include "keyboard.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "cpuclock.h"

static const uint kb_row_pins[KB_ROWS] = {0, 1, 2, 3, 4, 5, 6, 7};
static const uint kb_col_pins[KB_COLS] = {9, 10, 11, 12, 13};

void keyboard_init(void) {
    for (int i = 0; i < KB_ROWS; i++) {
        gpio_init(kb_row_pins[i]);
        /* The RP2350's pads reset with their pull-down ON: left on, a row
         * idling high fights it -- ~60uA per row, all the time, and through
         * a deep power-off too. Outputs need no pulls. */
        gpio_disable_pulls(kb_row_pins[i]);
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

uint64_t keyboard_held(void) {
    uint64_t held = 0;
    for (int row = 0; row < KB_ROWS; row++) {
        gpio_put(kb_row_pins[row], 0);
        sleep_us(5);
        for (int col = 0; col < KB_COLS; col++)
            if (!gpio_get(kb_col_pins[col]))
                held |= 1ull << (row * KB_COLS + col);
        gpio_put(kb_row_pins[row], 1);
    }
    return held;
}

/* Set by the column interrupt (a press that's over again by the time
 * the loop looks still counts). The alarm's handler has nothing to do:
 * taking its interrupt is what ends the WFI. */
static volatile bool key_edge = false;

static void key_edge_irq(uint gpio, uint32_t events) {
    (void) gpio;
    (void) events;
    key_edge = true;
}

static void wait_alarm_irq(uint alarm_num) {
    (void) alarm_num;
}

static bool any_column_low(void) {
    for (int c = 0; c < KB_COLS; c++)
        if (!gpio_get(kb_col_pins[c]))
            return true;
    return false;
}

bool keyboard_wait_any(absolute_time_t deadline) {
    /* All rows low: any key pulls its column low. */
    for (int r = 0; r < KB_ROWS; r++)
        gpio_put(kb_row_pins[r], 0);
    sleep_us(5);

    /* Both wake sources are interrupts taken on this core: the columns'
     * (the SDK clears stale edges when enabling), and a hardware alarm of
     * our own for the deadline -- not the SDK's alarm pool, whose interrupt
     * is on core 0 and only reaches this core as an event. */
    key_edge = false;
    for (int c = 0; c < KB_COLS; c++)
        gpio_set_irq_enabled_with_callback(kb_col_pins[c], GPIO_IRQ_EDGE_FALL, true, key_edge_irq);
    uint alarm = (uint) hardware_alarm_claim_unused(true);
    hardware_alarm_set_callback(alarm, wait_alarm_irq);
    hardware_alarm_set_target(alarm, deadline);

    /* Check-then-sleep with interrupts masked: a press (or the alarm)
     * landing between the checks and the WFI leaves its interrupt pending,
     * and a pending interrupt ends WFI even while masked -- so it can't be
     * missed. Anything else that wakes it just goes round again. */
    bool pressed;
    cpuclock_deep_sleep_begin(); /* the sleep clock gating -- cpuclock.h */
    while (true) {
        uint32_t saved = save_and_disable_interrupts();
        pressed = key_edge || any_column_low();
        bool timed_out = time_reached(deadline);
        if (!pressed && !timed_out)
            __wfi();
        restore_interrupts(saved);
        if (pressed || timed_out)
            break;
    }
    cpuclock_deep_sleep_end();

    hardware_alarm_cancel(alarm);
    hardware_alarm_set_callback(alarm, NULL);
    hardware_alarm_unclaim(alarm);
    for (int c = 0; c < KB_COLS; c++)
        gpio_set_irq_enabled(kb_col_pins[c], GPIO_IRQ_EDGE_FALL, false);
    for (int r = 0; r < KB_ROWS; r++)
        gpio_put(kb_row_pins[r], 1);

    /* Debounce, as the old 10ms polling did by its spacing: a key's
     * contacts chatter for a few ms, on press and on release (a release's
     * chatter wakes this too). keyboard_scan() then sees the settled state. */
    if (pressed)
        sleep_ms(10);
    return pressed;
}

uint keyboard_arm_wake_key(uint8_t row, uint8_t col) {
    for (int r = 0; r < KB_ROWS; r++)
        gpio_put(kb_row_pins[r], r == row ? 0 : 1);
    sleep_us(5);
    return kb_col_pins[col];
}
