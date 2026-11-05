#ifndef RTC_H
#define RTC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Calendar clock on the RP2350's Powman timer (there is no classic
 * hardware_rtc peripheral on RP2350 -- that's an RP2040-only block).
 * Powman counts milliseconds; this module treats the count as time since
 * 1970-01-01 00:00:00 and converts to/from the Gregorian calendar,
 * leap years included. No battery backup: after a power-on the clock
 * starts at 2021-03-05 00:59:00 (RTC_DEFAULT_* in rtc.c). Call once at
 * startup, before any other rtc_* function. */
void rtc_clock_init(void);

typedef struct {
    int year;    /* e.g. 2026 */
    int month;   /* 1-12 */
    int day;     /* 1-31 */
    int hour;    /* 0-23 */
    int min;     /* 0-59 */
    int sec;     /* 0-59 */
    int ms;      /* 0-999 */
    int weekday; /* 0 = Sunday ... 6 = Saturday */
} rtc_datetime_t;

void rtc_get_datetime(rtc_datetime_t *dt);

/* The clock as it counts: ms since 1970. */
uint64_t rtc_now_ms(void);

/* Current time of day. hour is 0-23. */
void rtc_get_hms(int *hour, int *min, int *sec);

/* Sets the time of day, keeping the date. hour is 0-23. */
void rtc_set_hms(int hour, int min, int sec);

/* The same to the hundredth of a second (CORRECT): ms since midnight. */
void rtc_set_ms_of_day(uint32_t ms);

/* Moves the clock forward or back (T+X), into another day if it comes to that;
 * not drift, so the calibration's reference moves with it. False (and changes
 * nothing) if that's before 1970. */
bool rtc_add_ms(int64_t ms);

/* Sets the date, keeping the time of day. Returns false (and changes
 * nothing) if it isn't a real calendar date, e.g. Feb 29 in a non-leap
 * year, or is before 1970. */
bool rtc_set_date(int year, int month, int day);

/* ===================== formats ===================== */

/* The Date Format setting: Free42's YMD / DMY / MDY modes, and RTC_WMD,
 * DOW-MON-DAY -- day of the week, month, day ("FRI-OCT-9") -- MDY to
 * Free42. */
typedef enum { RTC_YMD, RTC_DMY, RTC_MDY, RTC_WMD } rtc_date_format_t;

/* In px_settings' formats (settings.h): "2026-10-09", "09.10.2026",
 * "10/09/2026", "FRI-OCT-9"; for the header, the day and month alone
 * ("10-09", "09.10", "10/09" -- DOW-MON-DAY whole: "FRI-OCT-9"); and
 * "15:43" or, 12-hour, "3:43 PM" -- compact (no space): "3:43PM". */
void rtc_format_date(char *buf, size_t size, const rtc_datetime_t *dt);
void rtc_format_day_month(char *buf, size_t size, const rtc_datetime_t *dt);
void rtc_format_time(char *buf, size_t size, const rtc_datetime_t *dt, bool compact);

/* What CLOCK shows: "3:15:07 PM" or "15:15:07"; with the date, no seconds:
 * "3:15 PM 01/21" or "15:15 01/21". */
void rtc_format_clock(char *buf, size_t size, const rtc_datetime_t *dt, bool with_date);

/* "20261009-154312": names made from the time (states, screenshots) sort
 * oldest first. */
void rtc_format_stamp(char *buf, size_t size, const rtc_datetime_t *dt);

/* ===================== calibration ===================== */

/* The clock counts from the 12MHz crystal while the calculator is on, and
 * from the chip's low-power oscillator while it's off -- that one is
 * measured against the crystal at each power-off (rtc_lposc_hz_at()). On top
 * of both, px_settings.time_trim: tenths of a second per day added (Time
 * Calibration in the menu).
 *
 * Each time the clock is set to the second (rtc_set_hms() -- the menu,
 * !SETTIME, Free42's SETIME), the error since the last such setting is
 * measured: px_settings.cal_drift / cal_span_h, for Time Calibration to
 * offer. Errors of 30 minutes or more aren't drift (a time zone, a clock
 * that was never set): they just start over. (Saved at OFF: a program
 * running SETIME must not wear the flash out.) */

/* Whether the last rtc_set_hms() measured the drift (cal_drift/cal_span_h). */
bool rtc_set_measured(void);

/* Applies px_settings.time_trim to the running clock (after changing it). */
void rtc_apply_trim(void);

/* Sets it, at most 999.9 s/day either way, and applies it. */
void rtc_set_trim(int tenths);

/* Measures the drift from now on: the rate has just changed (SETAF). */
void rtc_measure_from_now(void);

/* The low-power oscillator's rate at core voltage mv, with the trim
 * applied: the rate to give the clock for a power-off (at POWER_OFF_MV).
 * It's an RC oscillator whose rate changes with its supply voltage (RP2350
 * datasheet 8.4.1: by up to 20%), and while off that's lower than any the
 * processor runs at: measured at 1.10V alone, the clock gained 2.4% while
 * off (13 hours). So it's measured against the crystal at both of the
 * processor's voltages (cpuclock.h) and extended to mv -- along 72% of the
 * straight line through them, as measured with the clock while off.
 * ~0.5s, feeding the watchdog; leaves the processor at the slow speed.
 * at_fast and at_slow (or NULL) get the two measurements, untrimmed. */
uint32_t rtc_lposc_hz_at(int mv, uint32_t *at_fast, uint32_t *at_slow);

/* At a timer wake while off: the rate again, for the temperature now --
 * measured quickly (~70ms) at the slow voltage and scaled like the
 * power-off's measurement: off_hz, the rate rtc_lposc_hz_at() gave, and
 * slow_hz, its reading at the slow voltage. 0 (keep the rate) without
 * them. Leaves the processor at the slow speed. */
uint32_t rtc_lposc_hz_again(uint32_t off_hz, uint32_t slow_hz);

#ifdef __cplusplus
}
#endif

#endif
