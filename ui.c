#include "ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ===================== keys ===================== */

/* The keys' letters by place, as the calculator's ALPHA mode types them
 * (keymap.h's alpha_key_matrix, which tests/test_ui.c holds this to). */
static const char letters[7][6] = {
    {'A', 'B', 'C', 'D', 'E', 'F'},
    {'G', 'H', 'I', 'J', 'K', 'L'},
    {0, 'M', 'N', 'O', 0, 0},
    {0, 'P', 'Q', 'R', 'S', 0},
    {0, 'T', 'U', 'V', 'W', 0},
    {0, 'X', 'Y', 'Z', '_', 0},
    {0, ':', ',', '?', ' ', 0},
};

char ui_key_letter(int place) {
    int r = PX_KEY_ROW(place), c = PX_KEY_COL(place);
    return r < 7 && c < 6 ? letters[r][c] : 0;
}

int ui_key_digit(int place) {
    static const int digits[7][6] = {
        {-1, -1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1, -1},
        {-1, 7, 8, 9, -1, -1},    {-1, 4, 5, 6, -1, -1},    {-1, 1, 2, 3, -1, -1},
        {-1, 0, -1, -1, -1, -1},
    };
    int r = PX_KEY_ROW(place), c = PX_KEY_COL(place);
    return r < 7 && c < 6 ? digits[r][c] : -1;
}

#define PLACE(k)   ((k) & 0xFF)
#define SHIFTED(k) (((k) & UI_SHIFTED) != 0)
#define KEY_MINUS  PX_KEY(5, 4)

static bool is_enter(int k) {
    return k >= 0 && PLACE(k) == PX_KEY_ENTER;
}

/* <-, or ON where it's a key (the settings menu's EXIT, an app's on_key()). */
static bool is_back(int k) {
    return k >= 0 && (PLACE(k) == PX_KEY_BACKSPACE || PLACE(k) == PX_KEY_ON);
}

/* The soft key's tab, 0-5, or -1. */
static int soft_key(int k) {
    return k >= 0 && PX_KEY_ROW(PLACE(k)) == 0 ? PX_KEY_COL(PLACE(k)) : -1;
}

static bool tab_is(const char *const *tabs, int k, const char *what) {
    int s = soft_key(k);
    return s >= 0 && tabs != NULL && tabs[s] != NULL && strcmp(tabs[s], what) == 0;
}

/* The EXIT key's tab, F6: does what the key does. BACK is the same, on
 * the settings screens the user wanted it on. */
static bool is_exit(int k, const char *const *tabs) {
    return is_back(k) || tab_is(tabs, k, "EXIT") || tab_is(tabs, k, "BACK");
}

/* The port's word for F6 (ui_port_t.leave). */
static const char *leave_word(const ui_port_t *p) {
    return p->leave != NULL ? p->leave : "EXIT";
}

static bool tab_live(const char *const *tabs, int k) {
    int s = soft_key(k);
    return s >= 0 && tabs != NULL && tabs[s] != NULL && tabs[s][0] != 0;
}

/* ▲ ▼, and 8 2 where digits aren't typed. */
static int up_down(int k, const char *const *tabs) {
    if (k < 0)
        return 0;
    int d = SHIFTED(k) ? -1 : ui_key_digit(PLACE(k));
    if (PLACE(k) == PX_KEY_ARROW_UP || d == 8 || tab_is(tabs, k, "^"))
        return -1;
    if (PLACE(k) == PX_KEY_ARROW_DN || d == 2 || tab_is(tabs, k, "\x0e"))
        return 1;
    return 0;
}

/* 4 6, and the ← → tabs. */
static int left_right(int k, const char *const *tabs) {
    if (k < 0)
        return 0;
    int d = SHIFTED(k) ? -1 : ui_key_digit(PLACE(k));
    if (d == 4 || tab_is(tabs, k, "\x10"))
        return -1;
    if (d == 6 || tab_is(tabs, k, "\x0f"))
        return 1;
    return 0;
}

/* ===================== drawing ===================== */

static void frame(const ui_port_t *p, int x, int y, int w, int h, int black) {
    p->fill(x, y, w, 1, black);
    p->fill(x, y + h - 1, w, 1, black);
    p->fill(x, y, 1, h, black);
    p->fill(x + w - 1, y, 1, h, black);
}

void ui_title(const ui_port_t *p, const char *title, const char *right) {
    p->fill(0, 0, PX_SCREEN_W, UI_TITLE_H - 1, 1);
    p->text(2, 0, title, PX_FONT_SMALL, 1);
    char clock[16] = "";
    if (right == NULL && p->clock != NULL) {
        p->clock(clock, sizeof(clock));
        right = clock;
    }
    if (right != NULL)
        p->text(PX_SCREEN_W - 2 - (int) strlen(right) * UI_CHAR_W, 0, right, PX_FONT_SMALL, 1);
    if (p->shifted != NULL && p->shifted())
        p->text(174, 0, "\r", PX_FONT_SMALL, 1); /* ↵, code 13, like the calculator's */
}

#ifdef UI_CHECK_TABS
#include <stdlib.h>

/* The settings' tab words (ui_guidelines.html): the screenshot tool and the
 * tests build with this, so a screen that strays stops them. ▲ ▼ only on F4
 * F5, or F3 F4 between ← → (the icon pages' cluster), EXIT only on F6. */
static void check_tab(int i, const char *t) {
    static const char *const words[] = {
        "SAVE",  "LOAD",  "USE",   "NEW",   "EDIT", "DEL",
        "RENAM", "IMPRT", "EXPRT", "FORMT", "START", "MOVE",  "FAV",   "UNFAV", "DONE",
        "ENTER", "REPLC", "APPLY", "BACK",  "CANCL", "PAGE1", "PAGE2", "INFO", "\x10",  "\x0f",  "\x10MON", "MON\x0f", "\x10YR", "YR\x0f"};
    bool ok = false;
    for (size_t w = 0; w < sizeof(words) / sizeof(words[0]); w++)
        ok = ok || strcmp(t, words[w]) == 0;
    if (strcmp(t, "^") == 0 || strcmp(t, "\x0e") == 0) {
        int at = i - (t[0] != '^'); /* where ▲ is, or would be */
        ok = at == 3 || at == 2;
    }
    if (strcmp(t, "EXIT") == 0)
        ok = i == 5;
    if (!ok) {
        fprintf(stderr, "ui_tabs: \"%s\" on F%d breaks the tab rules (ui_guidelines.html)\n", t, i + 1);
        exit(1);
    }
}
#endif

/* As Plus42's menu: 12 rows, 2 for the rounded corners, the capitals in the
 * other 10. */
void ui_tabs(const ui_port_t *p, const char *const *tabs) {
    if (tabs == NULL)
        return;
    for (int i = 0; i < 6; i++) {
        const char *t = tabs[i];
        if (t == NULL || t[0] == 0)
            continue;
#ifdef UI_CHECK_TABS
        check_tab(i, t);
#endif
        int x0 = (i * PX_SCREEN_W) / 6, x1 = ((i + 1) * PX_SCREEN_W) / 6 - 2;
        p->fill(x0 + 2, UI_TABS_Y, x1 - x0 - 3, 1, 1);
        p->fill(x0 + 1, UI_TABS_Y + 1, x1 - x0 - 1, 1, 1);
        p->fill(x0, UI_TABS_Y + 2, x1 - x0 + 1, PX_SCREEN_H - UI_TABS_Y - 2, 1);
        char text[8];
        int len = (int) strlen(t), max = (x1 - x0 + 1) / UI_CHAR_W;
        if (len > max)
            len = max;
        memcpy(text, t, (size_t) len);
        text[len] = 0;
        p->text(x0 + ((x1 - x0 + 1) - len * UI_CHAR_W) / 2, UI_TABS_Y, text, PX_FONT_SMALL, 1);
    }
}

/* A picture, a row a string ('#' black) up to a NULL, at x, y. */
static void picture(const ui_port_t *p, int x, int y, const char *const *rows, int black) {
    for (int r = 0; rows[r] != NULL; r++)
        for (int c = 0; rows[r][c] != 0; c++)
            if (rows[r][c] == '#')
                p->fill(x + c, y + r, 1, 1, black);
}

static const char *const radio_off[] = {"..###..", ".#...#.", "#.....#", "#.....#",
                                        "#.....#", ".#...#.", "..###..", NULL};
static const char *const radio_dot[] = {".......", ".......", "..###..", "..###..",
                                        "..###..", ".......", ".......", NULL};
/* A check box, 9 x 9 so its X can be two dots thick: a 7 x 7 one's tick
 * was too thin to see on the panel. */
static const char *const box_off[] = {"#########", "#.......#", "#.......#", "#.......#", "#.......#",
                                      "#.......#", "#.......#", "#.......#", "#########", NULL};
static const char *const box_tick[] = {".........", ".##...##.", ".###.###.", "..#####..", "...###...",
                                       "..#####..", ".###.###.", ".##...##.", ".........", NULL};

/* A list's tabs as shown: its own, ▲ ▼ on F4 F5 when there's more than one
 * row to move between (and the list left them free), the port's EXIT (or
 * BACK) on F6. */
static void list_tabs(const ui_port_t *p, const char *const *given, int count, const char *out[6]) {
    for (int i = 0; i < 6; i++)
        out[i] = given != NULL && given[i] != NULL ? given[i] : "";
    if (count > 1 && out[3][0] == 0 && out[4][0] == 0)
        out[3] = "^", out[4] = "\x0e";
    if (out[5][0] == 0)
        out[5] = leave_word(p);
}

static int list_first(const px_ui_list_t *l) {
    int first = l->selected - (UI_ROWS - 1);
    return first < 0 ? 0 : first;
}

int ui_list_row_y(const px_ui_list_t *l) {
    return UI_ROW_Y0 + (l->selected - list_first(l)) * UI_ROW_H;
}

void ui_list_draw(const ui_port_t *p, const px_ui_list_t *l) {
    p->fill(0, 0, PX_SCREEN_W, PX_SCREEN_H, 0);
    ui_title(p, l->title, NULL);
    if (l->count == 0 && l->empty != NULL)
        p->text(4, UI_ROW_Y0, l->empty, PX_FONT_SMALL, 0);
    int first = list_first(l);
    for (int row = 0; row < UI_ROWS && first + row < l->count; row++) {
        int i = first + row, y = UI_ROW_Y0 + row * UI_ROW_H;
        int on = i == l->selected;
        if (on)
            p->fill(0, y - 1, PX_SCREEN_W, 13, 1);
        int x = 4;
        if (l->radio) {
            picture(p, 4, y + 2, radio_off, !on);
            if (i == l->mark)
                picture(p, 4, y + 2, radio_dot, !on);
            x = 16;
        }
        char value[32] = "";
        if (l->value != NULL)
            l->value(i, value, sizeof(value), l->ctx);
        /* The value at the right, cut to what the name leaves. */
        int room = (PX_SCREEN_W - 8 - x) / UI_CHAR_W - (int) strlen(l->items[i]) - 1;
        if (room < 3)
            room = 3;
        if ((int) strlen(value) > room)
            strcpy(value + room - 2, "..");
        p->text(x, y, l->items[i], PX_FONT_SMALL, on);
        if (value[0])
            p->text(PX_SCREEN_W - 4 - (int) strlen(value) * UI_CHAR_W, y, value, PX_FONT_SMALL, on);
    }
    if (l->count > UI_ROWS) { /* where these rows are among all of them */
        int span = UI_TABS_Y - 1 - UI_ROW_Y0;
        int top = UI_ROW_Y0 - 1 + span * first / l->count;
        int bottom = UI_ROW_Y0 - 1 + span * (first + UI_ROWS) / l->count;
        p->fill(PX_SCREEN_W - 2, top, 2, bottom - top, 1);
    }
    const char *tabs[6];
    list_tabs(p, l->tabs, l->count, tabs);
    ui_tabs(p, tabs);
}

/* ===================== message, list ===================== */

int ui_message(const ui_port_t *p, const char *title, const char *const *lines, int count,
               const char *const *tabs) {
    while (true) {
        p->fill(0, 0, PX_SCREEN_W, PX_SCREEN_H, 0);
        ui_title(p, title, NULL);
        for (int i = 0; i < count && i < UI_ROWS; i++)
            p->text(4, UI_ROW_Y0 + i * UI_ROW_H, lines[i], PX_FONT_SMALL, 0);
        ui_tabs(p, tabs);
        p->show();
        int k = p->key();
        if (k == PX_UI_QUIT)
            return PX_UI_QUIT;
        if (is_enter(k))
            return PX_UI_ENTER;
        if (is_exit(k, tabs))
            return PX_UI_BACK;
        if (tab_live(tabs, k))
            return soft_key(k);
    }
}

int ui_list(const ui_port_t *p, px_ui_list_t *l) {
    while (true) {
        if (l->selected >= l->count)
            l->selected = l->count - 1;
        if (l->selected < 0)
            l->selected = 0;
        ui_list_draw(p, l);
        p->show();
        const char *tabs[6];
        list_tabs(p, l->tabs, l->count, tabs);
        int k = p->key();
        if (k == PX_UI_QUIT)
            return PX_UI_QUIT;
        int move = up_down(k, tabs);
        if (move != 0 && l->count > 0) {
            l->selected = (l->selected + move + l->count) % l->count;
        } else if (is_exit(k, tabs)) {
            return PX_UI_BACK;
        } else if (is_enter(k) && l->count > 0) {
            if (l->radio)
                l->mark = l->selected;
            return PX_UI_ENTER;
        } else if (tab_live(tabs, k)) {
            if (l->radio && soft_key(k) == 0 && l->count > 0) /* the first tab names what ENTER does */
                l->mark = l->selected;
            return soft_key(k);
        }
    }
}

/* ===================== text viewer ===================== */

#define VIEW_COLS 30 /* 240 px of the small font: the scroll bar keeps the rest */

/* The line that starts at s, wrapped at cols: *len its characters;
 * returns where the next starts, NULL after the last. A line breaks at its
 * last space, a word longer than a line where it must; \n ends one. */
static const char *view_line(const char *s, int *len, int cols) {
    int n = 0, space = -1;
    while (s[n] != 0 && s[n] != '\n' && n < cols) {
        if (s[n] == ' ')
            space = n;
        n++;
    }
    if (s[n] == 0 || (s[n] == '\n' && s[n + 1] == 0)) {
        *len = n;
        return NULL;
    }
    if (s[n] == '\n' || s[n] == ' ') {
        *len = n;
        return s + n + 1;
    }
    if (space > 0) {
        *len = space;
        return s + space + 1;
    }
    *len = n;
    return s + n;
}

int ui_view(const ui_port_t *p, const char *title, const char *text) {
    return ui_view_leave(p, title, text, leave_word(p));
}

int ui_view_leave(const ui_port_t *p, const char *title, const char *text, const char *leave) {
    if (text == NULL)
        text = "";
    int count = 0;
    for (const char *s = text; s != NULL; count++) {
        int len;
        s = view_line(s, &len, VIEW_COLS);
    }
    /* A page keeps the last one's bottom line, to read on from. */
    int last = count > UI_ROWS ? count - UI_ROWS : 0, top = 0, page = UI_ROWS - 1;
    const char *const tabs[6] = {"", "", "", "^", "\x0e", leave};
    const char *const tabs_one[6] = {"", "", "", "", "", leave};
    const char *const *shown = last > 0 ? tabs : tabs_one;
    while (true) {
        p->fill(0, 0, PX_SCREEN_W, PX_SCREEN_H, 0);
        /* The page, n/of: the last one may start part of a page on. */
        char where[16];
        snprintf(where, sizeof(where), "%d/%d", (top + page - 1) / page + 1, (last + page - 1) / page + 1);
        ui_title(p, title, last > 0 ? where : NULL);
        const char *s = text;
        int len = 0;
        for (int i = 0; i < top; i++)
            s = view_line(s, &len, VIEW_COLS);
        for (int row = 0; row < UI_ROWS && s != NULL; row++) {
            const char *next = view_line(s, &len, VIEW_COLS);
            char line[VIEW_COLS + 1];
            for (int c = 0; c < len; c++)
                line[c] = s[c] == '\t' || s[c] == '\r' ? ' ' : s[c];
            line[len] = 0;
            p->text(4, UI_ROW_Y0 + row * UI_ROW_H, line, PX_FONT_SMALL, 0);
            s = next;
        }
        if (last > 0) { /* where these lines are among all of them */
            int span = UI_TABS_Y - 1 - UI_ROW_Y0;
            int y0 = UI_ROW_Y0 - 1 + span * top / count, y1 = UI_ROW_Y0 - 1 + span * (top + UI_ROWS) / count;
            p->fill(PX_SCREEN_W - 2, y0, 2, y1 - y0, 1);
        }
        ui_tabs(p, shown);
        p->show();

        int k = p->key();
        if (k == PX_UI_QUIT)
            return PX_UI_QUIT;
        if (is_exit(k, shown))
            return PX_UI_BACK;
        int move = up_down(k, shown);
        int to = SHIFTED(k) && move != 0 ? (move < 0 ? 0 : last) : top + move * page;
        if (to < 0)
            to = 0;
        if (to > last)
            to = last;
        if (to != top)
            top = to;
        else if (k >= 0 && p->beep) /* nothing further that way, or a key that does nothing here */
            p->beep();
    }
}

/* ===================== dialog ===================== */

/* An alert as the classic black-and-white desktops drew one: the screen
 * under it dimmed (every other pixel cleared, a checkerboard), a box with a
 * double border (1 px, a 1 px gap, 2 px) and square corners, the icon at its
 * top left, the text beside it, the buttons at its bottom right -- the left
 * one the default, ringed; the arrows move the ring, ENTER presses it. The icons are drawn for the PX. */
static const char *const icon_stop[] = {
        "..........############..........",
        ".........##############.........",
        "........###..........###........",
        ".......###.##########.###.......",
        "......###.############.###......",
        ".....###.##############.###.....",
        "....###.################.###....",
        "...###.##################.###...",
        "..###.####################.###..",
        ".###.######################.###.",
        "###.########################.###",
        "##.##########################.##",
        "##.##########################.##",
        "##.####..................####.##",
        "##.####..................####.##",
        "##.####..................####.##",
        "##.####..................####.##",
        "##.####..................####.##",
        "##.####..................####.##",
        "##.##########################.##",
        "##.##########################.##",
        "###.########################.###",
        ".###.######################.###.",
        "..###.####################.###..",
        "...###.##################.###...",
        "....###.################.###....",
        ".....###.##############.###.....",
        "......###.############.###......",
        ".......###.##########.###.......",
        "........###..........###........",
        ".........##############.........",
        "..........############..........",
        NULL};
static const char *const icon_note[] = {
        "................................",
        "...##########################...",
        "..############################..",
        ".##..........................##.",
        "##............................##",
        "##............####............##",
        "##............####............##",
        "##............####............##",
        "##............................##",
        "##............................##",
        "##..........######............##",
        "##..........######............##",
        "##............####............##",
        "##............####............##",
        "##............####............##",
        "##............####............##",
        "##............####............##",
        "##............####............##",
        "##............####............##",
        "##..........########..........##",
        "##..........########..........##",
        "##............................##",
        ".##..........................##.",
        "..#######......###############..",
        "...######......##############...",
        ".......##......##...............",
        ".......##.....##................",
        ".......##....##.................",
        ".......##...##..................",
        ".......##..##...................",
        ".......##.##....................",
        ".......####.....................",
        NULL};
static const char *const icon_caution[] = {
        "................................",
        "...............##...............",
        "...............##...............",
        "..............####..............",
        "..............####..............",
        ".............######.............",
        ".............##..##.............",
        "............###..###............",
        "............##....##............",
        "...........###....###...........",
        "...........##......##...........",
        "..........###......###..........",
        "..........##...##...##..........",
        ".........###...##...###.........",
        "........###....##....###........",
        "........###....##....###........",
        ".......###.....##.....###.......",
        ".......###.....##.....###.......",
        "......###......##......###......",
        "......##.......##.......##......",
        ".....###.......##.......###.....",
        ".....##........##........##.....",
        "....###........##........###....",
        "....##....................##....",
        "...###....................###...",
        "...##..........##..........##...",
        "..###..........##..........###..",
        "..##........................##..",
        ".###........................###.",
        ".##############################.",
        "################################",
        "................................",
        NULL};

/* A filled rectangle with its corners cut by inset[], a row each from the
 * top and the bottom. */
static void rounded(const ui_port_t *p, int x, int y, int w, int h, const int *inset, int n, int black) {
    for (int i = 0; i < n; i++) {
        p->fill(x + inset[i], y + i, w - 2 * inset[i], 1, black);
        p->fill(x + inset[i], y + h - 1 - i, w - 2 * inset[i], 1, black);
    }
    p->fill(x, y + n, w, h - 2 * n, black);
}

#define BUTTON_H     18
#define DIALOG_W     238
#define DIALOG_PAD   12 /* border and margin, to the icon, the text and the buttons */
#define DIALOG_LINES 4

/* A button as on the Hello screen (main.c's hello_button()): its label in a
 * 1 px outline whose corners are cut by a pixel; the default one ringed
 * 2 px, 1 px out from it. */
static void dialog_button(const ui_port_t *p, int x, int y, int w, const char *label, bool ringed) {
    static const int ring[] = {2, 1}, gap[] = {1};
    if (ringed) {
        rounded(p, x - 3, y - 3, w + 6, BUTTON_H + 6, ring, 2, 1);
        rounded(p, x - 1, y - 1, w + 2, BUTTON_H + 2, gap, 1, 0);
    }
    static const int edge[] = {1};
    rounded(p, x, y, w, BUTTON_H, edge, 1, 1);
    rounded(p, x + 1, y + 1, w - 2, BUTTON_H - 2, edge, 1, 0);
    p->text(x + (w - p->text_width(label, PX_FONT_SMALL)) / 2, y + 3, label, PX_FONT_SMALL, 0);
}

int ui_dialog(const ui_port_t *p, int icon, const char *text, const char *left, const char *right) {
    const char *buttons[] = {left != NULL ? left : "ENTER", right};
    int r = ui_dialog_buttons(p, icon, text, buttons, right != NULL ? 2 : 1, 0);
    return r == 0 ? PX_UI_ENTER : r == 1 ? PX_UI_BACK : r;
}

int ui_dialog_buttons(const ui_port_t *p, int icon, const char *text, const char *const *buttons, int count,
                      int ring) {
    static const char *const *const icons[] = {NULL, icon_stop, icon_note, icon_caution};
    const char *const *picture_rows = icon > 0 && icon < (int) (sizeof(icons) / sizeof(icons[0])) ? icons[icon] : NULL;
    if (text == NULL)
        text = "";
    if (count < 1)
        count = 1;
    if (count > 3)
        count = 3;
    int x = (PX_SCREEN_W - DIALOG_W) / 2;
    int text_x = x + DIALOG_PAD + (picture_rows != NULL ? 32 + 10 : 0);
    int cols = (x + DIALOG_W - DIALOG_PAD - text_x) / UI_CHAR_W;
    int lines = 0;
    for (const char *s = text; s != NULL && lines < DIALOG_LINES; lines++) {
        int len;
        s = view_line(s, &len, cols);
    }
    int body = lines * UI_ROW_H;
    if (picture_rows != NULL && body < 32)
        body = 32;
    int h = DIALOG_PAD + body + 8 + BUTTON_H + 6 + 8;
    int y = (PX_SCREEN_H - h) / 2;

    for (int py = 0; py < PX_SCREEN_H; py++) /* the screen dimmed */
        for (int px = py & 1; px < PX_SCREEN_W; px += 2)
            p->fill(px, py, 1, 1, 0);
    p->fill(x, y, DIALOG_W, h, 1);
    p->fill(x + 1, y + 1, DIALOG_W - 2, h - 2, 0);
    p->fill(x + 2, y + 2, DIALOG_W - 4, h - 4, 1);
    p->fill(x + 4, y + 4, DIALOG_W - 8, h - 8, 0);
    if (picture_rows != NULL)
        picture(p, x + DIALOG_PAD, y + DIALOG_PAD, picture_rows, 1);
    const char *s = text;
    for (int row = 0; row < lines && s != NULL; row++) {
        int len;
        const char *next = view_line(s, &len, cols);
        char line[VIEW_COLS + 1];
        for (int c = 0; c < len; c++)
            line[c] = s[c] == '\t' || s[c] == '\r' ? ' ' : s[c];
        line[len] = 0;
        p->text(text_x, y + DIALOG_PAD + row * UI_ROW_H, line, PX_FONT_SMALL, 0);
        s = next;
    }
    /* The buttons the same width, the last at the box's right; three only
     * fit with less room around their words (PRINT SAVE CANCEL). */
    int pad = count == 3 ? 6 : 10, gap = count == 3 ? 12 : 16;
    int bw = 0;
    for (int i = 0; i < count; i++)
        if (p->text_width(buttons[i], PX_FONT_SMALL) > bw)
            bw = p->text_width(buttons[i], PX_FONT_SMALL);
    bw += 2 * pad;
    if (bw < 56)
        bw = 56;
    int rx = x + DIALOG_W - DIALOG_PAD - 3 - bw, by = y + h - 8 - 3 - BUTTON_H;
    int lx = rx - (count - 1) * (bw + gap);
    if (ring < 0 || ring >= count)
        ring = 0;
    /* The arrows move the ring, and ENTER presses the ringed button; F1
     * presses the first straight away, and F6, EXIT and <- the last (with
     * one button they answer PX_UI_BACK). */
    while (true) {
        p->fill(lx - 3, by - 3, rx + bw + 3 - (lx - 3), BUTTON_H + 6, 0);
        for (int i = 0; i < count; i++)
            dialog_button(p, lx + i * (bw + gap), by, bw, buttons[i], i == ring);
        p->show();
        int k = p->key();
        if (k == PX_UI_QUIT)
            return PX_UI_QUIT;
        if (is_enter(k))
            return ring;
        if (soft_key(k) == 0)
            return 0;
        if (is_back(k) || soft_key(k) == 5)
            return count > 1 ? count - 1 : PX_UI_BACK;
        int d = up_down(k, NULL) + left_right(k, NULL);
        if (count > 1 && d != 0)
            ring = (ring + d + count) % count;
        else if (k >= 0 && p->beep)
            p->beep();
    }
}

/* ===================== pull-down ===================== */

#define PULL_ROWS 5

int ui_pulldown(const ui_port_t *p, int y_at, int h_at, const char *const *items, int count, int *choice) {
    if (count <= 0)
        return PX_UI_BACK;
    int width = 0;
    for (int i = 0; i < count; i++)
        if ((int) strlen(items[i]) > width)
            width = (int) strlen(items[i]);
    int shown = count < PULL_ROWS ? count : PULL_ROWS;
    int w = (width + 1) * UI_CHAR_W + 4, h = shown * UI_ROW_H + 3;
    if (w > PX_SCREEN_W)
        w = PX_SCREEN_W;
    int x = PX_SCREEN_W - w;
    int y = y_at + h_at;
    if (y + h > UI_TABS_Y - 1)
        y = y_at - h;
    if (y < UI_TITLE_H)
        y = UI_TITLE_H;
    if (*choice < 0 || *choice >= count)
        *choice = 0;
    int first = 0;
    while (true) {
        if (*choice < first)
            first = *choice;
        if (*choice >= first + shown)
            first = *choice - shown + 1;
        p->fill(x, y, w, h, 0);
        frame(p, x, y, w, h, 1);
        for (int i = 0; i < shown; i++) {
            int ty = y + 2 + i * UI_ROW_H, on = first + i == *choice;
            if (on)
                p->fill(x + 1, ty - 1, w - 2, 13, 1);
            p->text(x + 2, ty, items[first + i], PX_FONT_SMALL, on);
        }
        if (first > 0)
            p->text(x + w - UI_CHAR_W - 1, y + 2, "^", PX_FONT_SMALL, first == *choice);
        if (first + shown < count)
            p->text(x + w - UI_CHAR_W - 1, y + 2 + (shown - 1) * UI_ROW_H, "\x0e", PX_FONT_SMALL,
                    first + shown - 1 == *choice);
        p->show();
        int k = p->key();
        if (k == PX_UI_QUIT)
            return PX_UI_QUIT;
        if (k == UI_REDRAW)
            continue;
        /* The screen's own tabs stay drawn under the list: its ▲ ▼, always on
         * the fourth and fifth, move here too; the other tabs close it. */
        int move = up_down(k, NULL), tab = soft_key(k);
        if (move == 0 && (tab == 3 || tab == 4))
            move = tab == 3 ? -1 : 1;
        if (move != 0)
            *choice = (*choice + move + count) % count;
        else if (is_enter(k))
            return PX_UI_ENTER;
        else if (is_back(k) || tab >= 0)
            return PX_UI_BACK;
    }
}

/* ===================== text and numbers ===================== */

int ui_text(const ui_port_t *p, const char *title, const char *prompt, char *text, int max, int chars) {
    char buf[64];
    if (max > (int) sizeof(buf) - 1)
        max = (int) sizeof(buf) - 1;
    snprintf(buf, sizeof(buf), "%s", text);
    int len = (int) strlen(buf);
    if (len > max)
        buf[len = max] = 0;
    while (true) {
        p->fill(0, 0, PX_SCREEN_W, PX_SCREEN_H, 0);
        ui_title(p, title, NULL);
        if (prompt != NULL)
            p->text(4, UI_ROW_Y0, prompt, PX_FONT_SMALL, 0);
        p->text(4, UI_ROW_Y0 + UI_ROW_H, "SHIFT then a digit key: digits", PX_FONT_SMALL, 0);
        /* The end of it, if it's longer than the line. */
        char line[72];
        snprintf(line, sizeof(line), "%s%s", buf, len < max ? "_" : "");
        int adv = p->text_width("0", PX_FONT_LARGE), fit = (PX_SCREEN_W - 8) / (adv > 0 ? adv : 11);
        const char *shown = line;
        if ((int) strlen(line) > fit)
            shown = line + strlen(line) - fit;
        p->text(4, 52, shown, PX_FONT_LARGE, 0);
        char count[16];
        snprintf(count, sizeof(count), "%d/%d", len, max);
        p->text(PX_SCREEN_W - 4 - (int) strlen(count) * UI_CHAR_W, 80, count, PX_FONT_SMALL, 0);
        /* No tabs: the top row of keys types A-F here. */
        for (int x = 0; x < PX_SCREEN_W; x += 2)
            p->fill(x, UI_TABS_Y - 2, 1, 1, 1);
        p->text(2, UI_TABS_Y, "ENTER:done \x10:delete EXIT:back", PX_FONT_SMALL, 0);
        p->show();

        int k = p->key();
        if (k == PX_UI_QUIT)
            return PX_UI_QUIT;
        if (k == UI_REDRAW)
            continue;
        if (is_back(k) && (len == 0 || PLACE(k) == PX_KEY_ON))
            return PX_UI_BACK;
        if (PLACE(k) == PX_KEY_BACKSPACE) {
            buf[--len] = 0;
            continue;
        }
        if (is_enter(k)) {
            if (len == 0 && chars == PX_UI_CHARS_NAME) {
                if (p->beep)
                    p->beep();
                continue;
            }
            strcpy(text, buf);
            return PX_UI_ENTER;
        }
        char c = 0;
        int place = PLACE(k);
        if (SHIFTED(k)) {
            if (ui_key_digit(place) >= 0)
                c = (char) ('0' + ui_key_digit(place));
            else if (place == KEY_MINUS && chars != PX_UI_CHARS_LABEL)
                c = '-';
        } else {
            c = ui_key_letter(place);
            if (chars != PX_UI_CHARS_TEXT && !(c >= 'A' && c <= 'Z'))
                c = 0;
        }
        if (c == 0 || len >= max) {
            if (p->beep)
                p->beep();
            continue;
        }
        buf[len++] = c;
        buf[len] = 0;
    }
}



int ui_number(const ui_port_t *p, const char *title, const char *prompt, int *value, int min, int max) {
    const char *const step_tabs[6] = {"ENTER", "", "", "^", "\x0e", leave_word(p)};
    int v = *value < min ? min : *value > max ? max : *value;
    bool typing = false;
    const char *error = NULL;
    char range[40];
    snprintf(range, sizeof(range), "From %d to %d", min, max);
    while (true) {
        p->fill(0, 0, PX_SCREEN_W, PX_SCREEN_H, 0);
        ui_title(p, title, NULL);
        if (prompt != NULL)
            p->text(4, UI_ROW_Y0, prompt, PX_FONT_SMALL, 0);
        p->text(4, UI_ROW_Y0 + UI_ROW_H, range, PX_FONT_SMALL, 0);
        char num[16];
        snprintf(num, sizeof(num), "%d%s", v, typing ? "_" : "");
        int w = p->text_width(num, PX_FONT_LARGE);
        p->text((PX_SCREEN_W - w) / 2, 52, num, PX_FONT_LARGE, 0);
        if (error != NULL)
            p->text(4, 80, error, PX_FONT_SMALL, 0);
        ui_tabs(p, step_tabs);
        p->show();

        int k = p->key();
        if (k == PX_UI_QUIT)
            return PX_UI_QUIT;
        if (k == UI_REDRAW)
            continue;
        error = NULL;
        int digit = SHIFTED(k) ? -1 : ui_key_digit(PLACE(k));
        int move = PLACE(k) == PX_KEY_ARROW_UP || tab_is(step_tabs, k, "^") ? 1
                   : PLACE(k) == PX_KEY_ARROW_DN || tab_is(step_tabs, k, "\x0e") ? -1 : 0;
        if (move != 0) {
            typing = false;
            v += move;
            v = v < min ? min : v > max ? max : v;
        } else if (tab_is(step_tabs, k, step_tabs[5]) || (is_back(k) && !typing)) {
            return PX_UI_BACK;
        } else if (PLACE(k) == PX_KEY_BACKSPACE) {
            v /= 10;
        } else if (digit >= 0) {
            if (!typing)
                v = 0;
            typing = true;
            if (v <= (2147483647 - digit) / 10)
                v = v * 10 + digit;
        } else if (is_enter(k) || soft_key(k) == 0) {
            if (v >= min && v <= max) {
                *value = v;
                return PX_UI_ENTER;
            }
            error = range;
            if (p->beep)
                p->beep();
        }
    }
}

/* ===================== the calendar ===================== */

static bool leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int month_days(int y, int m) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && leap(y) ? 29 : days[m - 1];
}

/* 0 = Sunday. */
static int weekday(int y, int m, int d) {
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3)
        y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

#define YEAR_MIN 1970
#define YEAR_MAX 2199

static void add_days(int *y, int *m, int *d, int n) {
    *d += n;
    while (*d < 1) {
        if (--*m < 1)
            *m = 12, --*y;
        *d += month_days(*y, *m);
    }
    while (*d > month_days(*y, *m)) {
        *d -= month_days(*y, *m);
        if (++*m > 12)
            *m = 1, ++*y;
    }
}

static void add_months(int *y, int *m, int *d, int n) {
    int months = *y * 12 + (*m - 1) + n;
    *y = months / 12;
    *m = months % 12 + 1;
    if (*d > month_days(*y, *m))
        *d = month_days(*y, *m);
}

int ui_date(const ui_port_t *p, const char *title, int *year, int *month, int *day) {
    const char *const tabs[6] = {"ENTER", "\x10MON", "MON\x0f", "\x10YR", "YR\x0f", leave_word(p)};
    static const char *const names[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                          "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    int y = *year, m = *month, d = *day;
    if (y < YEAR_MIN || y > YEAR_MAX || m < 1 || m > 12)
        y = 2000, m = 1;
    if (d < 1 || d > month_days(y, m))
        d = 1;
    while (true) {
        p->fill(0, 0, PX_SCREEN_W, PX_SCREEN_H, 0);
        /* The month in the title bar, where the clock would be: six weeks
         * of rows need the room. */
        char head[24];
        snprintf(head, sizeof(head), "%s %d", names[m - 1], y);
        ui_title(p, title, head);
        static const char *const wd[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
        for (int c = 0; c < 7; c++)
            p->text(12 + c * 34, 13, wd[c], PX_FONT_SMALL, 0);
        p->fill(4, 25, PX_SCREEN_W - 8, 1, 1);
        int first = weekday(y, m, 1);
        for (int n = 1; n <= month_days(y, m); n++) {
            int cell = first + n - 1, col = cell % 7, row = cell / 7;
            int cx = 12 + col * 34, cy = 28 + row * 13;
            char num[4];
            snprintf(num, sizeof(num), "%2d", n);
            if (n == d)
                p->fill(cx - 2, cy - 1, 20, 12, 1);
            p->text(cx, cy, num, PX_FONT_SMALL, n == d);
        }
        ui_tabs(p, tabs);
        p->show();

        int k = p->key();
        if (k == PX_UI_QUIT)
            return PX_UI_QUIT;
        if (k == UI_REDRAW)
            continue;
        int ny = y, nm = m, nd = d;
        int week = up_down(k, NULL), dir = left_right(k, NULL);
        if (week != 0)
            add_days(&ny, &nm, &nd, 7 * week);
        else if (dir != 0)
            add_days(&ny, &nm, &nd, dir);
        else if (soft_key(k) == 1 || soft_key(k) == 2)
            add_months(&ny, &nm, &nd, soft_key(k) == 1 ? -1 : 1);
        else if (soft_key(k) == 3 || soft_key(k) == 4)
            add_months(&ny, &nm, &nd, soft_key(k) == 3 ? -12 : 12);
        else if (is_exit(k, tabs))
            return PX_UI_BACK;
        else if (is_enter(k) || soft_key(k) == 0) {
            *year = y, *month = m, *day = d;
            return PX_UI_ENTER;
        }
        if (ny >= YEAR_MIN && ny <= YEAR_MAX)
            y = ny, m = nm, d = nd;
        else if (p->beep)
            p->beep();
    }
}

/* ===================== the time ===================== */

int ui_time(const ui_port_t *p, const char *title, const char *prompt, int *hours, int *minutes, int *seconds,
            int flags) {
    const char *const tabs[6] = {"ENTER", "\x10", "\x0f", "^", "\x0e", leave_word(p)};
    bool duration = flags & PX_UI_TIME_DURATION, h12 = !duration && (flags & PX_UI_TIME_12H);
    int nfields = 2 + ((flags & PX_UI_TIME_SECONDS) != 0) + h12;
    int v[4] = {*hours, *minutes, seconds != NULL ? *seconds : 0, 0};
    int hour_max = duration ? 9999 : 23;
    if (v[0] < 0 || v[0] > hour_max)
        v[0] = 0;
    if (h12) { /* the hour 1-12 and AM / PM */
        v[3] = v[0] >= 12;
        v[0] = v[0] % 12 ? v[0] % 12 : 12;
    }
    int field = 0, typed = 0; /* digits typed into the field so far */
    while (true) {
        p->fill(0, 0, PX_SCREEN_W, PX_SCREEN_H, 0);
        ui_title(p, title, NULL);
        if (prompt != NULL)
            p->text(4, UI_ROW_Y0, prompt, PX_FONT_SMALL, 0);
        /* Each field's text, then laid out centred, the picked one inverted. */
        char parts[4][8];
        int widths[4], total = 0, colon = p->text_width(":", PX_FONT_LARGE), space = p->text_width(" ", PX_FONT_LARGE);
        int ampm = h12 ? nfields - 1 : -1;
        for (int f = 0; f < nfields; f++) {
            if (f == ampm)
                snprintf(parts[f], sizeof(parts[f]), "%s", v[3] ? "PM" : "AM");
            else
                snprintf(parts[f], sizeof(parts[f]), f == 0 && duration ? "%d" : "%02d", v[f]);
            widths[f] = p->text_width(parts[f], PX_FONT_LARGE);
            total += widths[f] + (f == 0 ? 0 : f == ampm ? space : colon);
        }
        int x = (PX_SCREEN_W - total) / 2, y = 50;
        for (int f = 0; f < nfields; f++) {
            if (f > 0)
                x = f == ampm ? x + space : p->text(x, y, ":", PX_FONT_LARGE, 0);
            if (f == field)
                p->fill(x - 1, y - 1, widths[f] + 2, 18, 1);
            p->text(x, y, parts[f], PX_FONT_LARGE, f == field);
            x += widths[f];
        }
        ui_tabs(p, tabs);
        p->show();

        int k = p->key();
        if (k == PX_UI_QUIT)
            return PX_UI_QUIT;
        if (k == UI_REDRAW)
            continue;
        int digit = SHIFTED(k) ? -1 : ui_key_digit(PLACE(k));
        int step = PLACE(k) == PX_KEY_ARROW_UP || tab_is(tabs, k, "^") ? 1
                   : PLACE(k) == PX_KEY_ARROW_DN || tab_is(tabs, k, "\x0e") ? -1 : 0;
        int dir = tab_is(tabs, k, "\x10") ? -1 : tab_is(tabs, k, "\x0f") ? 1 : 0;
        int fmax = field == 0 ? (h12 ? 12 : hour_max) : 59, fmin = field == 0 && h12 ? 1 : 0;
        if (step != 0) {
            typed = 0;
            if (field == ampm)
                v[3] = !v[3];
            else if (duration && field == 0)
                v[0] = v[0] + step < 0 ? 0 : v[0] + step > hour_max ? hour_max : v[0] + step;
            else
                v[field] = v[field] + step > fmax ? fmin : v[field] + step < fmin ? fmax : v[field] + step;
        } else if (dir != 0) {
            typed = 0;
            field = (field + dir + nfields) % nfields;
        } else if (is_exit(k, tabs)) {
            return PX_UI_BACK;
        } else if (is_enter(k) || soft_key(k) == 0) {
            if (h12)
                v[0] = v[0] % 12 + (v[3] ? 12 : 0);
            *hours = v[0];
            *minutes = v[1];
            if (seconds != NULL)
                *seconds = (flags & PX_UI_TIME_SECONDS) ? v[2] : 0;
            return PX_UI_ENTER;
        } else if (digit >= 0 && field != ampm) {
            /* Typed: the first digit starts the field over; it moves on when
             * no more digits fit. */
            int width = field == 0 && duration ? 4 : 2;
            int nv = typed == 0 ? digit : v[field] * 10 + digit;
            if (nv > fmax && typed > 0)
                nv = digit, typed = 0;
            if (nv <= fmax) {
                v[field] = nv;
                typed++;
                if (typed >= width || nv * 10 > fmax) {
                    if (field == 0 && h12 && v[0] == 0)
                        v[0] = 12;
                    typed = 0;
                    if (field + 1 < nfields)
                        field++;
                }
            } else if (p->beep) {
                p->beep();
            }
        }
    }
}

/* ===================== forms ===================== */

void ui_field_value(const px_ui_field_t *f, char *buf, int size) {
    buf[0] = 0;
    if (f->shown != NULL) {
        snprintf(buf, (size_t) size, "%s", f->shown);
        return;
    }
    const int *v = f->value;
    switch (f->kind) {
        case PX_UI_CHOICE:
            if (v != NULL && *v >= 0 && *v < f->count)
                snprintf(buf, (size_t) size, "%s", f->items[*v]);
            break;
        case PX_UI_TEXT:
            if (f->text != NULL)
                snprintf(buf, (size_t) size, "%s", f->text);
            break;
        case PX_UI_DATE:
            snprintf(buf, (size_t) size, "%04d-%02d-%02d", v[0], v[1], v[2]);
            break;
        case PX_UI_TIME: {
            bool sec = f->flags & PX_UI_TIME_SECONDS;
            if (f->flags & PX_UI_TIME_DURATION)
                snprintf(buf, (size_t) size, sec ? "%d:%02d:%02d" : "%d:%02d", v[0], v[1], v[2]);
            else if ((f->flags & PX_UI_TIME_12H) && sec)
                snprintf(buf, (size_t) size, "%d:%02d:%02d %s", v[0] % 12 ? v[0] % 12 : 12, v[1], v[2],
                         v[0] >= 12 ? "PM" : "AM");
            else if (f->flags & PX_UI_TIME_12H)
                snprintf(buf, (size_t) size, "%d:%02d %s", v[0] % 12 ? v[0] % 12 : 12, v[1], v[0] >= 12 ? "PM" : "AM");
            else
                snprintf(buf, (size_t) size, sec ? "%02d:%02d:%02d" : "%02d:%02d", v[0], v[1], v[2]);
            break;
        }
        case PX_UI_NUMBER:
            snprintf(buf, (size_t) size, "%d", *v);
            break;
    }
}

#define FORM_MAX 16

typedef struct {
    px_ui_form_t *form;
    int field[FORM_MAX]; /* the form's field on each row shown */
} form_rows_t;

static void form_value(int row, char *buf, int size, void *ctx) {
    form_rows_t *r = ctx;
    ui_field_value(&r->form->fields[r->field[row]], buf, size);
}

int ui_form(const ui_port_t *p, px_ui_form_t *form) {
    form_rows_t rows;
    const char *labels[FORM_MAX];
    rows.form = form;
    while (true) {
        int count = 0, selected_row = 0;
        for (int i = 0; i < form->count && count < FORM_MAX; i++) {
            if (form->fields[i].hidden)
                continue;
            if (i <= form->selected)
                selected_row = count;
            rows.field[count] = i;
            labels[count++] = form->fields[i].label;
        }
        px_ui_list_t list = {form->title, labels, count, form_value, &rows, "", {0}, 0, -1, selected_row};
        for (int t = 0; t < 6; t++)
            list.tabs[t] = form->tabs[t];
        ui_list_draw(p, &list);
        int first = list_first(&list);
        for (int row = 0; row < UI_ROWS && first + row < count; row++) {
            const px_ui_field_t *f = &form->fields[rows.field[first + row]];
            if (f->kind != PX_UI_TOGGLE || f->shown != NULL)
                continue;
            int y = UI_ROW_Y0 + row * UI_ROW_H, on = first + row == selected_row;
            picture(p, PX_SCREEN_W - 13, y + 1, box_off, !on);
            if (*f->value)
                picture(p, PX_SCREEN_W - 13, y + 1, box_tick, !on);
        }
        p->show();

        int k = p->key();
        if (k == PX_UI_QUIT)
            return PX_UI_QUIT;
        if (k == UI_REDRAW || count == 0)
            continue;
        const char *tabs[6];
        list_tabs(p, form->tabs, count, tabs);
        if (tab_is(tabs, k, "ENTER")) /* a settings form's F1: does what the key does */
            k = PX_KEY_ENTER;
        int move = up_down(k, tabs);
        if (move != 0) {
            form->selected = rows.field[(selected_row + move + count) % count];
            continue;
        }
        if (is_exit(k, tabs))
            return PX_UI_BACK;
        if (tab_live(tabs, k))
            return soft_key(k);
        if (!is_enter(k))
            continue;

        int i = rows.field[selected_row];
        px_ui_field_t *f = &form->fields[i];
        int r = PX_UI_BACK, *v = f->value;
        switch (f->kind) {
            case PX_UI_CHOICE: {
                int choice = *v;
                r = ui_pulldown(p, ui_list_row_y(&list) - 1, 13, f->items, f->count, &choice);
                if (r == PX_UI_ENTER)
                    *v = choice;
                break;
            }
            case PX_UI_TOGGLE:
                *v = !*v;
                r = PX_UI_ENTER;
                break;
            case PX_UI_TEXT:
                r = ui_text(p, f->label, NULL, f->text, f->max, f->flags);
                break;
            case PX_UI_DATE:
                r = ui_date(p, f->label, &v[0], &v[1], &v[2]);
                break;
            case PX_UI_TIME:
                r = ui_time(p, f->label, NULL, &v[0], &v[1], &v[2], f->flags);
                break;
            case PX_UI_NUMBER:
                r = ui_number(p, f->label, NULL, v, f->min, f->max);
                break;
            case PX_UI_BUTTON:
                return PX_UI_ENTER;
        }
        if (r == PX_UI_QUIT)
            return PX_UI_QUIT;
        if (r == PX_UI_ENTER && form->changed != NULL && form->changed(form, i, form->ctx) == PX_UI_QUIT)
            return PX_UI_QUIT;
    }
}
