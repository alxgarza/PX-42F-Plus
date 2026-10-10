#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

/* A 1bpp bitmap for display_draw_bitmap(): rows of ceil(width/8) bytes,
 * MSB-first, 1 = black. Generated from a PNG by images/gen_image.py. */
typedef struct {
    uint16_t width;
    uint16_t height;
    const uint8_t *data;
} display_image_t;

extern const display_image_t splash_image;

#endif
