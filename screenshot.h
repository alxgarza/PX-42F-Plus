#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Screenshots: the screen as a 250 x 122 black and white PNG (pngwrite.h),
 * the size of a splash image. Taken with SHIFT and "-" held together
 * (keyhold.h), or over the console. */

#define SCREENSHOT_DIR "/SCREENSHOTS"

/* Keeps a copy of the screen as it is now. */
void screenshot_remember(void);

/* That copy, asked about in an alert at once (the gesture's keys may still
 * be down; it waits for them to come up): SAVE (the default), PRINT with a
 * real printer chosen (the HP 82240B, or the USB ESC/POS one unless it just
 * failed to answer; turned a quarter, Landscape, whatever Screen Print
 * says; any key stops it), CANCEL (EXIT, <-). Saved is
 * /SCREENSHOTS/<date>-<time>.png on the drive, said across the top of the
 * screen for a second, or why not, with the error sound. The screen is put
 * back as it was; the caller may draw its own again. */
void screenshot_take(void);

/* !SCREENSHOT on the console: the screen as it is now, as a PNG in base64
 * between "SCREENSHOT BEGIN" and "SCREENSHOT END" lines -- for
 * tools/screenshot.py, which saves it on the computer. */
void screenshot_to_console(void);

#ifdef __cplusplus
}
#endif

#endif
