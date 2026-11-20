#ifndef MENU_ICONS_H
#define MENU_ICONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The settings menu's 32 x 32 icons (settings_menu.c's grids). An app's
 * own icon comes from its .bin (app_icon(), app.h); these are the menu's. */
#define MENU_ICON_SIZE  32
#define MENU_ICON_BYTES (MENU_ICON_SIZE * MENU_ICON_SIZE / 8) /* rows of 4 bytes, MSB first, 1 = black */

typedef enum {
    MENU_ICON_OFF,
    MENU_ICON_SETTINGS,
    MENU_ICON_USB, /* a floppy disk: the drive */
    MENU_ICON_INFO,
    MENU_ICON_ALARMS,
    MENU_ICON_HELP,
    MENU_ICON_PLUS42,
    MENU_ICON_DATE_TIME,
    MENU_ICON_FONTS,
    MENU_ICON_SOUNDS,
    MENU_ICON_PRINTER,
    MENU_ICON_SPLASH,
    MENU_ICON_SLEEP,
    MENU_ICON_STATES,
    MENU_ICON_PROGRAMS,
    MENU_ICON_STYLE, /* Menu Style: icons or a list */
    MENU_ICON_APPS,  /* the Apps folder */
    MENU_ICON_GAMES, /* the Games folder */
    MENU_ICON_USB_CONNECT, /* USB Drive's page: a USB stick */
    MENU_ICON_USB_FORMAT,  /* an eraser rubbing the drive */
    MENU_ICON_USB_UPDATE,  /* a chip with a lightning bolt */
    MENU_ICON_RESET,       /* Settings → Reset: a restart arrow */
    MENU_ICON_FRAME, /* an app without its own icon: its first letter goes inside */
    MENU_ICON_COUNT
} menu_icon_t;

void menu_icon_bits(menu_icon_t icon, uint8_t bits[MENU_ICON_BYTES]);

#ifdef __cplusplus
}
#endif

#endif
