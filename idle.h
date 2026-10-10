#ifndef IDLE_H
#define IDLE_H

#include <stdbool.h>
#include <stdint.h>

#include "pico/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Waiting for the next key, the same way everywhere (the calculator's main
 * loop, the settings menu), core 1.
 *
 * On battery: at 48MHz, asleep until a key goes down (an interrupt) or
 * `until`, whichever comes first -- and at least once a second, to notice
 * a computer being plugged in and for timers the caller checks itself.
 * Connected to a computer: full speed, for up to 10ms (polling keeps the
 * console and the USB drive responsive). Either way the caller then scans
 * the keyboard and checks what it's waiting for. True if a key went down. */
bool idle_wait(absolute_time_t until);

/* A computer has set the calculator up as a USB device (the console, drive
 * and printer are usable) and hasn't suspended it. */
bool idle_usb_host_active(void);

/* For !STATUS: idle waits on battery, and how many a key ended. */
void idle_stats(uint32_t *waits, uint32_t *key_waits);

/* Whichever of the two times comes first -- for building the deadline
 * passed to idle_wait(). */
absolute_time_t idle_earliest(absolute_time_t until, absolute_time_t other);

#ifdef __cplusplus
}
#endif

#endif
