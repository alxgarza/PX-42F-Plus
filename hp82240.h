#ifndef HP82240_H
#define HP82240_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The HP 82240B infrared printer's side of printing (printer.c sends it):
 * what bytes a printed line is, and how a byte goes over the infrared.
 * No hardware calls, so tests/run.sh checks it on the Mac. From HP's
 * "HP 82240B Infrared Printer Technical Interfacing Guide". */

/* The most bytes one line can take: a text line of 24 characters the
 * printer has no glyph for is 4 mode bytes, one graphics run (ESC, count,
 * 166 columns) and the linefeed, 173; a screen line 4 + 166 + 1. */
#define HP82240_LINE_MAX 173

#define HP82240_COLUMNS 166 /* dot columns on a line */

/* One frame, the 30 half-bit times of one byte, first in bit 31 down to
 * bit 2: a 1 is a burst of carrier at the start of that half-bit. A start
 * (three bursts), the 4 error-correction bits and the 8 data bits, most
 * significant first (a 1 bursts in its first half, a 0 in its second), and
 * the three empty half-bits the printer needs before the next frame. */
uint32_t hp82240_frame(uint8_t byte);

/* A text line: text is Plus42's print_text() line (HP-42S character codes;
 * in double-wide mode each character is followed by a filler, '_' or ' '),
 * bits its 143 x 9 bitmap (bytesperline bytes a row, least significant bit
 * first), which supplies the shapes of the characters the printer doesn't
 * have. Returns the number of bytes written to out. */
int hp82240_text_line(uint8_t *out, const char *text, int length, const uint8_t *bits, int bytesperline,
                      bool double_wide);

/* Line band of a screen printed turned a quarter (PRLCD: the panel is
 * wider than the paper), as Plus42 turns a display too wide to print: the
 * paper's lines run across the screen from its right edge, 8 of its dot
 * columns a line, and along a line go its rows, the top at the left --
 * turn the paper a quarter clockwise to read it. fb: width x height
 * pixels, stride bytes a row, the leftmost pixel in the most significant
 * bit (display_fb.h). Rows past the printer's 166 dot columns are cut.
 * Returns the number of bytes written to out. */
int hp82240_screen_line(uint8_t *out, const uint8_t *fb, int stride, int width, int height, int band);

/* Line band of a picture printed upright: its rows band * 8 to band * 8 +
 * 7, the top one in the least significant bit, across from its left edge,
 * cut at the printer's 166 dot columns. fb as for hp82240_screen_line().
 * Returns the number of bytes written to out. */
int hp82240_upright_line(uint8_t *out, const uint8_t *fb, int stride, int width, int height, int band);

#ifdef __cplusplus
}
#endif

#endif
