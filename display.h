#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/spi.h"
#include "font.h"

/* Sitronix ST7305, 250x122 monochrome LCD, write-only SPI (no MISO, no
 * hardware CS -- RS/RST are plain GPIO). */
#define DISPLAY_WIDTH  250
#define DISPLAY_HEIGHT 122

#define DISP_SPI      spi0
#define DISP_SCK_PIN  18  /* SPI0 SCK */
#define DISP_MOSI_PIN 19  /* SPI0 TX */
#define DISP_RS_PIN   20  /* Register select: 0 = command, 1 = data */
#define DISP_RST_PIN  21  /* Reset, active low */

#ifdef __cplusplus
extern "C" {
#endif

/* Sets up SPI0 and the RS/RST GPIOs, resets the controller, and sends its
 * power-on initialization sequence. Must be called once before any other
 * display_* function. */
void display_init(void);

/* Clears the in-memory framebuffer (does not touch the panel until
 * display_flush() is called). */
void display_clear(void);

/* Fills the in-memory framebuffer with a raw byte value (does not touch
 * the panel until display_flush() is called). 0x00 = all pixels off,
 * 0xFF = all pixels on -- useful as a hardware bring-up test, independent
 * of any font. */
void display_fill(uint8_t value);

/* Sends the whole in-memory framebuffer to the panel. Automatically wakes
 * the panel to High Power Mode (16Hz refresh) first if it had been idled
 * down to Low Power Mode (1Hz) by display_tick() -- see there. */
void display_flush(void);

/* Call periodically (e.g. once per main-loop iteration) to drop the panel
 * back to Low Power Mode (1Hz refresh, lower power draw) once it's been
 * idle for a bit after the last display_flush(). Only touches the panel
 * when a transition is actually due, so calling this often is cheap. */
void display_tick(void);

/* Sets one pixel in the in-memory framebuffer. (x, y) outside the visible
 * DISPLAY_WIDTH x DISPLAY_HEIGHT area is silently ignored -- callers (in
 * particular display_draw_char()) don't need their own bounds checks. */
void display_set_pixel(uint8_t x, uint8_t y, bool on);

/* Draws one character from the given font into the framebuffer with its
 * top-left corner at pixel (x, y) -- any row/column, not just multiples
 * of some hardware-specific block size. 'color' is XORed with each font
 * bit: 0x00 for normal, 0xFF for inverted/highlighted. Glyph pixels that
 * land outside the visible display are clipped (via display_set_pixel()),
 * not rejected outright, so a character straddling the edge still draws
 * the part that's on-screen. Returns the x position for the next
 * character (x + font->width + 1, i.e. a 1px gap). See fonts/font.h for
 * the display_font_t layout and fonts/gen_font.py / fonts/font_*.h for
 * the available fonts. */
uint8_t display_draw_char(const display_font_t *font, char c, uint8_t x, uint8_t y, uint8_t color);

/* Draws a null-terminated string starting at pixel (x, y), advancing by
 * each character's width + 1px gap. Returns the x position for whatever
 * comes next, so calls can be chained (e.g. a label followed by a
 * value). */
uint8_t display_draw_string(const display_font_t *font, const char *s, uint8_t x, uint8_t y, uint8_t color);

/* Draws a horizontal dotted line (alternating on/off pixels, starting
 * on at x0) at row y, from column x0 to x1 inclusive. */
void display_draw_dotted_hline(uint8_t x0, uint8_t x1, uint8_t y);

#ifdef __cplusplus
}
#endif

#endif
