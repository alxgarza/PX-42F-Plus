#ifndef FONTSHEET_H
#define FONTSHEET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "png1bit.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A font sheet: a PNG with the 128 glyphs of a font (HP-42S character
 * codes 0-127) in a grid, 16 across and 8 down, code = row * 16 + column
 * -- what fonts/gen_font.py makes the built-in fonts from. Every cell is
 * cell_w x cell_h pixels, with gap pixels between cells (and none around
 * the edge), so the image has to be exactly
 *   16 * cell_w + 15 * gap  by  8 * cell_h + 7 * gap  pixels.
 * Dark, opaque pixels are ink (png1bit.h); gap pixels are ignored. */
#define FONT_SHEET_COLS   16
#define FONT_SHEET_ROWS   8
#define FONT_SHEET_GLYPHS (FONT_SHEET_COLS * FONT_SHEET_ROWS)

/* Decodes a sheet into glyphs: FONT_SHEET_GLYPHS glyphs of
 * ceil(cell_w * cell_h / 8) bytes each, in display_font_t's packing
 * (fonts/font.h). False with the reason in error -- glyphs is then
 * untouched, so a font in use stays whole. The sheet's bitmap is malloc'd
 * for the duration. */
bool font_sheet_load(const png_source_t *src, int cell_w, int cell_h, int gap, uint8_t *glyphs,
                     char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
