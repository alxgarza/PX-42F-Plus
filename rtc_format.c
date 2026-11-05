/* The clock's dates and times as text, in the formats chosen in Settings ->
 * Date & Time (px_settings). Plain C with no hardware, so it builds on a
 * computer too, for tools/menu_shots. */
#include "rtc.h"

#include <stdio.h>

#include "settings.h"

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

void rtc_format_clock(char *buf, size_t size, const rtc_datetime_t *dt, bool with_date) {
    if (with_date) {
        char time[12], date[16];
        rtc_format_time(time, sizeof(time), dt, false);
        rtc_format_day_month(date, sizeof(date), dt);
        snprintf(buf, size, "%s %s", time, date);
    } else if (px_settings.clock_24h) {
        snprintf(buf, size, "%02d:%02d:%02d", dt->hour, dt->min, dt->sec);
    } else {
        snprintf(buf, size, "%d:%02d:%02d %s", dt->hour % 12 == 0 ? 12 : dt->hour % 12, dt->min, dt->sec,
                 dt->hour >= 12 ? "PM" : "AM");
    }
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
