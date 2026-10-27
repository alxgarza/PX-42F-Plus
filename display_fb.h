#ifndef DISPLAY_FB_H
#define DISPLAY_FB_H

#include <stdbool.h>
#include <stdint.h>

#include "display.h"

/* Canonical framebuffer: plain row-major 1bpp, hardware-independent. One
 * bit per pixel, MSB-first within each row byte, no relation to the
 * ST7305's tile memory layout -- that mapping lives entirely in
 * display_flush() (see pack_tile_byte() below), so drawing code here never
 * needs to know about it. This is what makes display_set_pixel() (and
 * everything built on it) work at any (x, y), not just positions aligned
 * to some hardware block size. */
#define FB_SIZE (DISPLAY_HEIGHT * DISPLAY_STRIDE)

/* The framebuffer itself: in RAM kept through the deep power-off on the
 * calculator (display.c), an ordinary array in the host tools. Shared by
 * display.c (sending it to the panel) and display_draw.c (drawing into
 * it), which runs on a computer too (tools/menu_shots). */
extern uint8_t (*const display_fb)[DISPLAY_STRIDE];

/* Bounds-checked read: "off" outside the screen. */
bool display_get_pixel(int x, int y);

#endif
