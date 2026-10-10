#ifndef SETTINGS_MENU_H
#define SETTINGS_MENU_H

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

#ifdef __cplusplus
}
#endif

#endif
