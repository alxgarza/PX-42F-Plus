/* The HP 82240B printer's bytes and infrared frames (hp82240.c), on a
 * computer. tests/run.sh builds and runs it. */
#include <stdio.h>
#include <string.h>

#include "hp82240.h"

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

/* Draws dot column x as the printer would get it (top dot in bit 0). */
static void draw_column(int x, uint8_t col) {
    for (int y = 0; y < 8; y++)
        if (col >> y & 1)
            bits[y * BPL + (x >> 3)] |= (uint8_t) (1u << (x & 7));
}

/* The 12 data and error-correction bits a frame carries, from its half-bits. */
static int frame_word(uint32_t frame) {
    int word = 0;
    for (int b = 0; b < 12; b++)
        word = word << 1 | (int) (frame >> (28 - 2 * b) & 1);
    return word;
}

int main(void) {
    /* The guide's example: "A" is the start, then 110101000001. */
    if (hp82240_frame('A') != 0xF4CCAAC0u) {
        fprintf(stderr, "frame of 'A' is %08lx\n", (unsigned long) hp82240_frame('A'));
        failures++;
    }
    /* The printer corrects one wrong bit: every two bytes' frames differ
     * in at least 3 of their 12 bits. */
    for (int a = 0; a < 256; a++)
        for (int b = a + 1; b < 256; b++)
            if (__builtin_popcount((unsigned) (frame_word(hp82240_frame((uint8_t) a)) ^
                                               frame_word(hp82240_frame((uint8_t) b)))) < 3) {
                fprintf(stderr, "frames of %d and %d differ in under 3 bits\n", a, b);
                failures++;
            }

    uint8_t out[HP82240_LINE_MAX];
    int n;

    n = hp82240_text_line(out, "AB", 2, bits, BPL, false);
    EXPECT(out, n, 27, 249, 27, 252, 'A', 'B', 10);
    n = hp82240_text_line(out, "", 0, bits, BPL, false);
    EXPECT(out, n, 27, 249, 27, 252, 10);
    n = hp82240_text_line(out, "1\0002\001\0233^", 7, bits, BPL, false); /* 19 = °, 94 = up arrow */
    EXPECT(out, n, 27, 249, 27, 252, '1', 0xF7, '2', 0xD7, 0xB0, '3', 27, 6, 0, 0, 0, 0, 0, 0, 10);

    /* A square root (2) the printer lacks, its shape at character 1 (x = 6..10). */
    for (int j = 0; j < 5; j++)
        draw_column(6 + j, (uint8_t) (0x11 << (j % 4)));
    n = hp82240_text_line(out, "1\0022", 3, bits, BPL, false);
    EXPECT(out, n, 27, 249, 27, 252, '1', 27, 7, 0, 0x11, 0x22, 0x44, 0x88, 0x11, 0, '2', 10);
    n = hp82240_text_line(out, "1\002", 2, bits, BPL, false);
    EXPECT(out, n, 27, 249, 27, 252, '1', 27, 6, 0, 0x11, 0x22, 0x44, 0x88, 0x11, 10);
    n = hp82240_text_line(out, "\002\002", 2, bits, BPL, false);
    EXPECT(out, n, 27, 249, 27, 252, 27, 12, 0, 0, 0, 0, 0, 0, 0, 0x11, 0x22, 0x44, 0x88, 0x11, 10);

    /* Double-wide: a filler after each character, columns at twice the x. */
    memset(bits, 0, sizeof(bits));
    for (int j = 0; j < 5; j++)
        draw_column((6 + j) * 2, (uint8_t) (1u << j));
    n = hp82240_text_line(out, "A_\002 ", 4, bits, BPL, true);
    EXPECT(out, n, 27, 249, 27, 253, 'A', 27, 6, 0, 1, 2, 4, 8, 16, 10);

    /* A full line of characters the printer lacks: 166 columns, the most a line takes. */
    char roots[24];
    memset(roots, 2, sizeof(roots));
    n = hp82240_text_line(out, roots, 24, bits, BPL, false);
    if (n != HP82240_LINE_MAX || out[5] != 166) {
        fprintf(stderr, "24 square roots: %d bytes, a run of %d columns\n", n, out[5]);
        failures++;
    }

    /* The screen turned a quarter: line 0 is the right edge's 8 columns,
     * along it the rows, the top first. 16 x 3 pixels, 2 bytes a row,
     * the leftmost pixel in the top bit. */
    uint8_t fb[2 * 3] = {0};
    fb[0 * 2 + 1] = 0x01; /* (15, 0) */
    fb[2 * 2 + 1] = 0x02; /* (14, 2) */
    fb[1 * 2 + 0] = 0x80; /* (0, 1) */
    n = hp82240_screen_line(out, fb, 2, 16, 3, 0);
    EXPECT(out, n, 27, 252, 27, 3, 0x01, 0, 0x02, 4);
    n = hp82240_screen_line(out, fb, 2, 16, 3, 1);
    EXPECT(out, n, 27, 252, 27, 2, 0, 0x80, 4);
    fb[1 * 2 + 0] = 0;
    n = hp82240_screen_line(out, fb, 2, 16, 3, 1);
    EXPECT(out, n, 27, 252, 4);

    /* Upright: line 0 is rows 0-7 across, the top row in the bottom bit;
     * line 1 is past the 3 rows, blank. */
    fb[1 * 2 + 0] = 0x80; /* (0, 1) */
    n = hp82240_upright_line(out, fb, 2, 16, 3, 0);
    EXPECT(out, n, 27, 252, 27, 16, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0x01, 4);
    n = hp82240_upright_line(out, fb, 2, 16, 3, 1);
    EXPECT(out, n, 27, 252, 4);

    if (failures == 0)
        printf("HP 82240B: frames and lines good\n");
    return failures != 0;
}
