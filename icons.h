#ifndef ICONS_H
#define ICONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The status bar's battery gauge, drawn white on its black background:
 * an outline with a terminal, 18 x 10, and level (0-4) bars inside
 * (battery_level()). Drawn here rather than from a font, so every font
 * code stays the HP-42S character Free42 means by it, and a font loaded
 * from the drive (fonts.h) can't break it. (x, y) is the top left of the
 * 8 x 12 font cell it lines up with. */
void icon_battery(uint8_t x, uint8_t y, int level);

#ifdef __cplusplus
}
#endif

#endif
