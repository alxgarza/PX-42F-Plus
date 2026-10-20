#include "rtc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "hardware/clocks.h"
#include "hardware/powman.h"
#include "hardware/watchdog.h"
#include "hardware/timer.h"
#include "settings.h"
#include "cpuclock.h"

/* Where the clock starts after a power-on (it has no battery backup):
 * 2021-03-05 00:59:00. */
#define RTC_DEFAULT_YEAR   2021
#define RTC_DEFAULT_MONTH  3
#define RTC_DEFAULT_DAY    5
#define RTC_DEFAULT_HOUR   0
#define RTC_DEFAULT_MINUTE 59

#define MS_PER_DAY (86400ull * 1000ull)

/* Days since 1970-01-01 for a Gregorian date, and back. Algorithms from
 * Howard Hinnant's "chrono-Compatible Low-Level Date Algorithms": they
 * work in 400-year eras (146097 days each), which builds the full leap
 * rule in -- every 4th year, except centuries, except every 400th. */
static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int) (y - era * 400);                               /* 0-399 */
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;      /* 0-365, counted from March 1 */
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;               /* 0-146096 */
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = (int) (z - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int) (yoe + era * 400) + (*m <= 2);
}

static bool is_leap_year(int y) {
    return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
}

static int days_in_month(int y, int m) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && is_leap_year(y) ? 29 : days[m - 1];
}

/* A rate in Hz with the trim applied: telling the clock its source runs
 * a little slower than it does makes it count faster, and vice versa. */
static uint32_t trimmed(uint32_t hz) {
    return (uint32_t) ((int64_t) hz - (int64_t) hz * px_settings.time_trim / 864000);
}

void rtc_apply_trim(void) {
    powman_timer_set_1khz_tick_source_xosc_with_hz(trimmed(XOSC_HZ));
}

void rtc_clock_init(void) {
    /* Ticks from the 12MHz crystal, not the internal low-power RC
     * oscillator: the LPOSC isn't trimmed to a crystal and can drift by
     * minutes per day, too much for a calendar clock. The crystal keeps
     * running through this firmware's sleep (WFI, not dormant mode). */
    rtc_apply_trim();

    /* Powman sits in the always-on power domain, so a watchdog or soft
     * reset can leave it running with the correct time -- only start it
     * over from the default after an actual power-on. */
    if (!powman_timer_is_running()) {
        powman_timer_set_ms((uint64_t) days_from_civil(RTC_DEFAULT_YEAR, RTC_DEFAULT_MONTH, RTC_DEFAULT_DAY) * MS_PER_DAY
                            + ((uint64_t) RTC_DEFAULT_HOUR * 3600 + RTC_DEFAULT_MINUTE * 60) * 1000);
        powman_timer_start();
    }
}

void rtc_get_datetime(rtc_datetime_t *dt) {
    uint64_t now = powman_timer_get_ms();
    int64_t days = (int64_t) (now / MS_PER_DAY);
    uint32_t ms_of_day = (uint32_t) (now % MS_PER_DAY);

    civil_from_days(days, &dt->year, &dt->month, &dt->day);
    dt->hour = (int) (ms_of_day / 3600000);
    dt->min = (int) (ms_of_day / 60000 % 60);
    dt->sec = (int) (ms_of_day / 1000 % 60);
    dt->ms = (int) (ms_of_day % 1000);
    dt->weekday = (int) ((days + 4) % 7); /* 1970-01-01 was a Thursday */
}

void rtc_get_hms(int *hour, int *min, int *sec) {
    rtc_datetime_t dt;
    rtc_get_datetime(&dt);
    *hour = dt.hour;
    *min = dt.min;
    *sec = dt.sec;
}

/* The clock was just set from old_ms to new_ms: if that was a correction
 * (under 30 minutes) at least 6 hours after the last setting, it measures
 * the drift. Either way, this is the new reference. */
static bool measured_now;

bool rtc_set_measured(void) {
    return measured_now;
}

static void note_time_set(uint64_t old_ms, uint64_t new_ms) {
    int64_t error_ms = (int64_t) (old_ms - new_ms); /* + = the clock was fast */
    int64_t span_ms = (int64_t) (new_ms - px_settings.cal_ref_ms);
    measured_now = px_settings.cal_ref_ms != 0 && llabs(error_ms) < 30 * 60000 && span_ms >= 6 * 3600000LL;
    if (measured_now) {
        int64_t drift = error_ms * 864000 / span_ms; /* tenths of a second per day */
        px_settings.cal_drift = (int16_t) (drift > 32767 ? 32767 : drift < -32767 ? -32767 : drift);
        px_settings.cal_span_h = (uint16_t) (span_ms / 3600000 > 65535 ? 65535 : span_ms / 3600000);
    }
    px_settings.cal_ref_ms = new_ms;
    settings_save_later();
}

void rtc_set_hms(int hour, int min, int sec) {
    uint64_t old_ms = powman_timer_get_ms();
    uint64_t days = old_ms / MS_PER_DAY;
    uint64_t ms_of_day = ((uint64_t) hour * 3600 + (uint64_t) min * 60 + (uint64_t) sec) * 1000;
    uint64_t new_ms = days * MS_PER_DAY + ms_of_day;
    powman_timer_set_ms(new_ms);
    note_time_set(old_ms, new_ms);
}

bool rtc_set_date(int year, int month, int day) {
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > days_in_month(year, month))
        return false;
    uint64_t old_ms = powman_timer_get_ms();
    uint64_t new_ms = (uint64_t) days_from_civil(year, month, day) * MS_PER_DAY + old_ms % MS_PER_DAY;
    powman_timer_set_ms(new_ms);
    /* Whole days moved: the calibration reference moves with them. */
    if (px_settings.cal_ref_ms != 0 && new_ms != old_ms) {
        px_settings.cal_ref_ms += new_ms - old_ms;
        settings_save_later();
    }
    return true;
}

/* ===================== formats ===================== */

static const char *const month_names[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                            "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char *const weekday_names[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

void rtc_format_date(char *buf, size_t size, const rtc_datetime_t *dt) {
    switch (px_settings.date_format) {
        case RTC_WMD:
            snprintf(buf, size, "%s-%s-%d", weekday_names[dt->weekday], month_names[dt->month - 1], dt->day);
            break;
        case RTC_DMY:
            snprintf(buf, size, "%02d.%02d.%04d", dt->day, dt->month, dt->year);
            break;
        case RTC_MDY:
            snprintf(buf, size, "%02d/%02d/%04d", dt->month, dt->day, dt->year);
            break;
        default:
            snprintf(buf, size, "%04d-%02d-%02d", dt->year, dt->month, dt->day);
            break;
    }
}

void rtc_format_day_month(char *buf, size_t size, const rtc_datetime_t *dt) {
    switch (px_settings.date_format) {
        case RTC_WMD: /* short already: the whole "FRI-OCT-9" */
            rtc_format_date(buf, size, dt);
            break;
        case RTC_DMY:
            snprintf(buf, size, "%02d.%02d", dt->day, dt->month);
            break;
        case RTC_MDY:
            snprintf(buf, size, "%02d/%02d", dt->month, dt->day);
            break;
        default:
            snprintf(buf, size, "%02d-%02d", dt->month, dt->day);
            break;
    }
}

void rtc_format_stamp(char *buf, size_t size, const rtc_datetime_t *dt) {
    snprintf(buf, size, "%04d%02d%02d-%02d%02d%02d", dt->year, dt->month, dt->day, dt->hour, dt->min, dt->sec);
}

void rtc_format_time(char *buf, size_t size, const rtc_datetime_t *dt, bool compact) {
    if (px_settings.clock_24h) {
        snprintf(buf, size, "%02d:%02d", dt->hour, dt->min);
        return;
    }
    int h = dt->hour % 12 == 0 ? 12 : dt->hour % 12;
    bool pm = dt->hour >= 12;
    snprintf(buf, size, compact ? "%d:%02d%s" : "%d:%02d %s", h, dt->min, pm ? "PM" : "AM");
}

/* ===================== the low-power oscillator ===================== */

/* The frequency counter, against clk_ref (the crystal): 1/32kHz steps, so
 * one reading is good to ~0.1% of the LPOSC's ~32kHz. Averaging 8 readings,
 * each over the longest window (~33ms), gets well below that. */
#define LPOSC_READINGS 8
/* At a wake while off: two (~70ms), for the cost of the wake -- ~0.1%. */
#define LPOSC_QUICK_READINGS 2

/* How much of the straight line through the two measurements the rate
 * follows below them (rtc_lposc_hz_at()), in percent: measured on the
 * PX-42F, Oct 2026. */
#define LPOSC_CURVE_PERCENT 72

/* One measurement at the present core voltage, untrimmed: the average of
 * readings readings. */
static uint32_t measure_lposc(int readings) {
    fc_hw_t *fc = &clocks_hw->fc0;
    uint64_t sum = 0; /* in 1/32 kHz */
    busy_wait_ms(2); /* the oscillator settling, after a voltage change */
    for (int i = 0; i < readings; i++) {
        watchdog_update(); /* the whole measurement outlasts the 100ms window */
        while (fc->status & CLOCKS_FC0_STATUS_RUNNING_BITS)
            tight_loop_contents();
        fc->ref_khz = clock_get_hz(clk_ref) / 1000;
        fc->interval = 15;
        fc->min_khz = 0;
        fc->max_khz = 0xffffffff;
        fc->src = CLOCKS_FC0_SRC_VALUE_LPOSC_CLKSRC; /* starts it */
        while (!(fc->status & CLOCKS_FC0_STATUS_DONE_BITS))
            tight_loop_contents();
        sum += fc->result;
    }
    uint32_t hz = (uint32_t) (sum * 1000 / (32 * (uint64_t) readings));
    if (hz < 20000 || hz > 50000)
        hz = 32768; /* a reading that can't be right: the nominal rate */
    return hz;
}

uint32_t rtc_lposc_hz_at(int mv, uint32_t *at_fast, uint32_t *at_slow) {
    cpuclock_set_fast(true);
    uint32_t fast = measure_lposc(LPOSC_READINGS);
    cpuclock_set_fast(false);
    uint32_t slow = measure_lposc(LPOSC_READINGS);
    if (at_fast != NULL)
        *at_fast = fast;
    if (at_slow != NULL)
        *at_slow = slow;
    /* Below the slow voltage the rate climbs less steeply than between the
     * two measured: off at 0.90V, the clock ran 0.8% slow with a straight
     * line (twice, 3.7h and 5h), which puts the curve at 72% of it. */
    int64_t hz = (int64_t) slow
                 + ((int64_t) slow - fast) * (CPUCLOCK_SLOW_MV - mv) * LPOSC_CURVE_PERCENT
                           / ((CPUCLOCK_FAST_MV - CPUCLOCK_SLOW_MV) * 100);
    if (hz < 20000 || hz > 50000)
        hz = slow; /* a line that can't be right: the nearest measurement */
    return trimmed((uint32_t) hz);
}

uint32_t rtc_lposc_hz_again(uint32_t off_hz, uint32_t slow_hz) {
    cpuclock_set_fast(false);
    uint32_t now = measure_lposc(LPOSC_QUICK_READINGS);
    if (off_hz == 0 || slow_hz == 0)
        return 0; /* nothing to scale: keep the rate set */
    return (uint32_t) ((uint64_t) off_hz * now / slow_hz);
}
