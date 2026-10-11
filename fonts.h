#ifndef FONTS_H
#define FONTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The calculator screen's two fonts: the small one (the status bar and the
 * menu labels, 8 x 12) and the large one (the stack and everything else,
 * 10 x 16). Each is the built-in font (fonts/font_8x12, font_10x16) or a
 * font sheet from the drive, chosen in Settings -> Fonts:
 *
 *   /FONTS/SMALL/<name>.png   128 x 96:  16 x 8 glyphs of 8 x 12, no gaps
 *   /FONTS/LARGE/<name>.png   175 x 135: 16 x 8 glyphs of 10 x 16, 1 pixel
 *                             between them
 *
 * (fontsheet.h; the sizes are fixed -- the screen layout is built on them.
 * The built-in fonts' own sheets, to start a new one from, are
 * fonts/font_12x8.png and fonts/font_10x16.png in the source.)
 *
 * Only free42_app.cc draws with these. The settings menu, the boot and
 * crash screens and the splash clock keep the built-in fonts: the menu has
 * to stay readable to undo a font that isn't, and the splash clock is
 * redrawn while off, with no drive to load a font from. */
extern display_font_t font_small, font_large;

#define FONTS_DIR     "/FONTS"
#define FONT_NAME_MAX 28

typedef enum { FONT_SMALL, FONT_LARGE, FONT_KINDS } font_kind_t;

/* After statefs_init(), and again after USB Drive mode: makes the /FONTS
 * folders if they're missing and loads the chosen fonts (px_settings); one
 * that can't be loaded gives the built-in font. */
void fonts_init(void);

/* Switches to font `name` ("" = built-in). False with the reason in error
 * if the file can't be used -- the font then stays as it was. */
bool fonts_select(font_kind_t kind, const char *name, char *error, size_t error_size);

/* The sheets on the drive for a font, sorted; returns how many. */
int fonts_list(font_kind_t kind, char names[][FONT_NAME_MAX + 1], int max);

#ifdef __cplusplus
}
#endif

#endif
