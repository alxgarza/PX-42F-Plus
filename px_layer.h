#ifndef PX_LAYER_H
#define PX_LAYER_H

#include <stdint.h>

#include "px_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Plus42's graphics on the whole panel, over everything else on it: two
 * pictures, the dots painted black (ink) and those painted white (wipe). No
 * hardware, for tests/run.sh. Positions count from 0,0; off the canvas is left
 * out. */

/* AGRAPH's modes, flags 34 and 35 as (flag 34 * 2 + flag 35). */
typedef enum {
    PX_PATTERN_OR = 0,
    PX_PATTERN_REPLACE = 1,
    PX_PATTERN_CLEAR = 2,
    PX_PATTERN_XOR = 3,
} px_pattern_mode_t;

void px_layer_dot(const px_canvas_t *ink, const px_canvas_t *wipe, int x, int y, int black);

/* width columns of 8 dots, the top one in the least significant bit, as AGRAPH
 * takes them from ALPHA. XOR flips what the layer holds: Plus42's text under it
 * counts as white. */
void px_layer_pattern(const px_canvas_t *ink, const px_canvas_t *wipe, int x, int y, const char *pattern,
                      int width, px_pattern_mode_t mode);

/* The layer over a picture of the same size. */
void px_layer_compose(uint8_t *picture, const px_canvas_t *ink, const px_canvas_t *wipe);

#ifdef __cplusplus
}
#endif

#endif
