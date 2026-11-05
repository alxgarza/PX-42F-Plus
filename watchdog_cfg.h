#ifndef WATCHDOG_CFG_H
#define WATCHDOG_CFG_H

#include "hardware/watchdog.h"

/* Requires watchdog_update() at least this often or the chip reboots. One
 * command of Plus42's can't be cut short, its math in software: 2000 N! (two
 * thousand 128-bit decimal multiplies) reset the calculator when this was
 * 100 ms, and COMB has the same kind of loop, longer. A real hang only takes
 * this long to come back from. */
#define WATCHDOG_MS 5000

/* For stretches that legitimately can't feed the watchdog: sleeping until
 * the ON key, or a flash sector erase (which can take a few hundred ms). */
static inline void watchdog_pause(void) {
    watchdog_disable();
}

static inline void watchdog_resume(void) {
    watchdog_enable(WATCHDOG_MS, 1);
}

#endif
