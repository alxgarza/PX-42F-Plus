/* The standard screens (ui.c) driven key by key through a stand-in port,
 * as a person would press them, and what each returns checked. Also that
 * the keys' letters are the calculator's ALPHA ones (keymap.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keymap.h"
#include "ui.h"

static int failures;

#define CHECK(cond, ...)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                      \
            printf(__VA_ARGS__);                                                                             \
            printf("\n");                                                                                    \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

/* ----- the port: keys from a script, drawing into nothing ----- */

static const int *script;
static int beeps;

static void fill(int x, int y, int w, int h, int black) {
    (void) x, (void) y, (void) w, (void) h, (void) black;
}
/* The text rows drawn in the last frame, by y: the viewer's lines. */
static char drawn[PX_SCREEN_H][64];
static char f6[16]; /* the last tab drawn on F6 */

static int text(int x, int y, const char *s, int font, int inverse) {
    (void) inverse;
    if (x == 4 && y >= 0 && y < PX_SCREEN_H)
        snprintf(drawn[y], sizeof(drawn[y]), "%s", s);
    if (y == UI_TABS_Y && x >= PX_SCREEN_W * 5 / 6)
        snprintf(f6, sizeof(f6), "%s", s);
    return x + (int) strlen(s) * (font == PX_FONT_LARGE ? 11 : 8);
}
static int text_width(const char *s, int font) {
    return (int) strlen(s) * (font == PX_FONT_LARGE ? 11 : 8);
}
static void show(void) {
}

static const char *row(int n) {
    return drawn[UI_ROW_Y0 + n * UI_ROW_H];
}
static int key(void) {
    return *script == 0 ? PX_UI_QUIT : *script++;
}
static void beep(void) {
    beeps++;
}

static const ui_port_t port = {fill, text, text_width, show, key, NULL, NULL, beep};

/* Keys by place; 0 ends a script (and PX_KEY(0, 0) is never needed alone:
 * SOFT(0) is it with a flag nobody reads). */
#define ENTER PX_KEY_ENTER
#define BSP   PX_KEY_BACKSPACE
#define UP    PX_KEY_ARROW_UP
#define DOWN  PX_KEY_ARROW_DN
#define SOFT(n) (PX_KEY(0, n) | 0x400)
#define D(n)    digit_key(n)
#define END   0

static int digit_key(int n) {
    for (int place = 0; place < 64; place++)
        if (ui_key_digit(place) == n)
            return place;
    return -1;
}

static int letter_key(char c) {
    for (int place = 0; place < 64; place++)
        if (ui_key_letter(place) == c)
            return place == 0 ? SOFT(0) : place;
    return -1;
}

static void run(const int *keys) {
    script = keys;
    beeps = 0;
}

/* ----- the tests ----- */

static void letters_are_alpha_mode(void) {
    for (int r = 0; r < KB_ROWS; r++)
        for (int c = 0; c < KB_COLS; c++) {
            int place = r == 7 ? PX_KEY(c, 5) : PX_KEY(r, c);
            if (r == 7 && c > 1)
                continue;
            CHECK(ui_key_letter(place) == alpha_key_matrix[r][c], "R%dC%d: %d, ALPHA mode %d", r, c,
                  ui_key_letter(place), alpha_key_matrix[r][c]);
        }
}

static void date_picker(void) {
    int y = 2026, m = 11, d = 13;
    int keys[] = {D(6), D(6), DOWN, SOFT(2), ENTER, END};
    run(keys);
    CHECK(ui_date(&port, "D", &y, &m, &d) == PX_UI_ENTER, "date: ENTER");
    CHECK(y == 2026 && m == 12 && d == 22, "date: 2 days, a week, a month: %d-%d-%d", y, m, d);

    y = 2024, m = 1, d = 31;
    int keys2[] = {SOFT(2), ENTER, END}; /* into February: the 29th, a leap year */
    run(keys2);
    ui_date(&port, "D", &y, &m, &d);
    CHECK(y == 2024 && m == 2 && d == 29, "date: Jan 31 + a month: %d-%d-%d", y, m, d);

    y = 2199, m = 12, d = 31;
    int keys3[] = {D(6), SOFT(4), ENTER, END}; /* past the clock's last year: refused */
    run(keys3);
    ui_date(&port, "D", &y, &m, &d);
    CHECK(y == 2199 && m == 12 && d == 31 && beeps == 2, "date: past 2199: %d-%d-%d, %d beeps", y, m, d, beeps);

    y = 2026, m = 3, d = 1;
    int keys4[] = {D(4), SOFT(5), END}; /* EXIT (F6) keeps the date */
    run(keys4);
    CHECK(ui_date(&port, "D", &y, &m, &d) == PX_UI_BACK && m == 3 && d == 1, "date: EXIT");
}

static void time_picker(void) {
    int h = 13, m = 5, s = 0;
    int keys[] = {D(0), D(7), D(3), D(0), UP, ENTER, END}; /* 07, 30, then PM -> AM */
    run(keys);
    CHECK(ui_time(&port, "T", NULL, &h, &m, &s, PX_UI_TIME_12H) == PX_UI_ENTER, "time: ENTER");
    CHECK(h == 7 && m == 30 && s == 0, "time 12-hour: %d:%d:%d", h, m, s);

    h = 23, m = 59, s = 58;
    int keys2[] = {UP, SOFT(2), UP, SOFT(2), UP, UP, ENTER, END}; /* each wraps */
    run(keys2);
    ui_time(&port, "T", NULL, &h, &m, &s, PX_UI_TIME_SECONDS);
    CHECK(h == 0 && m == 0 && s == 0, "time wraps: %d:%d:%d", h, m, s);

    h = 0, m = 0, s = 0;
    int keys3[] = {D(1), D(2), D(3), D(4), D(5), D(9), D(3), D(0), ENTER, END};
    run(keys3);
    ui_time(&port, "T", NULL, &h, &m, &s, PX_UI_TIME_DURATION | PX_UI_TIME_SECONDS);
    CHECK(h == 1234 && m == 59 && s == 30, "duration: %d:%d:%d", h, m, s);

    h = 10, m = 0, s = 0;
    int keys4[] = {D(1), D(2), ENTER, END}; /* 12 o'clock, 12-hour, typed, then AM kept: midnight */
    run(keys4);
    ui_time(&port, "T", NULL, &h, &m, &s, PX_UI_TIME_12H);
    CHECK(h == 0 && m == 0, "12 AM is 0: %d:%d", h, m);
}

static void text_entry(void) {
    char t[17] = "";
    int keys[] = {letter_key('A'), letter_key('B'), D(1) | UI_SHIFTED, letter_key('Z'), BSP, ENTER, END};
    run(keys);
    CHECK(ui_text(&port, "N", NULL, t, 16, PX_UI_CHARS_NAME) == PX_UI_ENTER && strcmp(t, "AB1") == 0,
          "name: \"%s\"", t);

    char e[17] = "";
    int keys2[] = {ENTER, letter_key(' '), BSP, BSP, END}; /* a name can't be empty; <- on empty backs out */
    run(keys2);
    CHECK(ui_text(&port, "N", NULL, e, 16, PX_UI_CHARS_NAME) == PX_UI_BACK && beeps == 2 && e[0] == 0,
          "empty name: %d beeps", beeps);

    char m[25] = "HI";
    int keys3[] = {letter_key(' '), letter_key('?'), ENTER, END};
    run(keys3);
    ui_text(&port, "M", NULL, m, 24, PX_UI_CHARS_TEXT);
    CHECK(strcmp(m, "HI ?") == 0, "text: \"%s\"", m);

    char l[8] = "ABCDEFG";
    int keys4[] = {letter_key('H'), PX_KEY_ON, END}; /* full: beeps; EXIT leaves it */
    run(keys4);
    CHECK(ui_text(&port, "L", NULL, l, 7, PX_UI_CHARS_LABEL) == PX_UI_BACK && beeps == 1
          && strcmp(l, "ABCDEFG") == 0, "label full");
}

static void number_entry(void) {
    int v = 5;
    int keys[] = {D(4), D(2), ENTER, END};
    run(keys);
    CHECK(ui_number(&port, "N", NULL, &v, 0, 999) == PX_UI_ENTER && v == 42, "number typed: %d", v);

    v = 5;
    int keys2[] = {D(1), D(0), D(0), D(0), ENTER, BSP, ENTER, END}; /* 1000 refused, <- makes it 100 */
    run(keys2);
    ui_number(&port, "N", NULL, &v, 0, 999);
    CHECK(v == 100 && beeps == 1, "number out of range: %d", v);

    v = 999;
    int keys3[] = {UP, UP, DOWN, ENTER, END};
    run(keys3);
    ui_number(&port, "N", NULL, &v, 0, 999);
    CHECK(v == 998, "number steps stop at max: %d", v);
}

static void lists(void) {
    const char *items[] = {"One", "Two", "Three"};
    px_ui_list_t l = {"L", items, 3, NULL, NULL, "", {"NEW", "", "", "", "", ""}, 1, 0, 0}; /* ▲ ▼ come by themselves */
    int keys[] = {DOWN, DOWN, DOWN, DOWN, SOFT(1), ENTER, END}; /* wraps; a "" tab does nothing */
    run(keys);
    CHECK(ui_list(&port, &l) == PX_UI_ENTER && l.selected == 1 && l.mark == 1, "radio: %d %d", l.selected, l.mark);

    int keys2[] = {SOFT(4), SOFT(0), END}; /* ▼ is F5 */
    run(keys2);
    CHECK(ui_list(&port, &l) == 0 && l.selected == 2 && l.mark == 2, "tab 0 moves the dot, as ENTER: %d %d", l.selected, l.mark);

    int keys5[] = {SOFT(5), END}; /* EXIT (F6), added by the list */
    run(keys5);
    CHECK(ui_list(&port, &l) == PX_UI_BACK, "list: the EXIT tab goes back");

    px_ui_list_t empty = {"L", items, 0, NULL, NULL, "None", {"NEW", "", "", "", "", ""}, 0, -1, 0};
    int keys3[] = {DOWN, ENTER, BSP, END};
    run(keys3);
    CHECK(ui_list(&port, &empty) == PX_UI_BACK, "empty list: ENTER does nothing");

    int choice = 1;
    int keys4[] = {DOWN, DOWN, ENTER, END};
    run(keys4);
    CHECK(ui_pulldown(&port, 40, 13, items, 3, &choice) == PX_UI_ENTER && choice == 0, "pulldown: %d", choice);
}

static int changes, changed_field;

static int on_change(px_ui_form_t *form, int field, void *ctx) {
    (void) ctx;
    changes++;
    changed_field = field;
    form->fields[2].hidden = *form->fields[0].value == 0; /* the third only with the second choice */
    return 0;
}

static void forms(void) {
    static const char *const kinds[] = {"Message", "Run"};
    int kind = 0, on = 0, n = 7, date[3] = {2026, 11, 13};
    px_ui_field_t fields[] = {
        {"Kind", PX_UI_CHOICE, 0, &kind, kinds, 2, NULL, 0, 0, 0, NULL},
        {"Loud", PX_UI_TOGGLE, 0, &on, NULL, 0, NULL, 0, 0, 0, NULL},
        {"Count", PX_UI_NUMBER, 1, &n, NULL, 0, NULL, 0, 99, 0, NULL},
        {"Date", PX_UI_DATE, 0, date, NULL, 0, NULL, 0, 0, 0, NULL},
    };
    px_ui_form_t f = {"F", fields, 4, {"SAVE", "", "", "", "", ""}, on_change, NULL, 0};
    int keys[] = {ENTER, DOWN, ENTER,          /* Kind: Run */
                  DOWN, ENTER,                 /* Loud: on */
                  DOWN, ENTER, D(9), ENTER,    /* Count, shown now: 9 */
                  DOWN, ENTER, D(6), ENTER,    /* Date: the 14th */
                  SOFT(0), END};
    run(keys);
    int r = ui_form(&port, &f);
    CHECK(r == 0, "form: SAVE is tab 0: %d", r);
    CHECK(kind == 1 && on == 1 && n == 9 && date[2] == 14, "form: %d %d %d %d", kind, on, n, date[2]);
    CHECK(changes == 4 && changed_field == 3, "form: changed %d times, last %d", changes, changed_field);
}

static void viewer(void) {
    /* 30 a line: wraps at the last space, a long word cut, \n a new line. */
    const char *text = "The quick brown fox jumps over the lazy dog.\n"
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGH\n\n"
                       "1\n2\n3\n4\n5\n6\n7\n8\n9\n";
    memset(drawn, 0, sizeof(drawn));
    int keys[] = {BSP, END};
    run(keys);
    CHECK(ui_view(&port, "V", text) == PX_UI_BACK, "view: <- goes back");
    CHECK(strcmp(row(0), "The quick brown fox jumps over") == 0, "view: row 0 \"%s\"", row(0));
    CHECK(strcmp(row(1), "the lazy dog.") == 0, "view: row 1 \"%s\"", row(1));
    CHECK(strcmp(row(2), "ABCDEFGHIJKLMNOPQRSTUVWXYZABCD") == 0, "view: row 2 \"%s\"", row(2));
    CHECK(strcmp(row(3), "EFGH") == 0 && row(4)[0] == 0 && strcmp(row(5), "1") == 0, "view: rows 3-5");

    /* 14 lines: a page is 6 on, the last page shows 8-14; past it beeps. */
    memset(drawn, 0, sizeof(drawn));
    int keys2[] = {DOWN, SOFT(4), DOWN, SOFT(5), END};
    run(keys2);
    CHECK(ui_view(&port, "V", text) == PX_UI_BACK, "view: EXIT tab");
    CHECK(strcmp(row(0), "3") == 0 && strcmp(row(6), "9") == 0 && beeps == 1, "view: the end \"%s\".. \"%s\", %d beeps",
          row(0), row(6), beeps);

    int keys3[] = {DOWN, UP, UP, SOFT(5), END};
    run(keys3);
    ui_view(&port, "V", text);
    CHECK(strcmp(row(0), "The quick brown fox jumps over") == 0 && beeps == 1, "view: back to the top, %d beeps", beeps);

    int keys4[] = {PX_KEY_ARROW_DN | UI_SHIFTED, SOFT(5), END}; /* SHIFT: straight to the end */
    run(keys4);
    ui_view(&port, "V", text);
    CHECK(strcmp(row(0), "3") == 0, "view: SHIFT down \"%s\"", row(0));

    memset(drawn, 0, sizeof(drawn));
    int keys5[] = {DOWN, SOFT(3), SOFT(5), END}; /* one page: no arrows, nothing to move */
    run(keys5);
    CHECK(ui_view(&port, "V", "Short.") == PX_UI_BACK && strcmp(row(0), "Short.") == 0 && beeps == 2,
          "view: short text, %d beeps", beeps);
    CHECK(strcmp(f6, "EXIT") == 0, "view: F6 \"%s\"", f6);

    /* ui_view_leave(): F6 named BACK, and it goes back as EXIT does. */
    int keys6[] = {SOFT(5), END};
    run(keys6);
    CHECK(ui_view_leave(&port, "V", text, "BACK") == PX_UI_BACK && strcmp(f6, "BACK") == 0, "view: BACK tab \"%s\"",
          f6);
}

static void dialog(void) {
    int keys[] = {D(1), ENTER, END};
    run(keys);
    CHECK(ui_dialog(&port, PX_UI_ICON_STOP, "Delete PIP.SAV?", "DELETE", "KEEP") == PX_UI_ENTER && beeps == 1, "dialog: ENTER, %d beeps", beeps);
    int keys2[] = {SOFT(5), END};
    run(keys2);
    CHECK(ui_dialog(&port, PX_UI_ICON_CAUTION, "Delete?", NULL, "CANCEL") == PX_UI_BACK, "dialog: F6 cancels");
    int keys3[] = {BSP, END};
    run(keys3);
    CHECK(ui_dialog(&port, PX_UI_ICON_CAUTION, "Delete?", NULL, "CANCEL") == PX_UI_BACK, "dialog: <- cancels");
    int keys4[] = {SOFT(0), END};
    run(keys4);
    CHECK(ui_dialog(&port, PX_UI_ICON_CAUTION, "Delete?", NULL, "CANCEL") == PX_UI_ENTER, "dialog: F1 is ENTER");
    int keys6[] = {DOWN, ENTER, END}; /* an arrow rings the right one: ENTER presses it */
    run(keys6);
    CHECK(ui_dialog(&port, PX_UI_ICON_STOP, "Delete?", "DELETE", "KEEP") == PX_UI_BACK && beeps == 0, "dialog: arrow, ENTER");
    int keys7[] = {D(6), D(4), ENTER, END}; /* there and back */
    run(keys7);
    CHECK(ui_dialog(&port, PX_UI_ICON_STOP, "Delete?", "DELETE", "KEEP") == PX_UI_ENTER, "dialog: 6 4 ENTER");
    int keys8[] = {UP, SOFT(0), END}; /* F1 is still the left one */
    run(keys8);
    CHECK(ui_dialog(&port, PX_UI_ICON_STOP, "Delete?", "DELETE", "KEEP") == PX_UI_ENTER, "dialog: arrow, F1");
    int keys5[] = {BSP, END}; /* no right button: <- still answers no */
    run(keys5);
    CHECK(ui_dialog(&port, PX_UI_ICON_NOTE, "Saved.", "OK", NULL) == PX_UI_BACK, "dialog: one button, <-");
    run((int[]){END});
    CHECK(ui_dialog(&port, PX_UI_ICON_CAUTION, "Delete?", NULL, "CANCEL") == PX_UI_QUIT, "dialog: quit");
}

int main(void) {
    letters_are_alpha_mode();
    date_picker();
    time_picker();
    text_entry();
    number_entry();
    lists();
    forms();
    viewer();
    dialog();
    if (failures) {
        printf("UI: %d failed\n", failures);
        return 1;
    }
    printf("UI: all passed\n");
    return 0;
}
