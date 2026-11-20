/* A USB thermal printer's ESC/POS bytes (escpos.c), on a computer.
 * tests/run.sh builds and runs it. With a file name, it also writes a
 * sample job there -- lines drawn with Plus42's own characters
 * (font_6x8), a picture -- for tools/usb_printer_test/yc_print.py to send
 * to a real printer. */
#include <stdio.h>
#include <string.h>

#include "escpos.h"
#include "font_6x8.h"

static int failures = 0;

static void expect_bytes(int line, const uint8_t *got, int n, const uint8_t *want, int wanted) {
    if (n != wanted || memcmp(got, want, (size_t) n) != 0) {
        fprintf(stderr, "line %d: got", line);
        for (int i = 0; i < n; i++)
            fprintf(stderr, " %d", got[i]);
        fprintf(stderr, "\n   wanted");
        for (int i = 0; i < wanted; i++)
            fprintf(stderr, " %d", want[i]);
        fprintf(stderr, "\n");
        failures++;
    }
}

#define EXPECT(got, n, ...)                                                                   \
    do {                                                                                      \
        static const uint8_t want_[] = {__VA_ARGS__};                                         \
        expect_bytes(__LINE__, got, n, want_, (int) sizeof(want_));                           \
    } while (0)

/* A print_text() bitmap: 143 x 9, 18 bytes a row. */
#define BPL 18
static uint8_t bits[BPL * 9];

/* The line's bitmap as Plus42 draws it: character i at dot column 6i
 * (doubled in double-wide), from font_6x8, Plus42's own shapes. */
static void draw_text(const char *text, int length, bool double_wide) {
    memset(bits, 0, sizeof bits);
    int step = double_wide ? 2 : 1;
    for (int i = 0; i < length / step; i++) {
        const uint8_t *g = font_6x8.data + (uint8_t) text[i * step] * font_6x8.bytes_per_char;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 5; x++) {
                int bit = y * 6 + x;
                if (g[bit >> 3] >> (7 - (bit & 7)) & 1)
                    for (int k = 0; k < step; k++) {
                        int d = (i * 6 + x) * step + k;
                        bits[y * BPL + (d >> 3)] |= (uint8_t) (1u << (d & 7));
                    }
            }
    }
}

/* What escpos_text_line() is told about the line before. */
static bool after_picture;

/* A string literal and its length: HP-42S text has 0 bytes (the divide sign). */
#define LIT(s) s, (int) sizeof(s) - 1

static int line(uint8_t *out, const char *text, int length, bool double_wide) {
    if (double_wide) { /* each character followed by a filler, as print_text() gives them */
        static char wide[48];
        for (int i = 0; i < length; i++) {
            wide[2 * i] = text[i];
            wide[2 * i + 1] = ' ';
        }
        text = wide;
        length *= 2;
    }
    draw_text(text, length, double_wide);
    return escpos_text_line(out, text, length, bits, BPL, double_wide, &after_picture);
}

static void test_text(void) {
    uint8_t out[ESCPOS_LINE_MAX];

    /* Plain ASCII goes as it is. */
    int n = line(out, LIT("1.5 ENTER"), false);
    EXPECT(out, n, '1', '.', '5', ' ', 'E', 'N', 'T', 'E', 'R', '\n');

    /* HP-42S characters code pages 437 and 850 both have: their bytes. 30 is
     * the caret. */
    n = line(out, LIT("\x00\x11\x13\x1e"), false);
    EXPECT(out, n, 0xF6, 0xE6, 0xF8, '^', '\n');

    /* Double-wide: the font twice as wide, the fillers dropped. */
    n = line(out, LIT("AB"), true);
    EXPECT(out, n, 0x1D, '!', 0x10, 'A', 'B', 0x1D, '!', 0, '\n');

    /* A character the printer may lack (Sigma: 850 has o-tilde there) makes
     * the whole line a picture: 36-byte rows, 24 of them, at the left. */
    after_picture = false;
    n = line(out, LIT("1 \x05"), false);
    if (n != 8 + 36 * 24 || !after_picture) {
        fprintf(stderr, "Sigma line: %d bytes, picture %d\n", n, after_picture);
        failures++;
    }
    EXPECT(out, 8, 0x1D, 'v', '0', 0, 36, 0, 24, 0);

    /* A picture after a picture: 6 blank rows first, 30 in all (the text
     * line spacing). A text line after it isn't one. */
    n = line(out, LIT("2 \x05"), false);
    EXPECT(out, 8, 0x1D, 'v', '0', 0, 36, 0, 30, 0);
    for (int i = 8; i < 8 + 36 * 6; i++)
        if (out[i] != 0) {
            fprintf(stderr, "the gap above a picture after a picture has dots\n");
            failures++;
            break;
        }
    line(out, LIT("3"), false);
    if (after_picture) {
        fprintf(stderr, "a text line counted as a picture\n");
        failures++;
    }

    /* The picture's dots: one dot of Plus42's bitmap (character 1's first
     * column, top row: x 6) is 2 across and 3 down -- dots 12-13 of rows
     * 0-2, byte 1's 0x0C -- and nothing else. */
    memset(bits, 0, sizeof bits);
    bits[0 * BPL + 0] = 1 << 6;
    bool before = false;
    n = escpos_text_line(out, "A\x0f", 2, bits, BPL, false, &before);
    for (int i = 8; i < n; i++) {
        int row = (i - 8) / 36, byte = (i - 8) % 36;
        uint8_t want = row < 3 && byte == 1 ? 0x0C : 0;
        if (out[i] != want) {
            fprintf(stderr, "picture line row %d byte %d: %02x, wanted %02x\n", row, byte, out[i], want);
            failures++;
            break;
        }
    }

    /* 94 is the HP-42S's up arrow: drawn, never the caret. */
    n = line(out, LIT("\x5e"), false);
    EXPECT(out, 3, 0x1D, 'v', '0');

    /* Longer than 24 characters: cut at 24, as the HP 82240B. */
    n = line(out, LIT("123456789012345678901234567"), false);
    if (n != 25) {
        fprintf(stderr, "long line: %d bytes\n", n);
        failures++;
    }
}

/* A picture as its users send it: the header, then each row. */
static int picture(uint8_t *out, const uint8_t *fb, int stride, int width, int height) {
    int n = escpos_picture_header(out, width, height);
    for (int y = 0; y < height; y++)
        n += escpos_picture_row(out + n, fb + y * stride, width);
    return n;
}

static void test_picture(void) {
    /* 8 x 2, the top left pixel on: centred, (384 - 8) / 2 = 188 dots in,
     * byte 23's 0x08; rows of (188 + 8 + 7) / 8 = 25 bytes. */
    uint8_t fb[2] = {0x80, 0x00};
    uint8_t out[8 + 25 * 2];
    int n = picture(out, fb, 1, 8, 2);
    if (n != 58 || ESCPOS_ROW_BYTES(8) != 25) {
        fprintf(stderr, "picture: %d bytes, rows of %d\n", n, ESCPOS_ROW_BYTES(8));
        failures++;
    }
    EXPECT(out, 8, 0x1D, 'v', '0', 0, 25, 0, 2, 0);
    for (int i = 0; i < 50; i++)
        if (out[8 + i] != (i == 23 ? 0x08 : 0)) {
            fprintf(stderr, "picture byte %d: %02x\n", i, out[8 + i]);
            failures++;
        }
    /* The screen, 250 wide: 67 dots in, rows of 40 bytes. */
    if (ESCPOS_ROW_BYTES(250) != 40) {
        fprintf(stderr, "screen rows of %d bytes\n", ESCPOS_ROW_BYTES(250));
        failures++;
    }
}

/* A job to try on a printer: every kind of line, and a picture. */
static void write_sample(const char *path) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        failures++;
        return;
    }
    static uint8_t out[8192];
    fwrite(ESCPOS_INIT, 1, ESCPOS_INIT_LEN, f);
    static const struct {
        const char *text;
        int length;
        bool wide;
    } lines[] = {
            {LIT("PX-42F ESC/POS sample"), false},
            {LIT("\x00\x11\x13\x08\x12\x14\x16 in its font"), false},
            {LIT("\x02\x05\x07\x09\x0b \x0f\x10\x5e\x0e\x06\x01\x0c\x17 drawn"), false},
            {LIT("01 \x06LBL \"AREA\""), false},
            {LIT("02 STO\x00 01"), false},
            {LIT("03 \x0fPOL"), false},
            {LIT("1.2345678901E-12   ***"), false},
            {LIT("BIG \x0f 2X"), true},
            {LIT("x: 3.14159265359"), false},
    };
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++)
        fwrite(out, 1, (size_t) line(out, lines[i].text, lines[i].length, lines[i].wide), f);
    /* A picture: a frame with a diagonal, 250 x 60 (the screen's width). */
    static uint8_t fb[60][32];
    memset(fb, 0, sizeof fb);
    for (int y = 0; y < 60; y++)
        for (int x = 0; x < 250; x++)
            if (y < 2 || y >= 58 || x < 2 || x >= 248 || x * 60 / 250 == y)
                fb[y][x >> 3] |= (uint8_t) (0x80u >> (x & 7));
    fwrite(out, 1, (size_t) picture(out, &fb[0][0], 32, 250, 60), f);
    fwrite(ESCPOS_FEED, 1, ESCPOS_FEED_LEN, f);
    fclose(f);
    printf("ESC/POS: sample job written to %s\n", path);
}

int main(int argc, char **argv) {
    test_text();
    test_picture();
    if (argc > 1)
        write_sample(argv[1]);
    if (failures) {
        fprintf(stderr, "ESC/POS: %d failures\n", failures);
        return 1;
    }
    printf("ESC/POS: all passed\n");
    return 0;
}
