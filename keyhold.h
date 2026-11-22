#ifndef KEYHOLD_H
#define KEYHOLD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What holding keys down does, wherever keys are read -- the calculator
 * screen (main.c) and the settings menu -- so both loops only decide what
 * it means for them:
 *
 *   EXIT, 2 seconds          turn off -- KEY_HOLD_OFF; the caller does it
 *                            (each screen turns off its own way)
 *   SHIFT and "-" (PRINT)    a screenshot (screenshot.h), done here; half a
 *   together, 0.5 seconds    second, because typing SHIFT then "-" quickly
 *                            for the PRINT menu overlaps the keys too
 *   SHIFT held (0.3 s),      BST / SST in ALPHA (free42_shift_arrow()), done
 *   then an arrow            here -- SHIFT *then* an arrow switches the
 *                            keyboard mode there
 *
 * A screenshot leaves no trace: it saves the screen as it was when SHIFT
 * went down, that SHIFT is cancelled (free42_key_cancel()), and both keys
 * are up by the time KEY_HOLD_SCREENSHOT is returned -- the caller only
 * draws its screen again (the message about the file is still on it). */

typedef enum { KEY_HOLD_NONE, KEY_HOLD_OFF, KEY_HOLD_SCREENSHOT } key_hold_t;

#define EXIT_HOLD_OFF_MS   2000
#define SCREENSHOT_HOLD_MS 500
/* SHIFT down this long before an arrow: held, not typed before it. */
#define SHIFT_ARROW_HOLD_MS 300
/* Waits for a key to come up stop after this long: it's stuck or
 * miswired, and the calculator mustn't hang on it. */
#define KEY_STUCK_MS       60000

/* A key went down: call before acting on it. */
void key_hold_begin(uint8_t row, uint8_t col);

/* Call again and again while it stays down. */
key_hold_t key_hold_poll(void);
/* Apps: the screenshot gesture, after key_hold_begin() on SHIFT. True when
 * a screenshot was taken (both keys are up then). */
bool key_hold_app_screenshot(void);
/* Apps: a key just went down, row, col. If it's "-" with the SHIFT the
 * gesture began with still held, waits here while both stay down, up to
 * SCREENSHOT_HOLD_MS: true if that made a screenshot (then the "-" is the
 * gesture's, not the app's; both keys are up). An app busy after each key
 * -- a graph replotted -- only looked at the gesture between keys, and
 * missed it. False: the "-" is the app's, a moment late. */
bool key_hold_app_minus(uint8_t row, uint8_t col);

#ifdef __cplusplus
}
#endif

#endif
