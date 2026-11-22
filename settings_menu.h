#ifndef SETTINGS_MENU_H
#define SETTINGS_MENU_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETTINGS_MENU_EXIT,      /* back to the calculator */
    SETTINGS_MENU_POWER_OFF, /* the Off item, SHIFT+EXIT, or the sleep timer ran out */
} settings_menu_result_t;

/* The PX-42 settings menu, opened with SHIFT+OFF (SHIFT+EXIT). Runs its own
 * key loop until the user leaves it, feeding the watchdog throughout, and
 * draws over the whole screen -- the caller redraws the calculator after
 * SETTINGS_MENU_EXIT.
 *
 * ▲/▼ move the highlight, ENTER opens or toggles the highlighted item, EXIT
 * goes back, and SHIFT+EXIT (OFF) turns the calculator off from anywhere
 * in the menu. Numbers are typed on the digit keys, BSP deletes. */
settings_menu_result_t settings_menu_run(void);

/* ui_dialog_buttons() (ui.h) drawn and answered as the settings menu's
 * screens are, for a question outside it (the screenshot's), the first
 * button ringed; keys already down are let go before one is read. The
 * button's index, or PX_UI_QUIT. */
int settings_menu_dialog(int icon, const char *text, const char *const *buttons, int count);

#ifdef __cplusplus
}
#endif

#endif
