/* Drawing into the framebuffer (display_fb.h): pixels, rectangles, text,
 * bitmaps. Nothing here touches the panel -- display.c sends the result --
 * so it builds on a computer too, for tools/menu_shots. */
#include "display_fb.h"

#include <string.h>

void display_clear(void) {
    memset(display_fb, 0, FB_SIZE);
}

void display_copy(uint8_t *bits) {
    memcpy(bits, display_fb, FB_SIZE);
}

void display_set_pixel(uint8_t x, uint8_t y, bool on) {
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT)
        return;
    uint8_t mask = (uint8_t) (0x80 >> (x & 7));
    if (on)
        display_fb[y][x >> 3] |= mask;
    else
        display_fb[y][x >> 3] &= (uint8_t) ~mask;
}

void display_fill_rect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool on) {
    for (int y = y0; y <= (int) y1; y++)
        for (int x = x0; x <= (int) x1; x++)
            display_set_pixel((uint8_t) x, (uint8_t) y, on);
}

/* Bounds-checked read, used only by the flush-time tile conversion (rows/
 * columns near the panel's bottom/right edge legitimately fall outside the
 * buffer -- see pack_tile_byte()) -- reads as "off" rather than needing a
 * separate edge case there. */
bool display_get_pixel(int x, int y) {
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT)
        return false;
    return (display_fb[y][x >> 3] & (0x80 >> (x & 7))) != 0;
}

/* Where the next character goes, held at the right edge: x is a uint8_t,
 * and a line running past it used to wrap round and overwrite its own
 * start (Set Time's "Now 11:42 AM. Enter on the minute"). */
static uint8_t next_x(const display_font_t *font, uint8_t x) {
    int next = x + font->advance;
    return (uint8_t) (next > DISPLAY_WIDTH ? DISPLAY_WIDTH : next);
}

uint8_t display_draw_char(const display_font_t *font, char c, uint8_t x, uint8_t y, uint8_t color) {
    if ((uint8_t) c < font->first_char || (uint8_t) c > font->last_char)
        return next_x(font, x); /* not in the font -- skip but still advance */

    const uint8_t *glyph = font->data + ((uint8_t) c - font->first_char) * font->bytes_per_char;
    bool invert = color != 0;
    int bit_index = 0;

    for (uint8_t row = 0; row < font->height; row++) {
        for (uint8_t col = 0; col < font->width; col++) {
            uint8_t byte = glyph[bit_index >> 3];
            bool set = ((byte >> (7 - (bit_index & 7))) & 1) != 0;
            bit_index++;

            /* Compute the target position in a wider type before handing
             * it to display_set_pixel(): x/y are uint8_t, and near the
             * right/bottom edge x + col can exceed 255 (e.g. x=249,
             * col=15), which would silently wrap back on-screen instead
             * of clipping if added directly in uint8_t. */
            int px = (int) x + col;
            int py = (int) y + row;
            if (px >= 0 && px < DISPLAY_WIDTH && py >= 0 && py < DISPLAY_HEIGHT)
                display_set_pixel((uint8_t) px, (uint8_t) py, set != invert);
        }
    }

    return next_x(font, x);
}

uint8_t display_draw_string(const display_font_t *font, const char *s, uint8_t x, uint8_t y, uint8_t color) {
    while (*s && x < DISPLAY_WIDTH)
        x = display_draw_char(font, *s++, x, y, color);
    return x;
}

uint8_t display_draw_chars(const display_font_t *font, const char *s, int len, uint8_t x, uint8_t y, uint8_t color) {
    for (int i = 0; i < len && x < DISPLAY_WIDTH; i++)
        x = display_draw_char(font, s[i], x, y, color);
    return x;
}

bool display_text_ink(const display_font_t *font, const char *s, int *left, int *right) {
    bool any = false;
    for (int i = 0; s[i]; i++) {
        uint8_t c = (uint8_t) s[i];
        if (c < font->first_char || c > font->last_char)
            continue;
        const uint8_t *glyph = font->data + (c - font->first_char) * font->bytes_per_char;
        for (int col = 0; col < font->width; col++)
            for (int row = 0; row < font->height; row++) {
                int bit = row * font->width + col;
                if ((glyph[bit >> 3] >> (7 - (bit & 7))) & 1) {
                    int x = i * font->advance + col;
                    if (!any || x < *left)
                        *left = x;
                    if (!any || x > *right)
                        *right = x;
                    any = true;
                    break;
                }
            }
    }
    return any;
}

void display_draw_bitmap(const uint8_t *bits, int width, int height, int x, int y) {
    int stride = (width + 7) / 8;
    for (int row = 0; row < height; row++)
        for (int col = 0; col < width; col++) {
            int px = x + col, py = y + row;
            if (px >= 0 && px < DISPLAY_WIDTH && py >= 0 && py < DISPLAY_HEIGHT)
                display_set_pixel((uint8_t) px, (uint8_t) py,
                                  (bits[row * stride + col / 8] & (0x80 >> (col & 7))) != 0);
        }
}

void display_draw_dotted_hline(uint8_t x0, uint8_t x1, uint8_t y) {
    for (int x = x0; x <= (int) x1; x += 2)
        display_set_pixel((uint8_t) x, y, true);
}

