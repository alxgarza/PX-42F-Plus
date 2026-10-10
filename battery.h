#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Battery sense: GP28 (ADC2) sits at the midpoint of two 1M resistors in
 * series from the lithium cell to GND, so it reads half the cell voltage.
 * Call once at startup, before any other battery_* function. */
void battery_init(void);

/* Cell voltage in millivolts, averaged over several ADC samples. */
int battery_read_mv(void);

/* Charge level for the header icon, 0 (empty) to 4 (full) -- see the
 * thresholds in battery.c. Takes a fresh reading each call, with
 * hysteresis so a voltage sitting right on a threshold doesn't make the
 * icon flicker between two levels. */
int battery_level(void);

/* True once the cell is below BATTERY_LOW_MV (Free42's flag 49). */
bool battery_low(void);

/* Call every 10 seconds or so: true once the cell has read below
 * BATTERY_EMPTY_MV three times running -- time to power off, while there's
 * still enough to save the memory. */
bool battery_empty(void);

#ifdef __cplusplus
}
#endif

#endif
