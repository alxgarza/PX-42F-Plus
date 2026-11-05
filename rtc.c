#include "rtc.h"
#include "timemodule.h"
#include "stopwatch.h"

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
        powman_timer_set_ms((uint64_t) time_days_from_civil(RTC_DEFAULT_YEAR, RTC_DEFAULT_MONTH, RTC_DEFAULT_DAY) * MS_PER_DAY
                            + ((uint64_t) RTC_DEFAULT_HOUR * 3600 + RTC_DEFAULT_MINUTE * 60) * 1000);
        powman_timer_start();
    }
}

uint64_t rtc_now_ms(void) {
    return powman_timer_get_ms();
}

void rtc_get_datetime(rtc_datetime_t *dt) {
    time_ms_to_datetime(powman_timer_get_ms(), dt);
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

void rtc_set_ms_of_day(uint32_t ms) {
    uint64_t old_ms = powman_timer_get_ms();
    uint64_t new_ms = old_ms / MS_PER_DAY * MS_PER_DAY + ms;
    powman_timer_set_ms(new_ms);
    stopwatch_clock_moved((int64_t) (new_ms - old_ms));
    note_time_set(old_ms, new_ms);
}

void rtc_set_hms(int hour, int min, int sec) {
    rtc_set_ms_of_day((uint32_t) (hour * 3600 + min * 60 + sec) * 1000);
}

bool rtc_add_ms(int64_t ms) {
    uint64_t old_ms = powman_timer_get_ms();
    if (ms < 0 && (uint64_t) -ms > old_ms)
        return false;
    powman_timer_set_ms(old_ms + (uint64_t) ms);
    stopwatch_clock_moved(ms);
    if (px_settings.cal_ref_ms != 0) {
        px_settings.cal_ref_ms += (uint64_t) ms;
        settings_save_later();
    }
    return true;
}

void rtc_set_trim(int tenths) {
    px_settings.time_trim = (int16_t) (tenths > 9999 ? 9999 : tenths < -9999 ? -9999 : tenths);
    rtc_apply_trim();
}

void rtc_measure_from_now(void) {
    px_settings.cal_ref_ms = powman_timer_get_ms();
    px_settings.cal_span_h = 0;
    settings_save_later();
}

bool rtc_set_date(int year, int month, int day) {
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > time_days_in_month(year, month))
        return false;
    uint64_t old_ms = powman_timer_get_ms();
    uint64_t new_ms = (uint64_t) time_days_from_civil(year, month, day) * MS_PER_DAY + old_ms % MS_PER_DAY;
    powman_timer_set_ms(new_ms);
    stopwatch_clock_moved((int64_t) (new_ms - old_ms));
    /* Whole days moved: the calibration reference moves with them. */
    if (px_settings.cal_ref_ms != 0 && new_ms != old_ms) {
        px_settings.cal_ref_ms += new_ms - old_ms;
        settings_save_later();
    }
    return true;
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
