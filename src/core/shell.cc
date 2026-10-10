/* Minimal stub implementation of the Free42 shell interface (shell.h) for
 * the Pico 2 port. These are placeholder bodies whose only job right now is
 * to let the emulator core link; real hardware behavior (display, beeper,
 * persistent storage, RTC, etc.) still needs to be wired up.
 */

#include <stdio.h>

#include "shell.h"
#include "free42_app.h"
#include "rtc.h"
#include "settings.h"
#include "battery.h"
#include "sounds.h"
#include "statefs.h"
#include "printer.h"
#include "shell_spool.h"

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

/* Free42's state-file I/O (core_main.cc: my_fopen etc., enabled by the PX42
 * define) goes to the FAT volume in flash. */
FILE *my_fopen(const char *name, const char *mode) {
    return statefs_fopen(name, mode);
}

int my_rename(const char *oldname, const char *newname) {
    return statefs_rename(oldname, newname);
}

int my_remove(const char *name) {
    return statefs_remove(name);
}

const char *shell_platform() {
    return "0.1 Pico2";
}

void shell_blitter(const char *bits, int bytesperline, int x, int y,
                    int width, int height) {
}

/* 0-9 = TONE n (and BEEP), 10 = the error squeak: /SOUNDS/TONES/TONEn.wav,
 * ERROR.wav (or their built-ins -- sounds.h). */
void shell_beeper(int tone) {
    sounds_play_tone(tone);
}

void shell_annunciators(int updn, int shf, int prt, int run, int g, int rad) {
}

/* Hands the CPU back to the main loop every time slice while a program
 * runs (see free42_service()) -- it used to return false, so a program ran
 * to completion in one call and starved the watchdog. */
bool shell_wants_cpu() {
    return free42_slice_expired();
}

void shell_delay(int duration) {
}

void shell_request_timeout3(int delay) {
    free42_request_timeout3(delay);
}

uint8 shell_get_mem() {
    return 0;
}

bool shell_low_battery() {
    return battery_low();
}

void shell_powerdown() {
    free42_request_powerdown();
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

/* Free42 asks at Memory Clear: the Time Format setting. */
bool shell_clk24() {
    return px_settings.clock_24h;
}

/* Printer emulation (see printer.h): a text line goes out as UTF-8 --
 * hp2ascii() translates the HP-42S characters (divide sign, Sigma, arrows,
 * ...) -- and a graphics-only print (PRLCD: text is NULL) as block art from
 * the bitmap. */
void shell_print(const char *text, int length,
                 const char *bits, int bytesperline,
                 int x, int y, int width, int height) {
    (void) x;
    (void) y;
    if (text == NULL) {
        printer_write_bitmap((const uint8_t *) bits, bytesperline, width, height);
        return;
    }
    /* A printed line is at most 24 characters, doubled in double-wide mode;
     * hp2ascii() emits up to 5 bytes per character. */
    static char utf8[48 * 5 + 2];
    if (length > 48)
        length = 48;
    int n = hp2ascii(utf8, text, length);
    utf8[n++] = '\r';
    utf8[n++] = '\n';
    printer_write(utf8, n);
}

void shell_get_time_date(uint4 *time, uint4 *date, int *weekday) {
    /* *time is packed HHMMSScc (cc = hundredths); confirmed against
     * docmd_time's own unpacking (core_commands7.cc: h = time/1000000,
     * m = time/10000%100, s = time/100%100). *date is YYYYMMDD, *weekday
     * 0 = Sunday -- the same conventions rtc_get_datetime() uses. */
    rtc_datetime_t dt;
    rtc_get_datetime(&dt);
    if (time != NULL)
        *time = (uint4) (dt.hour * 1000000 + dt.min * 10000 + dt.sec * 100 + dt.ms / 10);
    if (date != NULL)
        *date = (uint4) (dt.year * 10000 + dt.month * 100 + dt.day);
    if (weekday != NULL)
        *weekday = dt.weekday;
}

bool shell_set_date(int4 year, int4 month, int4 day) {
    return rtc_set_date(year, month, day);
}

void shell_set_time(int4 hour, int4 min, int4 sec) {
    rtc_set_hms(hour, min, sec);
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
