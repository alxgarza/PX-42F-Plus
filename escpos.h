#ifndef ESCPOS_H
#define ESCPOS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A USB thermal receipt printer's side of printing (usbprint.c sends it):
 * the ESC/POS bytes of a printed line or picture. No hardware calls, so
 * tests/run.sh checks it on the Mac. Made for the 58 mm printers that
 * print 384 dots across (48 mm at 8 dots/mm) with a 12 x 24 font, 32
 * characters a line; tried on a YICHIP "PT2D" one. */

#define ESCPOS_DOTS 384 /* dots across the paper */

/* Sent once each time the printer is connected: reset, the Chinese
 * two-byte mode off (some start in it, and every byte past 0x7F would
 * begin a Chinese character), code page 437 (ESC t 0; the YICHIP printer
 * stays in 850, so only the bytes both share are used), lines 30 dots
 * apart (ESC 3), what a picture line is. */
#define ESCPOS_INIT     "\x1b@\x1c.\x1bt\x00\x1b" "3\x1e"
#define ESCPOS_INIT_LEN 10

/* After the last line, the paper fed up past the tear bar. */
#define ESCPOS_FEED     "\x1b" "d\x03"
#define ESCPOS_FEED_LEN 3

/* A line with a character the printer lacks is a picture: Plus42's 143
 * dots, each 2 across, in rows of 36 bytes, 24 rows, at most
 * ESCPOS_LINE_DOTS (the line spacing) with the gap above. */
#define ESCPOS_LINE_DOTS      30
#define ESCPOS_LINE_ROW_BYTES 36
#define ESCPOS_LINE_MAX       (8 + ESCPOS_LINE_ROW_BYTES * ESCPOS_LINE_DOTS)

/* A text line: text is Plus42's print_text() line (HP-42S character codes;
 * in double-wide mode each character is followed by a filler), bits its
 * 143 x 9 bitmap (bytesperline bytes a row, least significant bit first,
 * character i at dot column 6i, doubled in double-wide). In the printer's
 * font if it has every character, else a picture of bits (above). *picture
 * says whether the line before was a picture, and is set to whether this
 * one is: the spacing differs after one. Returns the number of bytes
 * written to out (at most ESCPOS_LINE_MAX). */
int escpos_text_line(uint8_t *out, const char *text, int length, const uint8_t *bits, int bytesperline,
                     bool double_wide, bool *picture);

/* A picture, upright and centred on the paper, as one raster image (GS v
 * 0): the header, then each of its height rows. width at most ESCPOS_DOTS.
 * The header returns 8, the bytes written to out; a row returns
 * ESCPOS_ROW_BYTES(width). row: width pixels, the leftmost in the most
 * significant bit (display_fb.h). */
#define ESCPOS_ROW_BYTES(width) (((ESCPOS_DOTS - (width)) / 2 + (width) + 7) / 8)
int escpos_picture_header(uint8_t *out, int width, int height);
int escpos_picture_row(uint8_t *out, const uint8_t *row, int width);

#ifdef __cplusplus
}
#endif

#endif
