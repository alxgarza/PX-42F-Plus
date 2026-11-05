/* The Plus42 shell interface (shell.h) for the PX-42F: what the core asks
 * of its host -- sounds, the clock, files on the drive, the printer, the
 * battery. The screen (shell_blitter(), shell_annunciators()...) and the
 * keyboard are free42_app.cc's.
 */

#include <stdio.h>
#include <malloc.h>

#include "shell.h"
#include "free42_app.h"
#include "rtc.h"
#include "settings.h"
#include "battery.h"
#include "sounds.h"
#include "statefs.h"
#include "printer.h"
#include "hp82240.h"
#include "shell_spool.h"
#include "core_globals.h"
#include "pico/time.h"
#include "hardware/powman.h"

/* The Pico SDK's newlib syscall layer (pico_clib_interface) stubs out
 * _read/_write/_open/_close/_lseek/_fstat/_isatty, but not _link/_unlink,
 * which newlib's own rename()/remove() reference. Free42's file access
 * goes through my_fopen/my_rename/my_remove (-> statefs.c) instead, so
 * these only have to exist for the link.
 */
extern "C" int _link(const char *oldpath, const char *newpath) {
    return -1;
}

extern "C" int _unlink(const char *path) {
    return -1;
}

/* The SDK's _sbrk() lets the heap grow up to __StackLimit, the end of RAM:
 * past HEAP_LIMIT (CMakeLists.txt) into the retained framebuffer and the
 * calculator's memory kept through OFF (retained.h). This one stops at
 * __HeapLimit, so a big matrix gets Insufficient Memory (malloc returns
 * NULL with PICO_MALLOC_PANIC=0, new throws bad_alloc) instead of
 * overwriting them. */
extern "C" char end, __HeapLimit;
static char *heap_top;

extern "C" void *_sbrk(int incr) {
    if (heap_top == NULL)
        heap_top = &end;
    if (incr > &__HeapLimit - heap_top)
        return (void *) -1;
    char *prev = heap_top;
    heap_top += incr;
    return prev;
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

/* 0-9 = TONE n (and BEEP), 10 = the error squeak: /SOUNDS/TONES/TONEn.wav,
 * ERROR.wav (or their built-ins -- sounds.h). */
void shell_beeper(int tone) {
    sounds_play_tone(tone, true);
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

/* MEM: the heap malloc hasn't taken from _sbrk() yet, plus what's free
 * inside what it has. */
uint8 shell_get_mem() {
    struct mallinfo mi = mallinfo();
    if (heap_top == NULL)
        heap_top = &end;
    return (uint8) (&__HeapLimit - heap_top) + mi.fordblks;
}

bool shell_low_battery() {
    return battery_low();
}

void shell_powerdown() {
    free42_request_powerdown();
}

/* For 0 SEED ("seed from the clock"): the calendar clock's milliseconds
 * mixed with the microseconds since boot. */
int8 shell_random_seed() {
    return (int8) (powman_timer_get_ms() * 1000 + time_us_64() % 1000);
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

/* Printer emulation (see printer.h). To the HP 82240B, printer.c sends
 * each line (hp82240.c makes its bytes), and PRLCD (text NULL) prints the
 * screen redrawn upright, or as it is, turned (Settings -> Printer). To the USB serial port, a text line goes out as
 * UTF-8 -- hp2ascii() translates the HP-42S characters (divide sign, Sigma,
 * arrows, ...) -- and a graphics-only print (PRLCD: text is NULL) as block
 * art from the bitmap. */
void shell_print(const char *text, int length,
                 const char *bits, int bytesperline,
                 int x, int y, int width, int height) {
    (void) x;
    (void) y;
    if (printer_will_print())
        free42_printing_show();
    if (px_settings.printer == PRINTER_HP82240B) {
        if (text != NULL) {
            printer_hp82240b_text(text, length, (const uint8_t *) bits, bytesperline, flags.f.double_wide_print);
        } else {
            free42_print_stop_begin();
            int height = px_settings.screen_print == SCREEN_PRINT_UPRIGHT ? free42_draw_paper(HP82240_COLUMNS) : 0;
            if (height > 0) {
                printer_hp82240b_paper(height, free42_print_stop);
            } else {
                free42_draw_screen(false); /* no printer icon on the paper */
                printer_hp82240b_screen(free42_print_stop);
            }
            free42_draw_screen(true); /* the screen back, in place of the paper */
            free42_print_stop_end();
        }
        return;
    }
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
    free42_core_message(message);
}

void shell_log(const char *message) {
}
