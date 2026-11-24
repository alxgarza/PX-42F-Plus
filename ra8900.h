#ifndef RA8900_H
#define RA8900_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The Epson RA8900CE (X1B000271A00500, "UB": +-5 ppm, -40..85 C), the
 * temperature-compensated clock chip of the PX-42F V1.1 board, on I2C.
 * Its registers' rules, kept free of hardware for tests/run.sh; rtc.c
 * talks to it. Epson's application manual: ETM44E. */

#define RA8900_ADDR 0x32 /* 7 bits: 0110 010 */

#define RA8900_SEC   0x00 /* 00-06: SEC MIN HOUR WEEK DAY MONTH YEAR, BCD */
#define RA8900_TIMER 0x0B /* 0B-0C: the countdown's start, 12 bits */
#define RA8900_EXT   0x0D /* TEST WADA USEL TE FSEL1 FSEL0 TSEL1 TSEL0 */
#define RA8900_FLAG  0x0E /* - - UF TF AF - VLF VDET; only 0 can be written */
#define RA8900_CTRL  0x0F /* CSEL1 CSEL0 UIE TIE AIE - - RESET */

#define RA8900_EXT_TE       0x10 /* the countdown runs */
#define RA8900_EXT_TSEL_SEC 0x02 /* it counts the seconds' turning */
#define RA8900_EXT_TSEL_MIN 0x03 /* ... the minutes' */
#define RA8900_FLAG_UF  0x20 /* a second turned (UIE) */
#define RA8900_FLAG_TF  0x10 /* the countdown reached 0 */
#define RA8900_FLAG_AF  0x08
#define RA8900_FLAG_VLF 0x02 /* the time was lost: low voltage, the crystal stopped, a power-on */
#define RA8900_CTRL_CSEL 0x40 /* temperature compensation every 2 s, the default */
#define RA8900_CTRL_UIE  0x20 /* /INT at each second's turning */
#define RA8900_CTRL_TIE  0x10 /* /INT when the countdown ends */

/* Written when the time is set: the extension register at its power-on
 * value (TSEL1; timer off, FOUT 32 kHz if FOE lets it out); every flag
 * cleared, VLF above all; the temperature compensated every 2 s (CSEL 01,
 * the default), no interrupts, and RESET: the divider below the seconds
 * starts over at the STOP that ends the write, so the next second comes
 * 1000 ms later. */
#define RA8900_EXT_SET  0x02
#define RA8900_FLAG_SET 0x00
#define RA8900_CTRL_SET 0x41

/* The seven time registers for ms since 1970 (the second it's in). False
 * outside the years the chip counts, 2000-2099. */
bool ra8900_encode(uint64_t ms, uint8_t regs[7]);

/* Back from the seven registers: ms since 1970, the start of the second.
 * False if they don't hold a real time (bad BCD, a 61st second, Feb 30):
 * the chip lost it. The day of the week isn't looked at. */
bool ra8900_decode(const uint8_t regs[7], uint64_t *ms);

/* The countdown that wakes the calculator while off: /INT goes low for
 * 7.8 ms as the chip's second (or minute) turns, when it reaches 0. Counting
 * seconds, 1-4095 of them (68 min); further off, minutes, to the last one
 * that turns before wake_ms -- the wake then sets the seconds' countdown
 * for the rest. A turn less than RA8900_WAKE_MARGIN_MS away is skipped: it
 * could come before the power-down is done, and the power manager refuses
 * to power down with a wake pending. Never after wake_ms (rounded up to
 * its second), except by that skipped turn. */
#define RA8900_WAKE_MARGIN_MS 100
typedef struct {
    uint8_t tsel;   /* RA8900_EXT_TSEL_SEC or _MIN */
    uint16_t count; /* 1-4095 */
} ra8900_timer_t;
ra8900_timer_t ra8900_wake_timer(uint64_t now_ms, uint64_t wake_ms);

/* The chip counts whole seconds: where clock_ms (our own clock, which keeps
 * the milliseconds) belongs, given that the chip says it's chip_ms. In
 * that second it's left alone; outside it, moved to its nearer end -- the
 * smallest correction the reading allows. */
uint64_t ra8900_settle(uint64_t clock_ms, uint64_t chip_ms);

#ifdef __cplusplus
}
#endif

#endif
