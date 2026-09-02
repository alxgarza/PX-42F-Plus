#ifndef RTC_H
#define RTC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the RP2350's Powman time-of-day counter (there is no classic
 * hardware_rtc peripheral on RP2350 -- that's an RP2040-only block; this
 * project's target is pico2_w/RP2350, so hardware_powman is the real
 * counterpart). Ticks from the internal low-power oscillator, so no
 * external crystal is needed, but also no battery backup: time resets to
 * 00:00:00 on every power-on/reset. Call once at startup, before
 * rtc_get_hms(). */
void rtc_clock_init(void);

/* Current time of day, derived from the Powman millisecond counter modulo
 * 24h. hour is 0-23. */
void rtc_get_hms(int *hour, int *min, int *sec);

/* Sets the time of day. hour is 0-23. Takes effect immediately (no need
 * to stop/restart the counter first -- unlike the RP2040's classic
 * hardware_rtc, Powman's set_ms() can be called while running). */
void rtc_set_hms(int hour, int min, int sec);

#ifdef __cplusplus
}
#endif

#endif
