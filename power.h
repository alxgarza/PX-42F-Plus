#ifndef POWER_H
#define POWER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Deep power-off: the RP2350's power manager switches off everything but
 * its always-on domain and SRAM bank 1 (0x20040000 up) -- processors,
 * clocks, peripherals, USB, the rest of SRAM -- until the key at (on_row,
 * on_col), the ON key, is pressed. Waking is a full boot from the start:
 * only what's in SRAM bank 1 and not cleared at startup survives
 * (retained.h).
 *
 * splash_until_ms = 0: the panel is put to sleep (blank) first. Otherwise
 * it keeps showing the splash already drawn on it, and the chip wakes
 * once a minute to update the splash's clock (power_splash_wake()), until
 * splash_until_ms (always-on timer time), when the panel is blanked.
 *
 * Call from core 1; never returns. Core 0 is parked first (see
 * power_core0_poll()). If the power manager refuses the request, the chip
 * reboots rather than hang. */
void power_deep_off(uint8_t on_row, uint8_t on_col, uint64_t splash_until_ms);

/* Call first thing at boot, from main() (core 0, core 1 not started). If
 * this is one of the once-a-minute splash wakes, updates the splash's
 * clock (or blanks it, its time up) and powers straight back down -- it
 * doesn't return. False for any other boot: carry on as normal. */
bool power_splash_wake(void);

/* Core 0's idle loop calls this after every wakeup: once power_deep_off()
 * has asked for it, core 0 masks all of its interrupts and sleeps for good,
 * so the processors can be powered down. */
void power_core0_poll(void);

/* This boot is the wake from a deep power-off (the ON key). */
bool power_woke_from_off(void);

#ifdef __cplusplus
}
#endif

#endif
