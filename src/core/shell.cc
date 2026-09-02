/* Minimal stub implementation of the Free42 shell interface (shell.h) for
 * the Pico 2 port. These are placeholder bodies whose only job right now is
 * to let the emulator core link; real hardware behavior (display, beeper,
 * persistent storage, RTC, etc.) still needs to be wired up.
 */

#include <stdio.h>

#include "shell.h"
#include "free42_app.h"
#include "rtc.h"

/* The Pico SDK's newlib syscall layer (pico_clib_interface) stubs out
 * _read/_write/_open/_close/_lseek/_fstat/_isatty, but not _link/_unlink.
 * Free42's state-file save path (my_rename/my_remove, and newlib's own
 * rename() fallback) can reach them, so provide no-op stubs here -- there
 * is no filesystem on this target yet.
 */
extern "C" int _link(const char *oldpath, const char *newpath) {
    return -1;
}

extern "C" int _unlink(const char *path) {
    return -1;
}

const char *shell_platform() {
    return "0.1 Pico2";
}

void shell_blitter(const char *bits, int bytesperline, int x, int y,
                    int width, int height) {
}

void shell_beeper(int tone) {
}

void shell_annunciators(int updn, int shf, int prt, int run, int g, int rad) {
}

bool shell_wants_cpu() {
    return false;
}

void shell_delay(int duration) {
}

void shell_request_timeout3(int delay) {
}

uint8 shell_get_mem() {
    return 0;
}

bool shell_low_battery() {
    return false;
}

void shell_powerdown() {
}

int8 shell_random_seed() {
    return 0;
}

uint4 shell_milliseconds() {
    return 0;
}

const char *shell_number_format() {
    return ".";
}

int shell_date_format() {
    return 0;
}

bool shell_clk24() {
    return true;
}

void shell_print(const char *text, int length,
                 const char *bits, int bytesperline,
                 int x, int y, int width, int height) {
}

void shell_get_time_date(uint4 *time, uint4 *date, int *weekday) {
    /* *time is packed HHMMSScc (cc = hundredths, always 0 here -- rtc.c is
     * only second-resolution); confirmed against docmd_time's own
     * unpacking (core_commands7.cc: h = time/1000000, m = time/10000%100,
     * s = time/100%100). Date/weekday stay stubbed -- rtc.c tracks time
     * of day only, no calendar. */
    int hour, min, sec;
    rtc_get_hms(&hour, &min, &sec);
    if (time != NULL)
        *time = (uint4) (hour * 1000000 + min * 10000 + sec * 100);
    if (date != NULL)
        *date = 20260101;
    if (weekday != NULL)
        *weekday = 4;
}

void shell_message(const char *message) {
}

void shell_log(const char *message) {
}

void shell_status_message(const char *text, int length) {
    /* Used to printf() this over USB serial, but a blocking stdio write
     * with no host actively reading it can stall long enough to trip the
     * watchdog -- this fires on every error/status message (out of range,
     * divide by zero, etc.), so it's tied to usage, not idle time. Routed
     * to the LCD's Y row instead (see free42_set_status_message() and
     * display_stack() in free42_app.cc). */
    free42_set_status_message(text, length);
}
