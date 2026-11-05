#ifndef TIMEMODULE_H
#define TIMEMODULE_H

#include <stdbool.h>
#include <stdint.h>

#include "rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The HP-41 Time Module's numbers -- kept free of hardware for tests/run.sh. */

/* The calendar (proleptic Gregorian, days since 1970). */
int64_t time_days_from_civil(int y, int m, int d);
void time_civil_from_days(int64_t z, int *y, int *m, int *d);
int time_days_in_month(int y, int m);
void time_ms_to_datetime(uint64_t ms, rtc_datetime_t *dt);

/* HHHH.MMSSss, its digits as an integer (the number x 1000000), to ms. False
 * if the minutes or seconds are over 59. */
bool time_digits_to_ms(int64_t digits, int64_t *ms);

/* The other way: ms to those digits, to the hundredth of a second. */
int64_t time_ms_to_digits(uint64_t ms);

/* XYZALM's time in X: as above but at most 23.595999, negative for p.m. */
bool alarm_time_to_ms(int64_t digits, bool negative, int64_t *ms);

/* XYZALM's date in Y in the calculator's format, the number x 1000000, and
 * back: MM.DDYYYY, DD.MMYYYY or YYYY.MMDD. False if it isn't a real date from
 * 1970 (the clock starts there; the HP-41's began in 1900) to 2199. */
typedef enum { TIME_MDY, TIME_DMY, TIME_YMD } time_date_order_t;
bool alarm_date_to_days(int64_t digits, time_date_order_t order, int64_t *days);
int64_t alarm_days_to_digits(int64_t days, time_date_order_t order);

/* The accuracy factor isn't a rate: one pulse of 1/10240 s is added (+) or
 * taken away (-) every AF seconds (HP-41CX manual, appendix F), so AF 0.1 is
 * 84.4 s/day and AF 99.9 is 0.08 s/day. In tenths of AF, on SETAF's rules: from
 * a value other than zero, x 1000: over 99.95 is 0, under 0.1 is 0.1. */
int af_tenths_from_milli(int64_t milli);

/* Between that (with its sign) and px_settings.time_trim, tenths of a second
 * per day. Not exact: the trim's steps are coarse where AF is large. */
int af_tenths_to_trim(int af_tenths);
int trim_to_af_tenths(int trim);

#ifdef __cplusplus
}
#endif

#endif
