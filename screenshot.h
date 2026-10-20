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

/* Writes that copy to /SCREENSHOTS/<date>-<time>.png on the drive, and
 * says so across the top of the screen for a second -- or why not, with
 * the error sound. The caller draws its screen again afterwards. */
void screenshot_save(void);

/* !SCREENSHOT on the console: the screen as it is now, as a PNG in base64
 * between "SCREENSHOT BEGIN" and "SCREENSHOT END" lines -- for
 * tools/screenshot.py, which saves it on the computer. */
void screenshot_to_console(void);

#ifdef __cplusplus
}
#endif

#endif
