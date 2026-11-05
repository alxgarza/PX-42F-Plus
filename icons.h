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
 * from the drive (fonts.h) can't break it. (x, y) is its top left. */
void icon_battery(uint8_t x, uint8_t y, int level);

/* A bell, 8 x 10, the same way: alarms are set (alarms.h). */
void icon_bell(uint8_t x, uint8_t y);

/* Plus42's and the HP-42S's print annunciator, in the same 10 rows, the same way:
 * sending to the printer. */
#define ICON_PRINTER_W 21
void icon_printer(uint8_t x, uint8_t y);

#ifdef __cplusplus
}
#endif

#endif
