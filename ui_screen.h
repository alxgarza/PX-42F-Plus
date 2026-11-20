#ifndef UI_SCREEN_H
#define UI_SCREEN_H

#include "display.h"

/* Drawing on the display as px_app.h's pixel(), fill() and text() do --
 * clipped to the screen, any coordinates -- for the apps' table and for
 * the standard screens' ports (ui.h). No hardware: the menu_shots tool
 * builds it too. */

void screen_pixel(int x, int y, int black);
void screen_fill(int x, int y, int w, int h, int black);
const display_font_t *screen_font(int font); /* PX_FONT_*; NULL for none */
int screen_text(int x, int y, const char *s, int font, int inverse);
int screen_text_width(const char *s, int font);

#endif
