#ifndef FREE42_APP_H
#define FREE42_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "pico/types.h"
#include "sounds.h"

#include "px_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the Free42 core: with the memory free42_sleep() saved, if there
 * is one (back from OFF -- or after a crash: the memory as of the last
 * ON), otherwise from Memory Clear. after_crash: this boot follows a fault
 * or watchdog reset, so the saved memory isn't kept for another go. Call
 * once, after statefs_init(), before free42_process_char(). */
void free42_init(bool after_crash, bool memory_clear);
/* Where the memory came from at that start: "RAM", "drive" or "none (Memory
 * Clear)" -- for the boot log (bootlog.h). */
const char *free42_state_source(void);
bool free42_drive_copy_exists(void);

/* Feeds one character in (typed over the serial console) as if it were an
 * HP-42S keypress, then prints the resulting 4-level stack.
 * Recognized characters: '0'-'9', '+', '-', '*', '/', and Enter ('\r' or '\n').
 * Anything else is ignored.
 */
void free42_process_char(char c);

/* Physical keyboard-matrix press/hold/release, mapped to the corresponding
 * HP-42S key code for this hardware's wiring (see the key_matrix table in
 * free42_app.cc; positions with no function mapped yet are silently
 * ignored). Split into separate calls, rather than one shot like
 * free42_process_char(), so the caller can drive the real HP-42S/Free42
 * "hold to confirm" timing: hold 1/4 second and the key's function name
 * appears on the Y row; keep holding to 2 seconds and it switches to
 * "NULL", meaning release now cancels the key instead of performing it.
 * Expected call sequence, driven by the caller's own timing loop against
 * keyboard_scan():
 *   free42_key_down(row, col)     -- once, when the press is first seen
 *   free42_key_is_enqueued()      -- check right after; if true, skip
 *                                    every call below except key_up()
 *   free42_key_repeat_mode()      -- check after key_down() and after each
 *                                    repeat_tick(); if nonzero, drive
 *                                    repeat_tick() instead of the
 *                                    timeout1/timeout2 pair below
 *   free42_key_repeat_tick()      -- while repeat_mode() is nonzero, call
 *                                    at the auto-repeat cadence
 *   free42_key_timeout1()         -- once, 250ms after key_down(), if
 *                                    still held and not repeating
 *   free42_key_timeout2()         -- once, 2000ms after key_down(), if
 *                                    still held and not repeating
 *   free42_key_cancel()           -- instead of acting on the key, if it
 *                                    was used for something else (a
 *                                    screenshot, keyhold.h); key_up()
 *                                    still follows
 *   free42_key_up()               -- once, when the key is released
 */
void free42_key_down(uint8_t row, uint8_t col);
bool free42_key_is_enqueued(void);
int free42_key_repeat_mode(void);
void free42_key_repeat_tick(void);
void free42_key_timeout1(void);
void free42_key_timeout2(void);
void free42_key_cancel(void);
void free42_key_up(void);
/* SHIFT held down + an arrow (free42_key_code()) in ALPHA: BST / SST, done;
 * false if that's not what the key does now. keyhold.c. */
bool free42_shift_arrow(int code);

/* Redraws the whole screen from the current state -- after the settings
 * menu, or for the battery gauge when nothing else has changed. */
void free42_redisplay(void);

/* Call on every pass of the main loop: runs the next time slice of a
 * running program (and handles PSE's timeout). Returns true while a
 * program is running -- the main loop then skips its idle sleep and keeps
 * the auto power-off from firing. */
bool free42_service(void);

/* When free42_service() next has something to do though no program is
 * running -- the end of a PSE. False if nothing is pending. */
bool free42_next_timeout(absolute_time_t *at);

/* For shell.cc: shell_wants_cpu() (the current slice is used up) and
 * shell_request_timeout3() (PSE and friends). */
bool free42_slice_expired(void);
void free42_request_timeout3(int delay_ms);

/* CLOCK: shows the running clock until the next key. */
void free42_clock_start(void);

/* ALMCAT: the alarms, on the top lines, until a key leaves them. */
void free42_almcat_start(void);

/* SW and SWPT: the stopwatch's keyboard and display, until EXIT (or the close
 * below, when the core ends it), and then the error it ended with. */
void free42_stopwatch_open(int store, int recall, bool delta);
bool free42_stopwatch_showing(void);
void free42_stopwatch_close(void);
int free42_stopwatch_error(void);

/* What PRON/PROFF do: Free42's "printer present" flag (55), without which
 * printing commands and TRACE/NORM printing do nothing, and "printer enable"
 * (21), without which a running program's don't, and VIEW and AVIEW in one
 * don't print. Set at every start from the Printer setting. */
void free42_set_printer(bool on);
bool free42_printer_on(void);

/* Plus42's flag 26, clear: no squeak, no error sound, no BEEP or TONE. The
 * flag is the setting, as a program or a state changes it: Memory Clear
 * sets it. */
void free42_set_quiet(bool quiet);
bool free42_quiet(void);

/* Draws the screen as Plus42 has it now into the framebuffer, without
 * sending it to the panel (that still happens after the key): for PRLCD,
 * which prints the screen as it is. */
void free42_draw_screen(bool with_print_icon);

/* Puts the printer icon in the status bar, on the panel at once. It goes when
 * the last line has been sent. */
void free42_printing_show(void);

/* An app's print_text() (app.c): a line of HP-42S characters to the
 * printer, as Plus42 prints one but in single width and with none of its
 * screen (no printer icon). On the HP 82240B, PX_PRINT_COLUMNS characters a
 * line, a longer text going on to the next; the characters the printer
 * lacks are drawn as Plus42 draws them. */
void free42_print_app_text(const char *text, int length);

/* Stopping PRLCD, which holds core 1: free42_print_stop() is true from the moment
 * EXIT or R/S goes down, not one already held when it began (the R/S that started
 * the program). free42_print_stop_end() waits for the key to be released, so it
 * does nothing else, and has free42_service() stop a running program with it. */
void free42_print_stop_begin(void);
bool free42_print_stop(void);
void free42_print_stop_end(void);

/* The shapes' two pictures of the whole panel, for a shape to draw in, cleared if
 * none is showing: what it paints black in ink and white in wipe, and the opposite
 * for a white pen. False if there's no memory for them. */
bool free42_shape_canvases(px_canvas_t *ink, px_canvas_t *wipe);
bool free42_shape_memory(void);
void free42_shapes_changed(void);

/* Plus42 cleared its display (CLLCD, or starting a screen afresh). */
void free42_forget_shapes(void);

/* PRLCD upright on the HP 82240B: the screen redrawn narrow enough for
 * paper_w dots, in Plus42's own 6 x 8 characters (font_mini) -- the status
 * bar's text and angle mode on a black bar, the lines of text with a
 * dotted line between each two, and Plus42's own menu row -- centred in
 * paper_w at the top of the framebuffer, not sent to the panel
 * (free42_draw_screen() puts the screen back). Returns its height, or 0
 * when the screen doesn't fit: more than paper_w / 6 columns, or a plot. */
int free42_draw_paper(int paper_w);

/* HP-42S key code (1-37) at a keyboard-matrix position, 0 if none -- for
 * screens outside the calculator, like the settings menu. */
int free42_key_code(uint8_t row, uint8_t col);

/* Which of the Key Sound set's sounds the key at a keyboard-matrix
 * position plays, as things stand -- call before free42_key_down(). */
key_sound_t free42_key_sound(uint8_t row, uint8_t col);

/* The free42_key_code()s used outside the calculator -- the same values as
 * KEY_* in src/core/core_globals.h, which is C++ only. */
#define FREE42_KEY_ENTER 13
#define FREE42_KEY_BSP   17
#define FREE42_KEY_UP    18
#define FREE42_KEY_DOWN  23
#define FREE42_KEY_SHIFT 28
#define FREE42_KEY_MINUS 32
#define FREE42_KEY_EXIT  33 /* also ON */

/* True (once) after SHIFT+EXIT was pressed: the main loop should run the
 * settings menu (settings_menu.h). */
bool free42_settings_requested(void);
/* The same, without taking the request (it stays for the call above). */
bool free42_settings_pending(void);

/* The ALPHA-mode letter at a keyboard-matrix position (alpha_key_matrix),
 * 0 if none -- for typing names outside the calculator. */
char free42_alpha_char(uint8_t row, uint8_t col);

/* Sets Free42's date and time formats (DMY/MDY/YMD, CLK12/CLK24) --
 * rtc_date_format_t, 24-hour or not -- e.g. from the settings menu. */
void free42_set_formats(uint8_t date_format, bool clock_24h);

/* Save the calculator's whole memory to a .p42 state file, or replace it
 * with one (statefs.h has the paths). Loading a file Free42 can't read
 * leaves the calculator in Memory Clear, as on the desktop versions. */
void free42_save_state(const char *path);
void free42_load_state(const char *path);

/* Programs as Plus42 .raw files, the format the desktop versions import and
 * export, in PROGRAMS_DIR on the drive (Settings -> Programs). Of the
 * current directory, as Plus42's own export works. */
#define PROGRAMS_DIR      "/PROGRAMS"
#define PROGRAM_LABEL_MAX 30 /* characters of a program's labels kept for the list */
/* The programs, as the PGM catalog shows them, .END. last: each one's
 * global labels ("ABC" "DEF"), or END. Returns how many (at most max). */
int free42_list_programs(char names[][PROGRAM_LABEL_MAX + 1], int max);
/* One program (an index from free42_list_programs()), or all of them
 * (-1), to the file at path. False if it couldn't be written. */
bool free42_export_programs(int index, const char *path);
/* Adds the programs in the file at path to the calculator's. Returns how
 * many programs that added, or -1 if the file couldn't be read. */
int free42_import_programs(const char *path);
/* The memory to the drive, where the next boot takes it from
 * (free42_init()) -- the RAM copy is dropped, or that one, as of the last
 * ON, would come back instead. Before Update Firmware
 * (power_update_mode()), and before an app takes that RAM (app.c). False
 * if it couldn't be saved. */
bool free42_save_to_drive(void);
/* After an app: the memory back in its RAM copy (and the drive's copy
 * gone, unless the memory is too big for RAM). */
void free42_memory_to_ram(void);
/* Settings → Reset: Plus42's memory cleared (both copies dropped) and the
 * calculator restarted. Alarms, the stopwatch, the settings and the drive's
 * files stay -- unlike the RESET button. Doesn't return. */
__attribute__((noreturn)) void free42_memory_clear_restart(void);
/* The core's complaint about a file (shell_message()): noted for the two above. */
void free42_core_message(const char *message);

/* OFF / sleep. shell_powerdown() calls free42_request_powerdown(); the main
 * loop polls free42_powerdown_requested() and, when set, calls
 * free42_sleep() -- which saves the calculator's memory to RAM that stays
 * powered, shows the splash or blanks the display, and powers the chip
 * down (power.h). It never returns: ON reboots, and free42_init() restores
 * the memory. The caller must stop the watchdog first. */
void free42_request_powerdown(void);
/* False while Free42's "continuous on" (XEQ "ON", flag 44) is set -- the
 * main loop's auto power-off timer must not fire then. */
bool free42_auto_off_enabled(void);
bool free42_powerdown_requested(void);
/* quiet: without the off sound (an empty battery). */
void free42_sleep(bool quiet);

#ifdef __cplusplus
}
#endif

#endif
