#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#include "hardware/spi.h"

/* Sitronix ST7305, 250x122 monochrome LCD, write-only SPI (no MISO, no
 * hardware CS -- RS/RST are plain GPIO). */
#define DISPLAY_WIDTH  250
#define DISPLAY_HEIGHT 122

#define DISP_SPI      spi0
#define DISP_SCK_PIN  18  /* SPI0 SCK */
#define DISP_MOSI_PIN 19  /* SPI0 TX */
#define DISP_RS_PIN   20  /* Register select: 0 = command, 1 = data */
#define DISP_RST_PIN  21  /* Reset, active low */

/* The ST7305's "3 writes / 24 bit" GRAM format (see the Data Format Select
 * command in the init sequence) packs pixels into 2-column x 12-row tiles,
 * 3 bytes each. Framebuffer geometry is expressed in these tiles rather
 * than raw pixels. DISP_BLOCK_ROWS rounds up: the last block-row's bottom
 * 10 rows fall off the bottom of a 122-row panel and are never shown. */
#define DISP_BLOCK_COLS ((DISPLAY_WIDTH) / 2)                /* 125 */
#define DISP_BLOCK_ROWS (((DISPLAY_HEIGHT) + 11) / 12)       /* 11 */

typedef union {
    uint8_t arr[3];
    struct {
        uint8_t top; /* rows 0-3 of the tile's 2 columns */
        uint8_t mid; /* rows 4-7 */
        uint8_t bot; /* rows 8-11 */
    };
} disp_block_t;

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
 * of the font table. */
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

/* Draws one character into the framebuffer at the given pixel-column/
 * block-row position (block-row = 12px). Font is font_24x10: 10px wide,
 * 24px tall, with a 1px gap baked into the 11px advance -- since that's
 * odd, pixel_col need not be block-aligned, and consecutive characters in
 * a string land on alternating even/odd pixel columns. An odd pixel_col
 * is handled via bit-shifting between two adjacent framebuffer blocks
 * (see the .c file for the confirmed ST7305 bit layout this relies on).
 * 'color' is XORed with the font bits: 0x00 for normal, 0xFF for
 * inverted/highlighted. Returns the pixel-column position for the next
 * character (pixel_col + 11). */
uint8_t display_draw_char(char c, uint8_t pixel_col, uint8_t block_row, uint8_t color);

/* Draws a null-terminated string starting at the given pixel-column/
 * block-row position, advancing by each character's width + 1px gap.
 * Returns the pixel-column position for whatever comes next, so calls
 * can be chained (e.g. a label followed by a value). */
uint8_t display_draw_string(const char *s, uint8_t pixel_col, uint8_t block_row, uint8_t color);

#ifdef __cplusplus
}
#endif

#endif
