#ifndef POWER_H
#define POWER_H

#include <stdbool.h>
#include <stdint.h>

/* The low-power regulator's voltage while off, in mV (power.c). */
#define POWER_OFF_MV 900

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
 * once a minute to update the splash's clock (power_off_wake()), until
 * splash_until_ms (always-on timer time), when the panel is blanked. Either
 * way it also wakes every 10 minutes to measure the oscillator that keeps
 * the time while off.
 *
 * Call from core 1; never returns. Core 0 is parked first (see
 * power_core0_poll()). If the power manager refuses the request, the chip
 * reboots rather than hang. */
void power_deep_off(uint8_t on_row, uint8_t on_col, uint64_t splash_until_ms);

/* Call first thing at boot, from main() (core 0, core 1 not started). If
 * this is one of the timer wakes while off, measures the clock's oscillator
 * again, updates the splash's clock (or blanks it, its time up) and powers
 * straight back down -- it doesn't return. False for any other boot: carry
 * on as normal. */
bool power_off_wake(void);

/* Core 0's idle loop calls this after every wakeup: once power_deep_off()
 * has asked for it, core 0 masks all of its interrupts and sleeps for good,
 * so the processors can be powered down. */
void power_core0_poll(void);

/* This boot is the wake from a deep power-off (the ON key) -- as
 * power_off_wake(), first thing at boot, found it. */
bool power_woke_from_off(void);

/* Restarts into the boot ROM's update mode, as BOOT + RESET does: the
 * RP2350 drive, which takes a .uf2. Settings -> USB Drive -> Update
 * Firmware, on core 1. Never returns. */
void power_update_mode(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
