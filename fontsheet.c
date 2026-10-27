#include "fontsheet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool font_sheet_load(const png_source_t *src, int cell_w, int cell_h, int gap, uint8_t *glyphs,
                     char *error, size_t error_size) {
    int width = FONT_SHEET_WIDTH(cell_w, gap);
    int height = FONT_SHEET_HEIGHT(cell_h, gap);
    int stride = (width + 7) / 8;
    int bytes_per_char = (cell_w * cell_h + 7) / 8;

    uint8_t *sheet = (uint8_t *) malloc((size_t) stride * (size_t) height);
    if (sheet == NULL) {
        snprintf(error, error_size, "out of memory");
        return false;
    }
    bool ok = png_decode_1bit_exact(src, sheet, width, height, stride, error, error_size);
    if (ok) {
        memset(glyphs, 0, (size_t) FONT_SHEET_GLYPHS * (size_t) bytes_per_char);
        for (int code = 0; code < FONT_SHEET_GLYPHS; code++) {
            int x0 = (code % FONT_SHEET_COLS) * (cell_w + gap);
            int y0 = (code / FONT_SHEET_COLS) * (cell_h + gap);
            uint8_t *glyph = glyphs + code * bytes_per_char;
            int bit = 0;
            for (int y = 0; y < cell_h; y++) {
                const uint8_t *row = sheet + (y0 + y) * stride;
                for (int x = x0; x < x0 + cell_w; x++, bit++) {
                    if (row[x / 8] & (0x80 >> (x % 8)))
                        glyph[bit / 8] |= (uint8_t) (0x80 >> (bit % 8));
                }
            }
        }
    }
    free(sheet);
    return ok;
}

void font_sheet_draw(const display_font_t *font, int gap, uint8_t *sheet, int stride) {
    memset(sheet, 0, (size_t) stride * (size_t) FONT_SHEET_HEIGHT(font->height, gap));
    for (int code = font->first_char; code <= font->last_char && code < FONT_SHEET_GLYPHS; code++) {
        int x0 = (code % FONT_SHEET_COLS) * (font->width + gap);
        int y0 = (code / FONT_SHEET_COLS) * (font->height + gap);
        const uint8_t *glyph = font->data + (code - font->first_char) * font->bytes_per_char;
        int bit = 0;
        for (int y = 0; y < font->height; y++) {
            uint8_t *row = sheet + (y0 + y) * stride;
            for (int x = x0; x < x0 + font->width; x++, bit++) {
                if (glyph[bit / 8] & (0x80 >> (bit % 8)))
                    row[x / 8] |= (uint8_t) (0x80 >> (x % 8));
            }
        }
    }
}
