#include "hp82240.h"

#define ESC 27
#define LF 10          /* print the line, the printhead back at the left */
#define LF_GRAPHICS 4  /* print it, the printhead left at the right: the guide's linefeed for graphics,
                        * whose first dot column wanders when the printhead starts from the left */
#define ISO8859 249    /* ESC 249: the ISO 8859-1 character set (Roman8 at power-on) */
#define SINGLE_WIDE 252
#define DOUBLE_WIDE 253
#define CHARS 24       /* characters on a line, single-wide */

uint32_t hp82240_frame(uint8_t byte) {
    /* Error-correction bits 11..8: each is even parity over these data bits. */
    static const uint8_t groups[4] = {0x78, 0xE6, 0xD5, 0x8B};
    uint32_t word = byte;
    for (int i = 0; i < 4; i++)
        word |= (uint32_t) __builtin_parity(byte & groups[i]) << (11 - i);

    uint32_t halves = 7; /* the start: three bursts */
    for (int b = 11; b >= 0; b--)
        halves = halves << 2 | ((word >> b & 1) ? 2u : 1u);
    return halves << 5; /* then three empty half-bits, and bits 1..0 unused */
}

/* The printer's ISO 8859-1 code for an HP-42S character, or 0 when it has
 * no such character: those are printed as graphics. */
static uint8_t iso8859(uint8_t c) {
    switch (c) {
        case 0:  return 0xF7; /* ÷ */
        case 1:  return 0xD7; /* × */
        case 8:  return 0xBF; /* ¿ */
        case 17: return 0xB5; /* µ */
        case 18: return 0xA3; /* £ */
        case 19: return 0xB0; /* ° */
        case 20: return 0xC5; /* Å */
        case 21: return 0xD1; /* Ñ */
        case 22: return 0xC4; /* Ä */
        case 25: return 0xC6; /* Æ */
        case 28: return 0xD6; /* Ö */
        case 29: return 0xDC; /* Ü */
        case 30: return '^';
        case 94: return 0;    /* the HP-42S's up arrow */
        default: return c >= 32 && c <= 126 ? c : 0;
    }
}

/* Dot column x of a text line's bitmap as the printer takes it: the top
 * dot in the least significant bit. */
static uint8_t column(const uint8_t *bits, int bytesperline, int x) {
    uint8_t col = 0;
    for (int r = 0; r < 8; r++)
        if (bits[r * bytesperline + (x >> 3)] >> (x & 7) & 1)
            col |= (uint8_t) (1u << r);
    return col;
}

int hp82240_text_line(uint8_t *out, const char *text, int length, const uint8_t *bits, int bytesperline,
                      bool double_wide) {
    int step = double_wide ? 2 : 1; /* text bytes per character: double-wide adds a filler after each */
    int chars = length / step;
    if (chars > CHARS / step)
        chars = CHARS / step;

    int n = 0;
    out[n++] = ESC;
    out[n++] = ISO8859;
    out[n++] = ESC;
    out[n++] = double_wide ? DOUBLE_WIDE : SINGLE_WIDE;
    for (int i = 0; i < chars;) {
        uint8_t code = iso8859((uint8_t) text[i * step]);
        if (code != 0) {
            out[n++] = code;
            i++;
            continue;
        }
        /* A run of characters the printer lacks, as graphics in the shapes
         * Plus42 drew (its character x is at 6x, doubled in double-wide):
         * each the 7 columns the printer gives a character, a blank one,
         * 5 of the character and a blank one -- but no blank first at the
         * start of the line or last at its end, as the printer does with
         * text, or a full line wouldn't fit. */
        out[n++] = ESC;
        int count = n++;
        for (; i < chars && iso8859((uint8_t) text[i * step]) == 0; i++) {
            if (i > 0)
                out[n++] = 0;
            for (int j = 0; j < 5; j++)
                out[n++] = column(bits, bytesperline, (i * 6 + j) * step);
            if (i < chars - 1)
                out[n++] = 0;
        }
        out[count] = (uint8_t) (n - count - 1);
    }
    out[n++] = LF;
    return n;
}

/* Ends a graphics line whose count columns are already at out + 4. */
static int graphics_line(uint8_t *out, int count) {
    int used = count; /* up to the last column with a dot: the rest would only take time to send */
    while (used > 0 && out[4 + used - 1] == 0)
        used--;
    out[0] = ESC;
    out[1] = SINGLE_WIDE;
    if (used == 0) {
        out[2] = LF_GRAPHICS; /* a blank line: a graphics run can't be empty, just the linefeed */
        return 3;
    }
    out[2] = ESC;
    out[3] = (uint8_t) used;
    out[4 + used] = LF_GRAPHICS;
    return used + 5;
}

static bool pixel(const uint8_t *fb, int stride, int x, int y) {
    return fb[y * stride + (x >> 3)] & (0x80 >> (x & 7));
}

int hp82240_screen_line(uint8_t *out, const uint8_t *fb, int stride, int width, int height, int band) {
    if (height > HP82240_COLUMNS)
        height = HP82240_COLUMNS;
    for (int y = 0; y < height; y++) {
        uint8_t col = 0;
        for (int r = 0; r < 8; r++) {
            int x = width - 1 - (band * 8 + r);
            if (x >= 0 && pixel(fb, stride, x, y))
                col |= (uint8_t) (1u << r);
        }
        out[4 + y] = col;
    }
    return graphics_line(out, height);
}

int hp82240_upright_line(uint8_t *out, const uint8_t *fb, int stride, int width, int height, int band) {
    if (width > HP82240_COLUMNS)
        width = HP82240_COLUMNS;
    for (int x = 0; x < width; x++) {
        uint8_t col = 0;
        for (int r = 0; r < 8; r++) {
            int y = band * 8 + r;
            if (y < height && pixel(fb, stride, x, y))
                col |= (uint8_t) (1u << r);
        }
        out[4 + x] = col;
    }
    return graphics_line(out, width);
}
