#include "idle.h"

#include "pico/time.h"
#include "tusb.h"
#include "cpuclock.h"
#include "keyboard.h"
#include "watchdog_cfg.h"
#include "debug_checkpoint.h"

/* The longest the battery wait sleeps without looking round. */
#define IDLE_WAKE_MAX_MS 1000

/* The connected-to-a-computer poll interval. */
#define USB_POLL_MS 10

static uint32_t waits, key_waits;

/* The board can't sense VBUS, but this is what matters anyway: a phone
 * charger powers it without ever getting this far, and an unplugged cable
 * or a sleeping computer stops the bus's 1ms frames, which TinyUSB reports
 * as suspend within a few ms. Reads TinyUSB's state only -- fine from this
 * core. */
bool idle_usb_host_active(void) {
    return tud_mounted() && !tud_suspended();
}

absolute_time_t idle_earliest(absolute_time_t until, absolute_time_t other) {
    return absolute_time_diff_us(other, until) > 0 ? other : until;
}

bool idle_wait(absolute_time_t until) {
    if (idle_usb_host_active()) {
        cpuclock_set_fast(true); /* power doesn't matter on USB */
        sleep_until(idle_earliest(until, make_timeout_time_ms(USB_POLL_MS)));
        return false;
    }

    cpuclock_set_fast(false);
    until = idle_earliest(until, make_timeout_time_ms(IDLE_WAKE_MAX_MS));
    debug_checkpoint(CKPT_IDLE_WAIT, (uint32_t) (absolute_time_diff_us(get_absolute_time(), until) / 1000));
    watchdog_enable(IDLE_WAKE_MAX_MS + WATCHDOG_MS, 1); /* the wait is longer than the usual window */
    waits++;
    bool key = keyboard_wait_any(until);
    if (key)
        key_waits++;
    watchdog_resume();
    return key;
}

void idle_stats(uint32_t *w, uint32_t *k) {
    *w = waits;
    *k = key_waits;
}
