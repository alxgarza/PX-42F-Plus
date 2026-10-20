#ifndef KEYHOLD_H
#define KEYHOLD_H

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
 *
 * A screenshot leaves no trace: it saves the screen as it was when SHIFT
 * went down, that SHIFT is cancelled (free42_key_cancel()), and both keys
 * are up by the time KEY_HOLD_SCREENSHOT is returned -- the caller only
 * draws its screen again (the message about the file is still on it). */

typedef enum { KEY_HOLD_NONE, KEY_HOLD_OFF, KEY_HOLD_SCREENSHOT } key_hold_t;

#define EXIT_HOLD_OFF_MS   2000
#define SCREENSHOT_HOLD_MS 500
/* Waits for a key to come up stop after this long: it's stuck or
 * miswired, and the calculator mustn't hang on it. */
#define KEY_STUCK_MS       60000

/* A key went down: call before acting on it. */
void key_hold_begin(uint8_t row, uint8_t col);

/* Call again and again while it stays down. */
key_hold_t key_hold_poll(void);

#ifdef __cplusplus
}
#endif

#endif
