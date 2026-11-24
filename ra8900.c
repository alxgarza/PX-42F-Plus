#include "ra8900.h"
#include "timemodule.h"

#define MS_PER_DAY (86400ull * 1000ull)

static uint8_t to_bcd(int n) {
    return (uint8_t) (n / 10 << 4 | n % 10);
}

/* -1 if it isn't BCD. */
static int from_bcd(uint8_t b) {
    if ((b & 0x0F) > 9 || b >> 4 > 9)
        return -1;
    return (b >> 4) * 10 + (b & 0x0F);
}

bool ra8900_encode(uint64_t ms, uint8_t regs[7]) {
    rtc_datetime_t dt;
    time_ms_to_datetime(ms, &dt);
    if (dt.year < 2000 || dt.year > 2099)
        return false;
    regs[0] = to_bcd(dt.sec);
    regs[1] = to_bcd(dt.min);
    regs[2] = to_bcd(dt.hour);
    regs[3] = (uint8_t) (1u << dt.weekday); /* one bit a day, Sunday bit 0 */
    regs[4] = to_bcd(dt.day);
    regs[5] = to_bcd(dt.month);
    regs[6] = to_bcd(dt.year - 2000);
    return true;
}

bool ra8900_decode(const uint8_t regs[7], uint64_t *ms) {
    /* The unused bits read 0 (bit 7 of SEC and MIN, 7-6 of HOUR, DAY, 7-5
     * of MONTH): one that's set is a register that isn't a time. */
    int sec = from_bcd(regs[0]), min = from_bcd(regs[1]), hour = from_bcd(regs[2]);
    int day = from_bcd(regs[4]), month = from_bcd(regs[5]), year = from_bcd(regs[6]);
    if (sec < 0 || sec > 59 || min < 0 || min > 59 || hour < 0 || hour > 23 || year < 0 || month < 1 || month > 12)
        return false;
    year += 2000;
    if (day < 1 || day > time_days_in_month(year, month))
        return false;
    *ms = (uint64_t) time_days_from_civil(year, month, day) * MS_PER_DAY
          + ((uint64_t) hour * 3600 + (uint64_t) min * 60 + (uint64_t) sec) * 1000;
    return true;
}

uint64_t ra8900_settle(uint64_t clock_ms, uint64_t chip_ms) {
    if (clock_ms < chip_ms)
        return chip_ms;
    if (clock_ms > chip_ms + 999)
        return chip_ms + 999;
    return clock_ms;
}

ra8900_timer_t ra8900_wake_timer(uint64_t now_ms, uint64_t wake_ms) {
    ra8900_timer_t t;
    uint64_t now_s = now_ms / 1000;
    uint64_t wake_s = (wake_ms + 999) / 1000; /* the second it's due in, turned */
    uint64_t n = wake_s > now_s ? wake_s - now_s : 1;
    if (n == 1 && 1000 - now_ms % 1000 < RA8900_WAKE_MARGIN_MS)
        n = 2;
    if (n <= 4095) {
        t.tsel = RA8900_EXT_TSEL_SEC;
        t.count = (uint16_t) n;
        return t;
    }
    /* Over an hour away, so many minutes: no margin needed. */
    uint64_t m = wake_s / 60 - now_s / 60;
    t.tsel = RA8900_EXT_TSEL_MIN;
    t.count = (uint16_t) (m > 4095 ? 4095 : m);
    return t;
}
