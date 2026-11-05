#include <stdbool.h>
#include <stdlib.h>

#include "timemodule.h"

/* Days since 1970-01-01 for a Gregorian date, and back. Algorithms from
 * Howard Hinnant's "chrono-Compatible Low-Level Date Algorithms": they
 * work in 400-year eras (146097 days each), which builds the full leap
 * rule in -- every 4th year, except centuries, except every 400th. */
int64_t time_days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int) (y - era * 400);                               /* 0-399 */
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;      /* 0-365, counted from March 1 */
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;               /* 0-146096 */
    return era * 146097 + doe - 719468;
}

void time_civil_from_days(int64_t z, int *y, int *m, int *d) {
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

int time_days_in_month(int y, int m) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && is_leap_year(y) ? 29 : days[m - 1];
}

void time_ms_to_datetime(uint64_t ms, rtc_datetime_t *dt) {
    int64_t days = (int64_t) (ms / 86400000);
    uint32_t ms_of_day = (uint32_t) (ms % 86400000);
    time_civil_from_days(days, &dt->year, &dt->month, &dt->day);
    dt->hour = (int) (ms_of_day / 3600000);
    dt->min = (int) (ms_of_day / 60000 % 60);
    dt->sec = (int) (ms_of_day / 1000 % 60);
    dt->ms = (int) (ms_of_day % 1000);
    dt->weekday = (int) ((days + 4) % 7); /* 1970-01-01 was a Thursday */
}

bool time_digits_to_ms(int64_t digits, int64_t *ms) {
    int64_t hours = digits / 1000000;
    int min = (int) (digits / 10000 % 100);
    int sec = (int) (digits / 100 % 100);
    int hundredths = (int) (digits % 100);
    if (min > 59 || sec > 59)
        return false;
    *ms = ((hours * 60 + min) * 60 + sec) * 1000 + hundredths * 10;
    return true;
}

int af_tenths_from_milli(int64_t milli) {
    if (milli >= 99950)
        return 0;
    if (milli < 100)
        return 1;
    return (int) ((milli + 50) / 100);
}

/* 86400 / 10240 s/day at AF 1 = 84.375 tenths; over AF's tenths, 843.75. */
static int reciprocal(int n) {
    int size = abs(n);
    int result = (84375 + 50 * size) / (100 * size);
    return n < 0 ? -result : result;
}

int af_tenths_to_trim(int af_tenths) {
    return af_tenths == 0 ? 0 : reciprocal(af_tenths);
}

int trim_to_af_tenths(int trim) {
    if (trim == 0)
        return 0;
    int af = reciprocal(trim);
    return af != 0 ? af : (trim < 0 ? -1 : 1);
}

int64_t time_ms_to_digits(uint64_t ms) {
    int64_t seconds = (int64_t) (ms / 1000);
    return seconds / 3600 * 1000000 + seconds / 60 % 60 * 10000 + seconds % 60 * 100 + (int64_t) (ms % 1000) / 10;
}

bool alarm_time_to_ms(int64_t digits, bool negative, int64_t *ms) {
    if (!time_digits_to_ms(digits, ms))
        return false;
    int hour = (int) (digits / 1000000);
    if (negative) { /* -1 to -11 are 1 to 11 p.m. (section 15's table) */
        if (hour < 1 || hour > 11)
            return false;
        *ms += 12 * 3600000LL;
    }
    return hour <= 23;
}

bool alarm_date_to_days(int64_t digits, time_date_order_t order, int64_t *days) {
    int year, month, day;
    if (order == TIME_YMD) {
        year = (int) (digits / 1000000);
        month = (int) (digits / 10000 % 100);
        day = (int) (digits / 100 % 100);
    } else {
        int first = (int) (digits / 1000000);
        int second = (int) (digits / 10000 % 100);
        year = (int) (digits % 10000);
        month = order == TIME_MDY ? first : second;
        day = order == TIME_MDY ? second : first;
    }
    if (year < 1970 || year > 2199 || month < 1 || month > 12 || day < 1 || day > time_days_in_month(year, month))
        return false;
    *days = time_days_from_civil(year, month, day);
    return true;
}

int64_t alarm_days_to_digits(int64_t days, time_date_order_t order) {
    int year, month, day;
    time_civil_from_days(days, &year, &month, &day);
    if (order == TIME_YMD)
        return year * 1000000LL + month * 10000LL + day * 100LL;
    return (order == TIME_MDY ? month : day) * 1000000LL + (order == TIME_MDY ? day : month) * 10000LL + year;
}
