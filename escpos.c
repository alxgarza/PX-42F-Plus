#include "escpos.h"

#define ESC 0x1B
#define GS  0x1D
#define LF  0x0A

static int raster_header(uint8_t *out, int row_bytes, int height);

#define CHARS     24  /* a printed line, as on the HP 82240B */
#define TEXT_DOTS 143 /* Plus42's line bitmap: 24 characters 6 dots apart, less the last gap */

/* The printer's byte for an HP-42S character, or 0 if it may lack it: then
 * the line is a picture. Only bytes code pages 437 and 850 share: ESC t 0
 * asks for 437, but the YICHIP printer stays in 850, where 437's √ Σ π ≤
 * ≥ are ¹ õ Ò ¾ ‗. Plus42's 94 is the up arrow, not the caret (30 is). */
static uint8_t printer_char(uint8_t c) {
    switch (c) {
        case 0:  return 0xF6; /* ÷ */
        case 4:  return 0xB1; /* ▒ */
        case 8:  return 0xA8; /* ¿ */
        case 17: return 0xE6; /* µ */
        case 18: return 0x9C; /* £ */
        case 19: return 0xF8; /* ° */
        case 20: return 0x8F; /* Å */
        case 21: return 0xA5; /* Ñ */
        case 22: return 0x8E; /* Ä */
        case 25: return 0x92; /* Æ */
        case 28: return 0x99; /* Ö */
        case 29: return 0x9A; /* Ü */
        case 30: return '^';
        case 94: return 0;
        default: return c >= 32 && c <= 126 ? c : 0;
    }
}

/* A line with a character the printer lacks, as one picture of Plus42's
 * bitmap: each dot 2 across and 3 down, so a character is the font's own
 * 12 x 24 cell and the columns line up with the text lines (in double-wide
 * the bitmap is already doubled across, as GS ! doubles the font). Inline
 * bit images (ESC *) were tried: this printer put each on a line of its own
 * and lost the text before it.
 * The spacing, measured on the YICHIP printer: text lines are 30 dots
 * apart (ESC 3), and after a picture the printer leaves about 8 dots more
 * before text. So a picture line is just its 24 rows, and text after it
 * comes about 32 below; a picture after a picture gets the 6 rows a text
 * line would have had first. */
#define GAP_ROWS (ESCPOS_LINE_DOTS - 24)
static int picture_line(uint8_t *out, const uint8_t *bits, int bytesperline, bool after_picture) {
    int top = after_picture ? GAP_ROWS : 0;
    int n = raster_header(out, ESCPOS_LINE_ROW_BYTES, top + 24); /* at the left, as text is */
    for (int y = -top; y < 24; y++) {
        uint8_t *row = out + n;
        for (int b = 0; b < ESCPOS_LINE_ROW_BYTES; b++)
            row[b] = 0;
        int r = y / 3;
        if (y >= 0)
            for (int x = 0; x < TEXT_DOTS; x++)
                if (bits[r * bytesperline + (x >> 3)] >> (x & 7) & 1)
                    row[x >> 2] |= (uint8_t) (0xC0u >> (2 * (x & 3)));
        n += ESCPOS_LINE_ROW_BYTES;
    }
    return n;
}

int escpos_text_line(uint8_t *out, const char *text, int length, const uint8_t *bits, int bytesperline,
                     bool double_wide, bool *picture) {
    int step = double_wide ? 2 : 1; /* text bytes per character: double-wide adds a filler after each */
    int chars = length / step;
    if (chars > CHARS / step)
        chars = CHARS / step;
    bool after_picture = *picture;
    *picture = false;
    for (int i = 0; i < chars; i++)
        if (printer_char((uint8_t) text[i * step]) == 0) {
            *picture = true;
            return picture_line(out, bits, bytesperline, after_picture);
        }

    int n = 0;
    if (double_wide) {
        out[n++] = GS;
        out[n++] = '!';
        out[n++] = 0x10; /* characters twice as wide */
    }
    for (int i = 0; i < chars; i++)
        out[n++] = printer_char((uint8_t) text[i * step]);
    if (double_wide) {
        out[n++] = GS;
        out[n++] = '!';
        out[n++] = 0;
    }
    out[n++] = LF;
    return n;
}

int escpos_picture_header(uint8_t *out, int width, int height) {
    return raster_header(out, ESCPOS_ROW_BYTES(width), height);
}

static int raster_header(uint8_t *out, int row_bytes, int height) {
    out[0] = GS;
    out[1] = 'v';
    out[2] = '0';
    out[3] = 0; /* normal size */
    out[4] = (uint8_t) row_bytes;
    out[5] = (uint8_t) (row_bytes >> 8);
    out[6] = (uint8_t) height;
    out[7] = (uint8_t) (height >> 8);
    return 8;
}

int escpos_picture_row(uint8_t *out, const uint8_t *row, int width) {
    int margin = (ESCPOS_DOTS - width) / 2;
    int row_bytes = ESCPOS_ROW_BYTES(width);
    for (int b = 0; b < row_bytes; b++)
        out[b] = 0;
    for (int x = 0; x < width; x++)
        if (row[x >> 3] >> (7 - (x & 7)) & 1) {
            int d = margin + x;
            out[d >> 3] |= (uint8_t) (0x80u >> (d & 7));
        }
    return row_bytes;
}

int escpos_turned_row(uint8_t *out, const uint8_t *fb, int stride, int width, int height, int row) {
    int across = height * ESCPOS_TURNED_SCALE;
    int margin = (ESCPOS_DOTS - across) / 2;
    int row_bytes = ESCPOS_ROW_BYTES(across);
    int x = width - 1 - row / ESCPOS_TURNED_SCALE;
    for (int b = 0; b < row_bytes; b++)
        out[b] = 0;
    for (int y = 0; y < height; y++)
        if (fb[y * stride + (x >> 3)] >> (7 - (x & 7)) & 1)
            for (int k = 0; k < ESCPOS_TURNED_SCALE; k++) {
                int d = margin + y * ESCPOS_TURNED_SCALE + k;
                out[d >> 3] |= (uint8_t) (0x80u >> (d & 7));
            }
    return row_bytes;
}
