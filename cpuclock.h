#ifndef CPUCLOCK_H
#define CPUCLOCK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Two system clock speeds, to save power while the calculator waits for a
 * key: fast, 150MHz from the system PLL (the speed it boots at), for work
 * -- a keypress being handled, a program running; and slow, 48MHz taken
 * from the USB PLL (which runs anyway, for USB), with the system PLL
 * stopped and the core voltage lowered from 1.10V to 1.00V, the rest of
 * the time.
 *
 * Clock gating, set up by cpuclock_init(): blocks the firmware never uses
 * (HSTX, PIO, I2C, the second SPI and UART, DMA, ...) get no clock at all,
 * and while both cores sleep, only what wakes them or must keep running --
 * the timer, the GPIO interrupts, USB, the always-on timer, the watchdog,
 * and the processors' memory system -- keeps its clock. That needs both
 * cores to sleep "deep", which they only do during core 1's idle wait
 * (cpuclock_deep_sleep_begin()).
 *
 * The peripheral clock (SPI, UART) is moved to the USB PLL's 48MHz for
 * good by cpuclock_init(), so it doesn't change with the switching: the
 * display's SPI and the UART's baud rate stay put. The speaker's PWM runs
 * on the system clock, but each tone works out its divider from the
 * current speed, and switching only happens between tones (both on core 1). */

/* Call once from main(), before any peripheral is set up. */
void cpuclock_init(void);

/* Core 1, around its idle wait (and nothing else): deep sleep for both
 * cores, which is what lets the sleep clock gating happen. end() returns
 * once core 0 has stopped sleeping deep too. */
void cpuclock_deep_sleep_begin(void);
void cpuclock_deep_sleep_end(void);

/* Core 0's idle loop calls this before each WFE: deep or not, as core 1
 * asks. */
void cpuclock_core0_before_sleep(void);

/* Core 1 only. Cheap when the speed is already the one asked for. */
void cpuclock_set_fast(bool fast);

#ifdef __cplusplus
}
#endif

#endif
