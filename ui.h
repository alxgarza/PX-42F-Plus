#ifndef UI_H
#define UI_H

/* The standard screens: lists with soft-key tabs, pull-downs, radio
 * buttons, check boxes, forms, text and number entry, the calendar and the
 * time picker. One look for the settings menu and the apps (px_app.h's
 * ui_*(), whose types these are). No hardware here: what it draws and the
 * keys it reads come through a port, so the same code runs in the firmware,
 * in the app simulator and in tests/test_ui.c. */

#include <stdbool.h>

#include "px_app.h"

/* What the port's key() returns besides a key's place, PX_KEY(row, col). */
#define UI_SHIFTED 0x200 /* | the place: SHIFT came before it */
#define UI_REDRAW  (-3)  /* nothing pressed, but redraw (the minute changed, SHIFT) */

typedef struct {
    void (*fill)(int x, int y, int w, int h, int black);
    /* PX_FONT_SMALL or PX_FONT_LARGE; returns the x after the text. */
    int (*text)(int x, int y, const char *s, int font, int inverse);
    int (*text_width)(const char *s, int font);
    void (*show)(void);
    /* Waits for a key that goes down: its place (| UI_SHIFTED), UI_REDRAW,
     * or PX_UI_QUIT. SHIFT itself is the port's: it comes as UI_REDRAW. */
    int (*key)(void);
    int (*shifted)(void);                /* SHIFT is waiting: the sign in the title bar */
    void (*clock)(char *buf, int size);  /* the time in the title bar; NULL: none */
    void (*beep)(void);                  /* a key that does nothing here */
    /* F6's word wherever a screen fills it in (lists, forms, numbers, the
     * calendar, the time, long text): NULL "EXIT" (apps); the settings
     * menu's "BACK" (the user's choice). It does what the EXIT key does. */
    const char *leave;
} ui_port_t;

/* The pieces, for screens of their own that look the same. */
#define UI_TITLE_H PX_UI_TOP
#define UI_ROW_Y0  15
#define UI_ROW_H   13
#define UI_ROWS    7
#define UI_TABS_Y  (PX_UI_BOTTOM + 2) /* under the dotted line at PX_UI_BOTTOM */
#define UI_CHAR_W  8

void ui_title(const ui_port_t *p, const char *title, const char *right);
void ui_tabs(const ui_port_t *p, const char *const *tabs);
/* The rows on the screen as ui_list() draws them: the y of `selected`'s. */
int ui_list_row_y(const px_ui_list_t *list);
void ui_list_draw(const ui_port_t *p, const px_ui_list_t *list);

int ui_message(const ui_port_t *p, const char *title, const char *const *lines, int count,
               const char *const *tabs);
int ui_list(const ui_port_t *p, px_ui_list_t *list);
int ui_form(const ui_port_t *p, px_ui_form_t *form);
int ui_pulldown(const ui_port_t *p, int y, int h, const char *const *items, int count, int *choice);
int ui_text(const ui_port_t *p, const char *title, const char *prompt, char *text, int max, int chars);
int ui_number(const ui_port_t *p, const char *title, const char *prompt, int *value, int min, int max);
/* Long text, wrapped at its spaces (\n a new line), a page at a time. */
int ui_view(const ui_port_t *p, const char *title, const char *text);
/* The same, F6 named leave ("BACK" on some settings screens). */
int ui_view_leave(const ui_port_t *p, const char *title, const char *text, const char *leave);
/* An alert over the screen, dimmed: icon (PX_UI_ICON_*), text, the left
 * button (the default: ENTER, F1) and the right (EXIT, <-, F6; NULL none). */
int ui_dialog(const ui_port_t *p, int icon, const char *text, const char *left, const char *right);
/* The same with 1-3 buttons, left to right, the ring first on buttons[ring]
 * (ENTER presses the ringed one, F1 the first, F6 EXIT <- the last): the
 * button's index, or PX_UI_QUIT. Not in the app API. */
int ui_dialog_buttons(const ui_port_t *p, int icon, const char *text, const char *const *buttons, int count,
                      int ring);
int ui_date(const ui_port_t *p, const char *title, int *year, int *month, int *day);
int ui_time(const ui_port_t *p, const char *title, const char *prompt, int *hours, int *minutes, int *seconds,
            int flags);

/* What a form's row shows for a field: the value, or `shown`. */
void ui_field_value(const px_ui_field_t *f, char *buf, int size);

/* The keys, by place: the letter typing gives (0 none), the digit (-1 none). */
char ui_key_letter(int place);
int ui_key_digit(int place);

#endif
