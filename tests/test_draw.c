/* The shapes of version 5 of the app table (px_draw.c), on a computer, as
 * pictures: what's drawn against what it should look like. tests/run.sh
 * builds and runs it. */
#include <stdio.h>
#include <string.h>

#include "px_draw.h"
#include "px_layer.h"

static uint8_t bits[4 * 24];
static const px_canvas_t canvas = {bits, 32, 24, 4};
static int failures;

static int pixel(int x, int y) {
    return bits[y * canvas.stride + x / 8] >> (7 - x % 8) & 1;
}

/* The canvas against rows of # and ., from the top; then it's cleared. */
static void expect(const char *name, const char *const *rows, int count) {
    int ok = 1;
    for (int y = 0; y < canvas.h; y++) {
        for (int x = 0; x < canvas.w; x++) {
            int want = y < count && x < (int) strlen(rows[y]) && rows[y][x] == '#';
            ok &= pixel(x, y) == want;
        }
    }
    if (!ok) {
        failures++;
        printf("FAIL %s:\n", name);
        for (int y = 0; y < canvas.h; y++) {
            for (int x = 0; x < canvas.w; x++)
                putchar(pixel(x, y) ? '#' : '.');
            putchar('\n');
        }
    }
    memset(bits, 0, sizeof(bits));
}

#define EXPECT(name, ...)                                  \
    do {                                                   \
        static const char *const rows[] = {__VA_ARGS__};   \
        expect(name, rows, sizeof(rows) / sizeof(rows[0])); \
    } while (0)

static int count_pixels(void) {
    int n = 0;
    for (int y = 0; y < canvas.h; y++)
        for (int x = 0; x < canvas.w; x++)
            n += pixel(x, y);
    return n;
}

static void boxes(void) {
    px_draw_box(&canvas, 1, 0, 8, 5, 0, 1);
    EXPECT("filled box", ".########", ".########", ".########", ".########", ".########");
    px_draw_box(&canvas, 1, 0, 8, 5, 1, 1);
    EXPECT("one-pixel box", ".########", ".#......#", ".#......#", ".#......#", ".########");
    px_draw_box(&canvas, 1, 0, 8, 6, 2, 1);
    EXPECT("box grows inward", ".########", ".########", ".##....##", ".##....##", ".########", ".########");
    px_draw_box(&canvas, 0, 0, 4, 3, 9, 1);
    EXPECT("thicker than the box: filled", "####", "####", "####");
    px_draw_box(&canvas, -2, -1, 4, 3, 0, 1);
    EXPECT("box off the corner", "##", "##");
    px_draw_box(&canvas, 0, 0, 0, 5, 0, 1);
    px_draw_box(&canvas, 0, 0, -3, 5, 0, 1);
    EXPECT("box with no size");
}

static void ellipses(void) {
    px_draw_ellipse(&canvas, 6, 5, 5, 5, 1, 1);
    EXPECT("circle", "....#####", "...#.....#", "..#.......#", ".#.........#", ".#.........#", ".#.........#",
           ".#.........#", ".#.........#", "..#.......#", "...#.....#", "....#####");
    px_draw_ellipse(&canvas, 6, 5, 5, 5, 2, 1);
    EXPECT("thick circle", "....#####", "...#######", "..###...###", ".###.....###", ".##.......##", ".##.......##",
           ".##.......##", ".###.....###", "..###...###", "...#######", "....#####");
    px_draw_ellipse(&canvas, 6, 5, 5, 5, 0, 1);
    EXPECT("disc", "....#####", "...#######", "..#########", ".###########", ".###########", ".###########",
           ".###########", ".###########", "..#########", "...#######", "....#####");
    px_draw_ellipse(&canvas, 10, 3, 9, 3, 1, 1);
    EXPECT("ellipse", "......#########", "...###.........###", ".##...............##", ".#.................#",
           ".##...............##", "...###.........###", "......#########");
    px_draw_ellipse(&canvas, 1, 1, 1, 1, 1, 1);
    EXPECT("radius 1", "###", "#.#", "###");
    px_draw_ellipse(&canvas, 2, 0, 0, 0, 1, 1);
    EXPECT("radius 0", "..#");
    px_draw_ellipse(&canvas, 5, 5, -1, 3, 0, 1);
    EXPECT("negative radius");
}

/* Flooded from the centre through the four neighbours, a ring with a gap
 * lets it out to the canvas's corner. */
static int ring_leaks(int x, int y, uint8_t *seen) {
    if (x < 0 || y < 0 || x >= canvas.w || y >= canvas.h || seen[y * canvas.w + x] || pixel(x, y))
        return 0;
    if (x == 0 && y == 0)
        return 1;
    seen[y * canvas.w + x] = 1;
    return ring_leaks(x + 1, y, seen) | ring_leaks(x - 1, y, seen) | ring_leaks(x, y + 1, seen) | ring_leaks(x, y - 1, seen);
}

static void rings_are_closed(void) {
    for (int rx = 1; rx <= 11; rx++) {
        for (int ry = 1; ry <= 11; ry++) {
            uint8_t seen[32 * 24] = {0};
            px_draw_ellipse(&canvas, 15, 12, rx, ry, 1, 1);
            if (rx > 1 && ry > 1 && ring_leaks(15, 12, seen)) {
                failures++;
                printf("FAIL ring %d,%d has a gap\n", rx, ry);
            }
            memset(bits, 0, sizeof(bits));
        }
    }
}

static void lines(void) {
    px_draw_line(&canvas, 0, 0, 5, 2, 1, 1);
    EXPECT("thin line", "##", "..##", "....##");
    px_draw_line(&canvas, 0, 0, 5, 2, 0, 1);
    EXPECT("t 0 is a thin line", "##", "..##", "....##");
    px_draw_line(&canvas, 1, 1, 8, 1, 3, 1);
    EXPECT("thick line", "##########", "##########", "##########");
    px_draw_line(&canvas, 3, 3, 3, 3, 5, 1);
    EXPECT("a thick dot", "", "..###", ".#####", ".#####", ".#####", "..###");
}

static void polygons(void) {
    const px_point_t triangle[3] = {{1, 9}, {9, 0}, {17, 9}};
    px_draw_polygon(&canvas, triangle, 3, 0, 1);
    EXPECT("filled triangle", ".........#", "........###", ".......#####", "......#######", ".....#########",
           ".....#########", "....###########", "...#############", "..###############", ".#################");
    px_draw_polygon(&canvas, triangle, 3, 1, 1);
    EXPECT("triangle", ".........#", "........#.#", ".......#...#", "......#.....#", ".....#.......#",
           ".....#.......#", "....#.........#", "...#...........#", "..#.............#", ".#################");
    const px_point_t notch[5] = {{1, 0}, {15, 0}, {8, 5}, {15, 10}, {1, 10}};
    px_draw_polygon(&canvas, notch, 5, 0, 1);
    EXPECT("concave", ".###############", ".##############", ".############", ".###########", ".##########",
           ".########", ".##########", ".###########", ".############", ".##############", ".###############");
    const px_point_t one[1] = {{4, 2}};
    px_draw_polygon(&canvas, one, 1, 1, 1);
    EXPECT("one corner", "", "", "....#");
    px_draw_polygon(&canvas, triangle, 0, 0, 1);
    px_draw_polygon(&canvas, triangle, PX_POLY_MAX + 1, 0, 1);
    EXPECT("no corners, or too many");
}

static void white_erases(void) {
    px_draw_box(&canvas, 0, 0, 8, 3, 0, 1);
    px_draw_box(&canvas, 2, 1, 4, 1, 0, 0);
    EXPECT("white box", "########", "##....##", "########");
    px_draw_box(&canvas, 0, 0, 5, 5, 0, 1);
    px_draw_ellipse(&canvas, 2, 2, 1, 1, 0, 0);
    EXPECT("white disc", "#####", "#...#", "#...#", "#...#", "#####");
    const px_point_t triangle[3] = {{9, 0}, {0, 9}, {18, 9}};
    px_draw_polygon(&canvas, triangle, 3, 0, 1);
    px_draw_polygon(&canvas, triangle, 3, 0, 0);
    EXPECT("polygon drawn, then erased");
}

/* Nothing here may touch memory outside the canvas (AddressSanitizer), and
 * these ends are the int limits a BASIC program can pass. */
static void far_off_the_canvas(void) {
    const int big = 2147483647;
    px_draw_box(&canvas, -big, -big, big, big, 3, 1);
    px_draw_box(&canvas, big, big, big, big, 0, 1);
    px_draw_ellipse(&canvas, big, -big, big, big, 3, 1);
    px_draw_ellipse(&canvas, -big, big, big, 1, 0, 1);
    px_draw_ellipse(&canvas, 15, 12, 5, 5, big, 1);
    memset(bits, 0, sizeof(bits));
    px_draw_line(&canvas, -big, -big, big, big, 1, 1);
    if (count_pixels() == 0) {
        failures++;
        puts("FAIL a line through the canvas drew nothing");
    }
    memset(bits, 0, sizeof(bits));
    px_draw_line(&canvas, -big, -big, big, big, big, 1);
    px_draw_line(&canvas, 0, big, 0, big, 3, 1);
    memset(bits, 0, sizeof(bits));
    px_draw_box(&canvas, 1, 1, 4, 2, big, 1);
    EXPECT("box: the thickest is filled", "", ".####", ".####");
    px_draw_box(&canvas, 1, 1, 4, 2, 1 << 30, 1);
    EXPECT("box: 2 t mustn't overflow", "", ".####", ".####");
    px_draw_box(&canvas, 1, 1, 4, 2, -big, 1);
    EXPECT("box: negative is filled", "", ".####", ".####");
    px_draw_ellipse(&canvas, 1, 1, 1, 1, -big, 1);
    EXPECT("ellipse: negative is filled", "###", "###", "###");
    px_draw_ellipse(&canvas, 1, 1, 1, 1, big, 1);
    EXPECT("ellipse: the thickest is filled", "###", "###", "###");
    memset(bits, 0, sizeof(bits));
    px_draw_line(&canvas, -9000, -500, -9000, 500, 9, 1);
    EXPECT("a line off the canvas");
    px_point_t corners[PX_POLY_MAX];
    for (int i = 0; i < PX_POLY_MAX; i++) {
        corners[i].x = i % 2 ? -32768 : 32767;
        corners[i].y = (int16_t) (i * 1000 - 32000);
    }
    px_draw_polygon(&canvas, corners, PX_POLY_MAX, 0, 1);
    px_draw_polygon(&canvas, corners, PX_POLY_MAX, 1, 1);
    px_draw_polygon(&canvas, corners, PX_POLY_MAX, 64, 1);
    memset(bits, 0, sizeof(bits));
}

/* PIXEL and AGRAPH: 0x05 is dots at the top and two below it. */
static void layer(void) {
    static uint8_t wipe_bits[4 * 24], picture[4 * 24];
    const px_canvas_t wipe = {wipe_bits, 32, 24, 4};
    px_layer_dot(&canvas, &wipe, 3, 1, 1);
    px_layer_dot(&canvas, &wipe, 32, 1, 1);
    px_layer_dot(&canvas, &wipe, -1, 0, 1);
    EXPECT("a dot, and two off the canvas", "", "...#");
    memset(wipe_bits, 0, sizeof(wipe_bits));

    px_layer_pattern(&canvas, &wipe, 1, 0, "\x05\x02", 2, PX_PATTERN_OR);
    EXPECT("pattern, OR", ".#", "..#", ".#");
    memset(wipe_bits, 0, sizeof(wipe_bits));

    px_draw_box(&canvas, 0, 0, 3, 3, 0, 1);
    px_layer_pattern(&canvas, &wipe, 1, 0, "\x05", 1, PX_PATTERN_REPLACE);
    EXPECT("pattern, replace", "###", "#.#", "###");
    memset(wipe_bits, 0, sizeof(wipe_bits));

    px_draw_box(&canvas, 0, 0, 3, 3, 0, 1);
    px_layer_pattern(&canvas, &wipe, 1, 0, "\x05", 1, PX_PATTERN_CLEAR);
    EXPECT("pattern, clear", "#.#", "###", "#.#");
    memset(wipe_bits, 0, sizeof(wipe_bits));

    px_draw_box(&canvas, 0, 0, 2, 1, 0, 1);
    px_layer_pattern(&canvas, &wipe, 1, 0, "\x05\x01", 2, PX_PATTERN_XOR);
    EXPECT("pattern, XOR", "#.#", "", ".#");
    memset(wipe_bits, 0, sizeof(wipe_bits));

    px_layer_pattern(&canvas, &wipe, -1, -2, "\xff\xff", 2, PX_PATTERN_OR);
    EXPECT("pattern off the corner", "#", "#", "#", "#", "#", "#");
    memset(wipe_bits, 0, sizeof(wipe_bits));

    /* White over black text, black over white, and the rest as it was. */
    memset(picture, 0, sizeof(picture));
    picture[0] = 0xF0;
    px_layer_dot(&canvas, &wipe, 0, 0, 0);
    px_layer_dot(&canvas, &wipe, 7, 0, 1);
    px_layer_compose(picture, &canvas, &wipe);
    if (picture[0] != 0x71) {
        failures++;
        printf("FAIL compose: %02x\n", picture[0]);
    }
    memset(bits, 0, sizeof(bits));
}

int main(void) {
    layer();
    boxes();
    ellipses();
    rings_are_closed();
    lines();
    polygons();
    white_erases();
    far_off_the_canvas();
    if (failures) {
        printf("test_draw: %d FAILED\n", failures);
        return 1;
    }
    puts("test_draw: all passed");
    return 0;
}
