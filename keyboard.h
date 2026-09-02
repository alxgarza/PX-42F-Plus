#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

/* 8 rows (outputs, GPIO0-7) x 5 columns (inputs w/ internal pull-ups,
 * GPIO8-12). A row is driven low (active) one at a time while scanning;
 * a pressed key at that row pulls its column low too. */
#define KB_ROWS 8
#define KB_COLS 5

#ifdef __cplusplus
extern "C" {
#endif

/* Sets up the row (output, idle high) and column (input, pulled up)
 * GPIOs. Call once before keyboard_scan(). */
void keyboard_init(void);

/* Scans the whole matrix once. If any key is currently pressed, writes
 * its position to *row (0-7) and *col (0-4) and returns true. Returns
 * false (leaving *row/*col untouched) if nothing is pressed. With more
 * than one key held down at once, reports whichever one the scan reaches
 * first (lowest row, then lowest column) -- no rollover/ghosting
 * handling yet, fine for bring-up testing one key at a time.
 *
 * Only use this to detect a *new* press (nothing else known to be held).
 * To check whether an already-known key is still down, use
 * keyboard_is_held() instead -- using this function for that mixes up
 * "is my key still down" with "is *some* key down," which silently
 * swallows a second key pressed before the first is released (see
 * keyboard_is_held()'s comment). */
bool keyboard_scan(uint8_t *row, uint8_t *col);

/* Checks one specific matrix position (as previously returned by
 * keyboard_scan()), without scanning the rest of the matrix. Use this to
 * track a specific key's release -- e.g. while waiting out a hold-timeout
 * on the key that was actually pressed -- instead of re-calling
 * keyboard_scan(), which reports the first pressed key in scan order
 * and would silently substitute a *different* key (one that happened to
 * get pressed, e.g. during fast typing, before this one was released)
 * for "still held," swallowing that other key's own press entirely. */
bool keyboard_is_held(uint8_t row, uint8_t col);

#ifdef __cplusplus
}
#endif

#endif
