#ifndef FREE42_APP_H
#define FREE42_APP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cold-starts the Free42 core. Call once before free42_process_char(). */
void free42_init(void);

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
 *   free42_key_up()               -- once, when the key is released
 */
void free42_key_down(uint8_t row, uint8_t col);
bool free42_key_is_enqueued(void);
int free42_key_repeat_mode(void);
void free42_key_repeat_tick(void);
void free42_key_timeout1(void);
void free42_key_timeout2(void);
void free42_key_up(void);

/* Called by shell.cc's shell_status_message() whenever the core displays
 * an error/status message (e.g. "DIVIDE BY 0"). Stores it so the next
 * display_stack() redraw shows it on the Y row instead of Y's value. */
void free42_set_status_message(const char *text, int length);

#ifdef __cplusplus
}
#endif

#endif
