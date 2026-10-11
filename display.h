#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/spi.h"
#include "font.h"

/* Sitronix ST7305, 250x122 monochrome LCD, write-only SPI (no MISO;
 * RS/RST/CS are plain GPIO). */
#define DISPLAY_WIDTH  250
#define DISPLAY_HEIGHT 122

#define DISP_SPI      spi0
#define DISP_SCK_PIN  18  /* SPI0 SCK */
#define DISP_MOSI_PIN 19  /* SPI0 TX */
#define DISP_RS_PIN   20  /* Register select: 0 = command, 1 = data */
#define DISP_RST_PIN  21  /* Reset, active low */
#define DISP_CS_PIN   22  /* Chip select, active low */

#ifdef __cplusplus
extern "C" {
#endif

/* Sets up SPI0 and the RS/RST GPIOs, resets the controller, and sends its
 * power-on initialization sequence. Must be called once before any other
 * display_* function. */
void display_init(void);

/* Takes over the bus to a panel that's already running -- after a wake
 * from a deep power-off that kept it showing the splash -- without
 * resetting it: it keeps its image and settings. */
void display_attach(void);

/* Clears the in-memory framebuffer (does not touch the panel until
 * display_flush() is called). */
void display_clear(void);

/* Sends the whole in-memory framebuffer to the panel. */
void display_flush(void);

/* The panel always runs in Low Power Mode (no visible blinking, like the
 * PX-41CX); this picks its refresh rate: 8Hz normally, 1Hz (slow = true,
 * lowest draw while still showing an image) for the off-screen splash. At
 * 1Hz, rows written on either side of a refresh can appear up to ~1s
 * apart, so it's only for screens that change once a minute. */
void display_set_slow_refresh(bool slow);

/* Clears the panel (and the framebuffer) and puts the controller into
 * sleep mode (lowest panel power), for a power-off: the next boot's
 * display_init() resets it. Takes ~0.4s. */
void display_sleep(void);

/* Sets one pixel in the in-memory framebuffer. (x, y) outside the visible
 * DISPLAY_WIDTH x DISPLAY_HEIGHT area is silently ignored -- callers (in
 * particular display_draw_char()) don't need their own bounds checks. */
void display_set_pixel(uint8_t x, uint8_t y, bool on);

/* Fills the rectangle [x0, x1] x [y0, y1] (inclusive on all sides) with
 * on/off pixels -- e.g. the solid black status-bar background that
 * inverted (color=0xFF) text is then drawn over. */
void display_fill_rect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool on);

/* Draws one character from the given font into the framebuffer with its
 * top-left corner at pixel (x, y) -- any row/column, not just multiples
 * of some hardware-specific block size. 'color' is XORed with each font
 * bit: 0x00 for normal, 0xFF for inverted/highlighted. Glyph pixels that
 * land outside the visible display are clipped (via display_set_pixel()),
 * not rejected outright, so a character straddling the edge still draws
 * the part that's on-screen. Returns the x position for the next
 * character (x + font->advance -- see fonts/font.h; not necessarily
 * width + 1, some fonts' glyphs already reserve blank columns). See
 * fonts/font.h for the display_font_t layout and fonts/gen_font.py /
 * fonts/font_*.h for the available fonts. */
uint8_t display_draw_char(const display_font_t *font, char c, uint8_t x, uint8_t y, uint8_t color);

/* Draws a null-terminated string starting at pixel (x, y), advancing by
 * each character's font->advance. Returns the x position for whatever
 * comes next, so calls can be chained (e.g. a label followed by a
 * value). */
uint8_t display_draw_string(const display_font_t *font, const char *s, uint8_t x, uint8_t y, uint8_t color);

/* Same as display_draw_string(), but draws exactly len characters instead
 * of stopping at a 0 byte -- for Free42 text, where byte 0 is a real
 * character (the divide sign), not a terminator. */
uint8_t display_draw_chars(const display_font_t *font, const char *s, int len, uint8_t x, uint8_t y, uint8_t color);

/* Copies a 1bpp bitmap into the framebuffer at (x, y), clipped to the
 * screen: rows of ceil(width/8) bytes, MSB-first, 1 = black. Both ink and
 * background pixels are written. */
void display_draw_bitmap(const uint8_t *bits, int width, int height, int x, int y);

/* Draws a horizontal dotted line (alternating on/off pixels, starting
 * on at x0) at row y, from column x0 to x1 inclusive. */
void display_draw_dotted_hline(uint8_t x0, uint8_t x1, uint8_t y);

#ifdef __cplusplus
}
#endif

#endif
