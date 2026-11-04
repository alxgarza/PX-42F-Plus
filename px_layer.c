#include "px_layer.h"

static int on_canvas(const px_canvas_t *c, int x, int y) {
    return x >= 0 && x < c->w && y >= 0 && y < c->h;
}

static int get_dot(const px_canvas_t *c, int x, int y) {
    return c->bits[y * c->stride + x / 8] >> (7 - x % 8) & 1;
}

static void put_dot(const px_canvas_t *c, int x, int y, int on) {
    uint8_t *byte = &c->bits[y * c->stride + x / 8];
    uint8_t mask = (uint8_t) (0x80 >> (x % 8));
    *byte = on ? (uint8_t) (*byte | mask) : (uint8_t) (*byte & ~mask);
}

void px_layer_dot(const px_canvas_t *ink, const px_canvas_t *wipe, int x, int y, int black) {
    if (!on_canvas(ink, x, y))
        return;
    put_dot(ink, x, y, black);
    put_dot(wipe, x, y, !black);
}

void px_layer_pattern(const px_canvas_t *ink, const px_canvas_t *wipe, int x, int y, const char *pattern,
                      int width, px_pattern_mode_t mode) {
    for (int h = 0; h < width; h++) {
        unsigned char column = (unsigned char) pattern[h];
        for (int v = 0; v < 8; v++, column >>= 1) {
            int dot = column & 1, X = x + h, Y = y + v;
            if (!on_canvas(ink, X, Y))
                continue;
            switch (mode) {
                case PX_PATTERN_OR:
                    if (dot)
                        px_layer_dot(ink, wipe, X, Y, 1);
                    break;
                case PX_PATTERN_REPLACE:
                    px_layer_dot(ink, wipe, X, Y, dot);
                    break;
                case PX_PATTERN_CLEAR:
                    if (dot)
                        px_layer_dot(ink, wipe, X, Y, 0);
                    break;
                case PX_PATTERN_XOR:
                    if (dot)
                        px_layer_dot(ink, wipe, X, Y, !get_dot(ink, X, Y));
                    break;
            }
        }
    }
}

void px_layer_compose(uint8_t *picture, const px_canvas_t *ink, const px_canvas_t *wipe) {
    int size = ink->h * ink->stride;
    for (int i = 0; i < size; i++)
        picture[i] = (uint8_t) ((picture[i] & ~wipe->bits[i]) | ink->bits[i]);
}
