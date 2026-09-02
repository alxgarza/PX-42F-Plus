#include "rtc.h"

#include "hardware/powman.h"

void rtc_clock_init(void) {
    powman_timer_set_1khz_tick_source_lposc();
    powman_timer_set_ms(0); /* clock starts at 00:00:00.000 */
    powman_timer_start();
}

void rtc_get_hms(int *hour, int *min, int *sec) {
    uint64_t total_sec = (powman_timer_get_ms() / 1000) % 86400;
    *hour = (int) (total_sec / 3600);
    *min = (int) ((total_sec / 60) % 60);
    *sec = (int) (total_sec % 60);
}
