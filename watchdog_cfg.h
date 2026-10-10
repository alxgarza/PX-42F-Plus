#ifndef WATCHDOG_CFG_H
#define WATCHDOG_CFG_H

#include "hardware/watchdog.h"

/* Requires watchdog_update() at least this often or the chip reboots. */
#define WATCHDOG_MS 100

/* For stretches that legitimately can't feed the watchdog: sleeping until
 * the ON key, or a flash sector erase (which can take a few hundred ms). */
static inline void watchdog_pause(void) {
    watchdog_disable();
}

static inline void watchdog_resume(void) {
    watchdog_enable(WATCHDOG_MS, 1);
}

#endif
