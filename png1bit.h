#ifndef PNG1BIT_H
#define PNG1BIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A small streaming PNG decoder that renders straight to a 1-bit bitmap --
 * for user splash images (splash.c). Every standard PNG variant except
 * interlaced: grayscale, RGB, palette, gray+alpha and RGBA, at any bit
 * depth (1/2/4/8/16), any row filter. Pixels become black where they're
 * dark (average of R, G, B below 128) and opaque (alpha at least 128) --
 * the same rule as images/gen_image.py -- and everything else white.
 *
 * The image is centered on the bitmap: smaller images get a white border,
 * larger ones are cropped. Working memory (a 32KB inflate window plus two
 * image rows) is malloc'd for the duration and freed before returning. */

typedef struct {
    /* Reads up to len bytes of the PNG file into buf; returns the count, 0
     * at end of file, negative on error. */
    int (*read)(void *ctx, uint8_t *buf, int len);
    void *ctx;
    /* Called now and then during a long decode (e.g. to feed a watchdog);
     * may be NULL. */
    void (*tick)(void);
} png_source_t;

/* Decodes into bits: width x height pixels, rows of stride bytes, MSB-first,
 * 1 = black. On failure returns false with a short reason in error (the
 * bitmap is then undefined). */
bool png_decode_1bit(const png_source_t *src, uint8_t *bits, int width, int height, int stride,
                     char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
