#include "ui_screen.h"

#include <string.h>

#include "display.h"
#include "font_10x16.h"
#include "font_6x8.h"
#include "font_8x12.h"
#include "fonts.h"
#include "px_app.h"

void screen_pixel(int x, int y, int black) {
    if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT)
        display_set_pixel((uint8_t) x, (uint8_t) y, black != 0);
}

void screen_fill(int x, int y, int w, int h, int black) {
    int x1 = x + w - 1, y1 = y + h - 1;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 >= DISPLAY_WIDTH)
        x1 = DISPLAY_WIDTH - 1;
    if (y1 >= DISPLAY_HEIGHT)
        y1 = DISPLAY_HEIGHT - 1;
    if (x <= x1 && y <= y1)
        display_fill_rect((uint8_t) x, (uint8_t) y, (uint8_t) x1, (uint8_t) y1, black != 0);
}

const display_font_t *screen_font(int font) {
    switch (font) {
        case PX_FONT_SMALL: return &font_8x12;
        case PX_FONT_LARGE: return &font_10x16;
        case PX_FONT_MINI: return &font_6x8;
        case PX_FONT_CHOSEN_SMALL: return &font_small;
        case PX_FONT_CHOSEN_LARGE: return &font_large;
        case PX_FONT_CHOSEN_MINI: return &font_mini;
        default: return NULL;
    }
}

int screen_text(int x, int y, const char *s, int font, int inverse) {
    const display_font_t *f = screen_font(font);
    if (f == NULL)
        return x;
    /* display_draw_char() takes 0-255 and clips each pixel: skip what's
     * left of the screen, stop at its right edge. */
    for (; *s != 0 && x < DISPLAY_WIDTH; s++) {
        if (x > -f->advance && y > -f->height && y < DISPLAY_HEIGHT) {
            if (x >= 0 && y >= 0) {
                display_draw_char(f, *s, (uint8_t) x, (uint8_t) y, inverse ? 0xFF : 0x00);
            } else { /* partly off the top or left: pixel by pixel */
                uint8_t c = (uint8_t) *s;
                if (c >= f->first_char && c <= f->last_char) {
                    const uint8_t *glyph = f->data + (c - f->first_char) * f->bytes_per_char;
                    for (int bit = 0; bit < f->width * f->height; bit++)
                        screen_pixel(x + bit % f->width, y + bit / f->width,
                                  ((glyph[bit / 8] >> (7 - bit % 8)) & 1) ^ (inverse != 0));
                }
            }
        }
        x += f->advance;
    }
    return x;
}

int screen_text_width(const char *s, int font) {
    const display_font_t *f = screen_font(font);
    return f == NULL ? 0 : (int) strlen(s) * f->advance;
}
