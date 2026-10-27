/* Host stand-in for the Pico SDK's time functions: a clock that only moves
 * when the tool says (tools/menu_shots). */
#ifndef PICO_TIME_H
#define PICO_TIME_H
#include "pico/types.h"
extern uint64_t host_now_us;
#define at_the_end_of_time ((absolute_time_t) UINT64_MAX)
static inline absolute_time_t get_absolute_time(void) { return host_now_us; }
static inline uint64_t time_us_64(void) { return host_now_us; }
static inline absolute_time_t make_timeout_time_ms(uint32_t ms) { return host_now_us + (uint64_t) ms * 1000; }
static inline absolute_time_t delayed_by_ms(absolute_time_t t, uint32_t ms) { return t + (uint64_t) ms * 1000; }
static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) { return (int64_t) (to - from); }
static inline bool time_reached(absolute_time_t t) { return host_now_us >= t; }
static inline void sleep_ms(uint32_t ms) { host_now_us += (uint64_t) ms * 1000; }
#endif
