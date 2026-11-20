#include "settings_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "tusb.h"
#include "battery.h"
#include "display.h"
#include "font_10x16.h"
#include "font_8x12.h"
#include "free42_app.h"
#include "keyboard.h"
#include "rtc.h"
#include "settings.h"
#include "alarms.h"
#include "timemodule.h"
#include "speaker.h"
#include "statefs.h"
#include "flashdisk.h"
#include "usb_msc.h"
#include "debug_checkpoint.h"
#include "crash_report.h"
#include "splash.h"
#include "printer.h"
#include "cpuclock.h"
#include "idle.h"
#include "sounds.h"
#include "fonts.h"
#include "keyhold.h"
#include "app.h"
#include "icons.h"
#include "menu_icons.h"
#include "menu_places.h"
#include "ui.h"
#include "ui_screen.h"
#include "power.h"
#include "usbprint.h"

#ifndef PICO_PROGRAM_VERSION_STRING
#define PICO_PROGRAM_VERSION_STRING "?"
#endif

/* The Plus42 release src/core was taken from (plus42-upstream/HISTORY),
 * and the Intel Decimal Floating-Point Math Library it computes with
 * (third_party/IntelRDFPMathLib20U1). */
#define PLUS42_VERSION  "1.5.2"
#define INTEL_LIB_VERSION "2.0 Update 1"

/* HP-42S key codes, as free42_key_code() returns them. */
#define K_ENTER FREE42_KEY_ENTER
#define K_BSP   FREE42_KEY_BSP
#define K_UP    FREE42_KEY_UP
#define K_DOWN  FREE42_KEY_DOWN
#define K_SHIFT FREE42_KEY_SHIFT
#define K_EXIT  FREE42_KEY_EXIT

/* Pseudo key codes from wait_key(). */
#define K_NONE     0  /* timed out (only with a timeout) */
#define K_TICK     -1 /* the minute changed: redraw the clock */
#define K_SLEEP    -2 /* sleep timer ran out */
#define K_POWEROFF -3 /* SHIFT then EXIT */

#define MAX_STATES 32
#define MAX_PROGRAMS      64 /* programs listed for Export */
#define MAX_PROGRAM_FILES 64 /* .raw files listed for Import and Delete */
#define PROGRAM_FILE_MAX  28 /* characters of a .raw file's name shown; longer ones aren't listed */

static bool shift_on = false;

/* Menu Style was just changed: every screen goes back, and the menu opens
 * again in the new style (settings_menu_run()). */
static bool style_changed = false;

/* Details of the key wait_key() last returned, for screens that need more
 * than its key code -- name entry wants its letter, and whether SHIFT was
 * pressed before it. */
static uint8_t last_row, last_col;
static bool last_shifted;
static int last_key; /* the key wait_key() last returned */

/* Back out of a screen: EXIT, or the backspace arrow. (Where digits or a
 * name are typed, the arrow deletes first, and backs out once it's empty.) */
static bool is_back(int key) {
    return key == K_EXIT || key == K_BSP;
}

static int digit_of(int key) {
    switch (key) {
        case 34: return 0;
        case 29: return 1;
        case 30: return 2;
        case 31: return 3;
        case 24: return 4;
        case 25: return 5;
        case 26: return 6;
        case 19: return 7;
        case 20: return 8;
        case 21: return 9;
        default: return -1;
    }
}

/* Waits for one keypress (returned on release, so a held key never
 * repeats) and returns its key code -- or a K_* pseudo code. SHIFT is
 * handled here: it toggles shift_on, and SHIFT then EXIT is K_POWEROFF;
 * shift is dropped by any other key. timeout_ms = 0 waits indefinitely;
 * otherwise K_NONE after that long. K_SLEEP once no key has been pressed
 * for the Sleep Timer -- counted from the last keypress (idle_since), not
 * from this call: the screens come back here every minute to redraw their
 * clock (K_TICK), which used to start the count over, so the menu never
 * went to sleep. */
static absolute_time_t idle_since;

static int wait_key_timeout(uint32_t timeout_ms) {
    absolute_time_t deadline = timeout_ms ? make_timeout_time_ms(timeout_ms) : at_the_end_of_time;
    int start_hour, start_min, sec;
    rtc_get_hms(&start_hour, &start_min, &sec);

    while (true) {
        watchdog_update();

        uint8_t row, col;
        if (keyboard_scan(&row, &col)) {
            cpuclock_set_fast(true); /* work: full speed until it's idle again */
            key_hold_begin(row, col);
            sounds_key_play(free42_key_code(row, col) == FREE42_KEY_ENTER ? KEY_SOUND_ENTER : KEY_SOUND_ENTRY);
            for (int i = 0; i < KEY_STUCK_MS / 10 && keyboard_is_held(row, col); i++) {
                watchdog_update();
                /* Keys held down (keyhold.h). After a screenshot the keys
                 * are up and SHIFT wasn't toggled; K_TICK redraws. */
                key_hold_t hold = key_hold_poll();
                if (hold == KEY_HOLD_SCREENSHOT) {
                    idle_since = get_absolute_time();
                    return K_TICK;
                }
                if (hold == KEY_HOLD_OFF) {
                    shift_on = false;
                    return K_POWEROFF;
                }
                sleep_ms(10);
            }
            idle_since = get_absolute_time();

            int key = free42_key_code(row, col);
            if (key == K_SHIFT) {
                shift_on = !shift_on;
                return K_SHIFT;
            }
            last_row = row;
            last_col = col;
            last_shifted = shift_on;
            shift_on = false;
            if (last_shifted && key == K_EXIT)
                return K_POWEROFF;
            if (key != 0) {
                last_key = key;
                return key;
            }
            continue;
        }

        if (px_settings.sleep_minutes != 0 && !idle_usb_host_active() /* not on a computer's USB: see main.c */
                && absolute_time_diff_us(idle_since, get_absolute_time()) / 1000
                   >= (int64_t) px_settings.sleep_minutes * 60000)
            return K_SLEEP;

        int hour, min;
        rtc_get_hms(&hour, &min, &sec);
        if (hour != start_hour || min != start_min)
            return K_TICK;

        if (time_reached(deadline))
            return K_NONE;

        /* Wait for a key (idle.h) -- or for the deadline, the next minute
         * (for K_TICK), or the sleep timer, whichever is first. */
        rtc_datetime_t now;
        rtc_get_datetime(&now);
        absolute_time_t until = idle_earliest(deadline,
                                              make_timeout_time_ms((uint32_t) (60000 - (now.sec * 1000 + now.ms))));
        if (px_settings.sleep_minutes != 0)
            until = idle_earliest(until, delayed_by_ms(idle_since, (uint32_t) px_settings.sleep_minutes * 60000u));
        idle_wait(until);
    }
}

static int wait_key(void) {
    return wait_key_timeout(0);
}

static void error_beep(void) {
    /* Free42's own error squeak (shell_beeper(10)), not waited for: an
     * ERROR.wav of a few seconds froze the screen and keys until it ended. */
    sounds_play_tone(10, false);
}

/* ===================== the standard screens (ui.c) ===================== */

/* wait_key() as ui.c takes keys: by place, SHIFT before it folded in; the
 * minute changing, SHIFT itself and a screenshot redraw. */
static int menu_ui_key(void) {
    int key = wait_key();
    if (key == K_POWEROFF || key == K_SLEEP)
        return PX_UI_QUIT;
    if (key <= 0 || key == K_SHIFT)
        return UI_REDRAW;
    int place = last_row == 7 ? PX_KEY(last_col, 5) : PX_KEY(last_row, last_col);
    return place | (last_shifted ? UI_SHIFTED : 0);
}

static int menu_ui_shifted(void) {
    return shift_on;
}

static void menu_ui_clock(char *buf, int size) {
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    rtc_format_time(buf, (size_t) size, &now, true);
}

static void menu_ui_show(void) {
    display_flush();
}

static const ui_port_t menu_ui = {screen_fill,    screen_text,     screen_text_width, menu_ui_show,
                                  menu_ui_key,    menu_ui_shifted, menu_ui_clock,     error_beep};

/* The soft keys' tabs, left to right; those not named are none. */
#define TABS(...) ((const char *const[6]){__VA_ARGS__})
/* A question: the tab naming the answer (DEL, LOAD, FORMT...) on F1, EXIT on F6. */
#define CONFIRM(verb) TABS(verb, "", "", "", "", "EXIT")

/* A line of text on a screen of rows (a list's rows, ui.h). */
static void draw_line(int line, const char *text) {
    screen_text(4, UI_ROW_Y0 + line * UI_ROW_H, text, PX_FONT_SMALL, 0);
}

static void show_busy(const char *title, const char *text) {
    display_clear();
    ui_title(&menu_ui, title, NULL);
    draw_line(1, text);
    display_flush();
}

static void format_minutes(char *buf, size_t size, uint16_t minutes) {
    if (minutes == 0)
        snprintf(buf, size, "Off");
    else
        snprintf(buf, size, "%u min", (unsigned) minutes);
}

typedef enum { EDIT_OK, EDIT_CANCEL, EDIT_POWER_OFF } edit_result_t;

/* A ui_*() result: ENTER, or the first tab (what ENTER does, named), is OK;
 * back or any other tab (EXIT) isn't. */
static edit_result_t edit_result(int r) {
    return r == PX_UI_QUIT ? EDIT_POWER_OFF : r == PX_UI_ENTER || r == 0 ? EDIT_OK : EDIT_CANCEL;
}

/* Lines of text; the first tab names what ENTER does, a second one backs
 * out. For messages ("ENTER") both just mean "seen". */
static edit_result_t show_message(const char *title, const char *const *lines, int count, const char *const *tabs) {
    return edit_result(ui_message(&menu_ui, title, lines, count, tabs));
}

/* For a screen drawn its own way with the tabs USE (F1) and EXIT (F6):
 * ENTER or the first is EDIT_OK, back or EXIT EDIT_CANCEL; -1 for anything
 * else (draw it again). */
static int tab_key(void) {
    int k = menu_ui_key();
    if (k == PX_UI_QUIT)
        return EDIT_POWER_OFF;
    if (k < 0)
        return -1;
    int place = k & 0xFF;
    if (place == PX_KEY_ENTER || place == PX_KEY(0, 0))
        return EDIT_OK;
    if (place == PX_KEY_BACKSPACE || place == PX_KEY_ON || place == PX_KEY(0, 5))
        return EDIT_CANCEL;
    return -1;
}

/* ===================== lists ===================== */

#define LIST_BACK      -1
#define LIST_POWER_OFF -2

typedef void (*item_value_fn)(int item, char *buf, size_t size);

static item_value_fn list_value_fn;

static void list_value(int item, char *buf, int size, void *ctx) {
    (void) ctx;
    list_value_fn(item, buf, (size_t) size);
}

static int list_result(int r, int selected) {
    return r == PX_UI_QUIT ? LIST_POWER_OFF : r == PX_UI_ENTER || r == 0 ? selected : LIST_BACK;
}

/* A list: ENTER (or the tab naming it, NULL: none) returns the highlighted
 * index, back (or the EXIT tab) returns LIST_BACK. *selected is kept across calls, so a screen
 * reopened returns with the same row highlighted. value, if non-NULL, gives
 * a right-aligned value for each row. */
static int run_list(const char *title, const char *const *names, int count, item_value_fn value,
                    int *selected, const char *enter_tab) {
    list_value_fn = value;
    px_ui_list_t l = {title, names, count, value != NULL ? list_value : NULL, NULL, "",
                      {enter_tab != NULL ? enter_tab : ""}, 0, -1, *selected};
    int r = ui_list(&menu_ui, &l);
    *selected = l.selected;
    return list_result(r, l.selected);
}

/* One of several, as radio buttons, the one in use on: ENTER (or its tab)
 * chooses, EXIT keeps it. Returns the index chosen, LIST_BACK or
 * LIST_POWER_OFF. */
static int choose(const char *title, const char *const *names, int count, int current) {
    px_ui_list_t l = {title, names, count, NULL, NULL, "", {"ENTER"}, 1, current, current};
    return list_result(ui_list(&menu_ui, &l), l.selected);
}

/* ===================== pages of icons ===================== */

/* A header bar naming the picked icon, with the date and time; GRID_COLS x
 * GRID_ROWS icons of 32 x 32 under it, 7 x 35 = 245 across, 3 x 32 down,
 * the rows touching (an icon's own margin keeps them apart); and the soft
 * keys' tabs along the bottom (the top row of keys) in the small font, as
 * Plus42's menu: 12 rows, 2 for the rounded corners, the capitals in the
 * other 10. The picked icon is drawn inverted, its own 32 x 32. */
#define GRID_COLS 7
#define GRID_ROWS 3
_Static_assert(GRID_COLS == PLACES_ROW, "menu_places.c opens a row of places while carrying");
#define GRID_X0   4
#define GRID_DX   35
#define GRID_Y0   13
#define GRID_DY   32

/* The soft keys: the top row's key codes, F1 to F6. */
enum { SOFT_F1 = 1, SOFT_EXIT = 6 };

/* On the icons (the user's layout): ENTER ← ▲ ▼ → EXIT (BACK in the
 * folders); with SHIFT on, MOVE, FAV or UNFAV where one applies, and BACK,
 * which only puts the plain tabs back (any key ends SHIFT), but the plain
 * ones stay on an icon with neither; DONE ← ▲ ▼ → CANCL while
 * carrying. A soft key does what its tab says (grid_soft_key()), so a
 * word means the same in either row. What the arrows give grid_step(): */
enum { GO_ROW_UP = -20, GO_ROW_DOWN = -21, GO_MOVE = -22, GO_SECOND = -23, GO_CANCEL = -24 };

/* The header: the picked icon's name, then (right to left) the time, the
 * date, and the shift sign while SHIFT is on. A name the date leaves no
 * room for ("Connect to PC" beside "WED-OCT-21 11:42AM") goes whole, and
 * the date goes; a name too long even then is cut. */
static void draw_grid_header(const char *name) {
    display_fill_rect(0, 0, DISPLAY_WIDTH - 1, UI_TITLE_H - 2, true);
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    char date[16], time[12], right[32];
    rtc_format_date(date, sizeof(date), &now);
    rtc_format_time(time, sizeof(time), &now, true);
    snprintf(right, sizeof(right), "%s %s", date, time);
    int sign = shift_on ? UI_CHAR_W + 2 : 0;
    if ((int) strlen(name) * UI_CHAR_W + 4 > DISPLAY_WIDTH - 2 - (int) strlen(right) * UI_CHAR_W - sign)
        snprintf(right, sizeof(right), "%s", time);
    int x = DISPLAY_WIDTH - 2 - (int) strlen(right) * UI_CHAR_W;
    display_draw_string(&font_8x12, right, (uint8_t) x, 0, 0xFF);
    if (shift_on) {
        x -= UI_CHAR_W + 2;
        display_draw_char(&font_8x12, '\r', (uint8_t) x, 0, 0xFF); /* ↵, code 13, like the calculator's */
    }
    int fits = (x - 4) / UI_CHAR_W, len = (int) strlen(name);
    display_draw_chars(&font_8x12, name, len < fits ? len : fits, 2, 0, 0xFF);
}


/* An item's icon into bits; *letter, if not 0, is drawn in the middle of
 * it (an app without its own icon: MENU_ICON_FRAME and its first letter). */
typedef void (*grid_icon_fn)(int item, uint8_t bits[MENU_ICON_BYTES], char *letter);

/* The picked icon keeps its shape: only its black is dithered, every
 * other pixel of it left white, so it reads as grey. */
static void draw_icon(const uint8_t *bits, char letter, int x, int y, bool picked) {
    for (int row = 0; row < MENU_ICON_SIZE; row++)
        for (int col = 0; col < MENU_ICON_SIZE; col++)
            if (bits[row * 4 + col / 8] & (0x80 >> (col & 7)))
                display_set_pixel((uint8_t) (x + col), (uint8_t) (y + row), true);
    if (letter != 0)
        display_draw_char(&font_10x16, letter, (uint8_t) (x + 11), (uint8_t) (y + 8), 0x00);
    if (picked)
        for (int row = 0; row < MENU_ICON_SIZE; row++)
            for (int col = (row + 1) & 1; col < MENU_ICON_SIZE; col += 2)
                display_set_pixel((uint8_t) (x + col), (uint8_t) (y + row), false);
}

/* Where key takes the pick from sel: up and down a row (8 2, the ▲ ▼
 * tabs), past the bottom to the top and the other way; back and on one
 * icon (the keypad's ↑ ↓, 4 6, the ← → tabs as K_UP K_DOWN), past the end of a row to the
 * next. A short last row: down into it lands on its last icon, up past the
 * top on the row above it. sel if key isn't one of them. */
static int grid_step_once(int key, int sel, int count) {
    int rows = (count + GRID_COLS - 1) / GRID_COLS, col = sel % GRID_COLS, row = sel / GRID_COLS;
    int digit = digit_of(key);
    if (key == GO_ROW_UP)
        digit = 8;
    if (key == GO_ROW_DOWN)
        digit = 2;
    if (digit == 8) {
        int to = row > 0 ? sel - GRID_COLS : (rows - 1) * GRID_COLS + col;
        return to < count ? to : to - GRID_COLS;
    }
    if (digit == 2) {
        if (sel + GRID_COLS < count)
            return sel + GRID_COLS;
        return row < rows - 1 ? count - 1 : col;
    }
    if (key == K_UP || digit == 4)
        return (sel + count - 1) % count;
    if (key == K_DOWN || digit == 6)
        return (sel + 1) % count;
    return sel;
}

/* The same, past the blank places (names[] NULL) when skip: on in the
 * same direction to the next icon, or sel if there's none that way. */
static int grid_step(int key, int sel, int count, const char *const *names, bool skip) {
    int to = grid_step_once(key, sel, count);
    for (int n = 0; skip && names[to] == NULL && n < count; n++)
        to = grid_step_once(key, to, count);
    return skip && names[to] == NULL ? sel : to;
}

/* Moving icons: MOVE picks the icon up (it's drawn
 * with a double frame), the same keys carry it over the others, which stay
 * where they are, and DONE, ENTER or back puts it down: into an empty
 * place, leaving its own empty (the user's wish: the icons after it used
 * to close the gap, which scrambled the groups), or onto an icon, which
 * then takes the place it came from. While carrying, a row of empty places
 * is open past the last icon, up to max places. The first `pinned` can't
 * be moved, nor passed. move(from, to) swaps two places in the caller's
 * lists (names[] included, NULL for a blank); save() returns the count
 * without the trailing blanks, and keeps them if changed. */
typedef struct {
    int pinned, max;
    void (*move)(int from, int to);
    int (*save)(bool changed);
} grid_moves_t;

/* The name of the shifted second soft key (FAV, UNFAV) for the picked
 * one; NULL for none. */
typedef const char *(*grid_tab_fn)(int item);

#define GRID_SECOND -4 /* run_grid(): second()'s soft key, on *selected */

/* A soft key on the icons as the key it stands for, by its tab's word. */
static int grid_soft_key(const char *tab) {
    static const struct {
        const char *tab;
        int key;
    } keys[] = {{"ENTER", K_ENTER},     {"DONE", K_ENTER},  {"EXIT", K_EXIT},        {"BACK", K_BSP},
                {"CANCL", GO_CANCEL},   {"\x10", K_UP},     {"\x0f", K_DOWN},        {"^", GO_ROW_UP},
                {"\x0e", GO_ROW_DOWN}, {"MOVE", GO_MOVE}};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if (strcmp(tab, keys[i].tab) == 0)
            return keys[i].key;
    return GO_SECOND; /* FAV, UNFAV */
}

/* run_list()'s twin for icons: ENTER (the key or the tab) returns the
 * picked index, back (or the BACK tab, on a page with no MOVE or second
 * tab) LIST_BACK, a second soft key that second() names GRID_SECOND.
 * moves is NULL on a page whose icons stay put. names[i] NULL is a blank
 * place, never picked. The icons on screen are fetched once each time
 * the rows shown change (an app's comes from its file), and carried along
 * when one is moved. */
static int run_grid(const char *const *names, int count, grid_icon_fn icon, int *selected,
                    const grid_moves_t *moves, grid_tab_fn second, const char *leave) {
    /* The HP-42S characters: 16 ←, 15 →, 14 ▼; ^ ▲ as every list's. */
    static const char *const tabs[6] = {"ENTER", "\x10", "^", "\x0e", "\x0f", "EXIT"};
    static const char *const tabs_shifted[6] = {"MOVE", "", "", "", "", "BACK"};
    static const char *const tabs_moving[6] = {"DONE", "\x10", "^", "\x0e", "\x0f", "CANCL"};
    static uint8_t bits[GRID_COLS * GRID_ROWS][MENU_ICON_BYTES];
    static char letters[GRID_COLS * GRID_ROWS];
    static uint8_t carried[MENU_ICON_BYTES];
    char carried_letter = 0;
    int fetched = -1; /* the first row the icons in bits[] are of */
    int first = 0;
    int from = -1; /* the place of the icon carried; -1 none */
    while (true) {
        bool moving = from >= 0;
        if (*selected >= count)
            *selected = count - 1;
        if (*selected < 0)
            *selected = 0;
        while (!moving && *selected > 0 && names[*selected] == NULL)
            (*selected)--;
        while (!moving && *selected < count - 1 && names[*selected] == NULL)
            (*selected)++; /* a folder may start with blanks */
        int row = *selected / GRID_COLS;
        if (row < first)
            first = row;
        else if (row >= first + GRID_ROWS)
            first = row - GRID_ROWS + 1;
        if (fetched != first) {
            for (int i = 0; i < GRID_COLS * GRID_ROWS && first * GRID_COLS + i < count; i++) {
                letters[i] = 0;
                if (names[first * GRID_COLS + i] != NULL)
                    icon(first * GRID_COLS + i, bits[i], &letters[i]);
            }
            fetched = first;
        }

        display_clear();
        char moving_name[48];
        snprintf(moving_name, sizeof(moving_name), "Move: %s", moving ? names[from] : "");
        draw_grid_header(moving ? moving_name : names[*selected]);
        int rows = (count + GRID_COLS - 1) / GRID_COLS;
        for (int i = 0; i < GRID_COLS * GRID_ROWS && first * GRID_COLS + i < count; i++) {
            int place = first * GRID_COLS + i;
            bool picked = place == *selected;
            /* Carried: drawn where it is now, over what's there; its own
             * place empty. */
            bool here = moving && picked;
            if (!here && (names[place] == NULL || (moving && place == from)))
                continue;
            int x = GRID_X0 + i % GRID_COLS * GRID_DX, y = GRID_Y0 + i / GRID_COLS * GRID_DY;
            draw_icon(here ? carried : bits[i], here ? carried_letter : letters[i], x, y, picked);
            /* Picked: a frame on its square's edge (outside it would touch
             * the header bar and the tabs), the corners rounded: the lines
             * stop 2 short, a dot between them. Carried: a second one just
             * inside. */
            for (int k = picked ? (moving ? 2 : 1) : 0, inset = 0; k > 0; k--, inset++) {
                int l = x + inset, t = y + inset, r = x + MENU_ICON_SIZE - 1 - inset, b = y + MENU_ICON_SIZE - 1 - inset;
                display_fill_rect((uint8_t) (l + 2), (uint8_t) t, (uint8_t) (r - 2), (uint8_t) t, true);
                display_fill_rect((uint8_t) (l + 2), (uint8_t) b, (uint8_t) (r - 2), (uint8_t) b, true);
                display_fill_rect((uint8_t) l, (uint8_t) (t + 2), (uint8_t) l, (uint8_t) (b - 2), true);
                display_fill_rect((uint8_t) r, (uint8_t) (t + 2), (uint8_t) r, (uint8_t) (b - 2), true);
                display_set_pixel((uint8_t) (l + 1), (uint8_t) (t + 1), true);
                display_set_pixel((uint8_t) (r - 1), (uint8_t) (t + 1), true);
                display_set_pixel((uint8_t) (l + 1), (uint8_t) (b - 1), true);
                display_set_pixel((uint8_t) (r - 1), (uint8_t) (b - 1), true);
            }
        }
        if (rows > GRID_ROWS) { /* where these rows are among all of them */
            int span = UI_TABS_Y - 1 - GRID_Y0;
            int top = GRID_Y0 + span * first / rows;
            int bottom = GRID_Y0 + span * (first + GRID_ROWS) / rows - 1;
            display_fill_rect(DISPLAY_WIDTH - 2, (uint8_t) top, DISPLAY_WIDTH - 1, (uint8_t) bottom, true);
        }
        /* An icon that can't move nor go to (or from) the home screen
         * keeps the plain tabs with SHIFT on: there's nothing to show. */
        bool can_move = moves != NULL && *selected >= moves->pinned; /* Off stays put */
        const char *fav = second != NULL ? second(*selected) : NULL;
        bool shifted = shift_on && !moving && (can_move || fav != NULL);
        const char *const *shown = moving ? tabs_moving : shifted ? tabs_shifted : tabs;
        const char *tabs_now[6];
        for (int i = 0; i < 6; i++)
            tabs_now[i] = shown[i];
        if (shifted) {
            tabs_now[1] = fav != NULL ? fav : "";
            if (!can_move)
                tabs_now[0] = "";
        } else if (!moving) {
            tabs_now[5] = leave;
        }
        ui_tabs(&menu_ui, tabs_now);
        display_flush();

        int key = wait_key();
        if (key >= SOFT_F1 && key <= SOFT_EXIT) {
            if (tabs_now[key - 1][0] == 0 || (shifted && key == SOFT_EXIT))
                continue; /* a blank tab does nothing; the shifted BACK, back to the plain tabs */
            key = grid_soft_key(tabs_now[key - 1]);
        }
        if (digit_of(key) == 5) /* the middle of 4 6 8 2: ENTER */
            key = K_ENTER;
        /* Carrying: DONE (ENTER) puts it down, CANCL (EXIT, <-) puts it
         * back where it was; turning off puts it down. */
        bool cancel = key == GO_CANCEL || is_back(key);
        bool put_down = key == K_ENTER || key == K_POWEROFF || key == K_SLEEP;
        if (moving && (put_down || cancel)) {
            bool changed = put_down && *selected != from;
            if (changed)
                moves->move(from, *selected);
            if (cancel)
                *selected = from;
            from = -1;
            count = moves->save(changed); /* written only if it moved: the drive has no wear levelling */
            fetched = -1;
        }
        if (key == K_POWEROFF || key == K_SLEEP)
            return LIST_POWER_OFF;
        if (moving) {
            if (!put_down && !cancel) {
                int to = grid_step(key, *selected, count, names, false);
                if (to < moves->pinned) /* around Off, not onto it: from the first place back to the end */
                    to = *selected == moves->pinned ? count - 1 : moves->pinned;
                *selected = to;
            }
            continue;
        }
        if (is_back(key))
            return LIST_BACK;
        if (key == GO_SECOND)
            return GRID_SECOND;
        if (key == GO_MOVE) { /* only shown where it can */
            if (*selected >= moves->pinned) { /* Off can't be moved, nor turned off this way */
                int i = *selected - first * GRID_COLS; /* in view: it's the one picked */
                memcpy(carried, bits[i], sizeof(carried));
                carried_letter = letters[i];
                from = *selected;
                /* a row of blank places open past the last icon */
                int open = ((count + GRID_COLS - 1) / GRID_COLS + 1) * GRID_COLS;
                count = open < moves->max ? open : moves->max;
                fetched = -1;
            }
            continue;
        }
        if (key == K_ENTER)
            return *selected;
        *selected = grid_step(key, *selected, count, names, true);
    }
}

/* A page of the menu's own icons (the gear's, Plus42's): each opens its
 * screen. leaves: that screen's EDIT_OK means back to the calculator (a
 * state was loaded). */
typedef struct {
    const char *name;
    menu_icon_t icon;
    edit_result_t (*open)(void);
    bool leaves;
} page_item_t;

static const page_item_t *page_items;

static void page_icon(int item, uint8_t bits[MENU_ICON_BYTES], char *letter) {
    menu_icon_bits(page_items[item].icon, bits);
}

/* EDIT_OK when the calculator should show at once (a state was loaded).
 * In the List style the same items are a list, titled. leave names F6:
 * EXIT, or BACK (Settings, Plus42, USB Drive: the user's choice). */
static edit_result_t run_page(const char *title, const page_item_t *items, int count, int *selected,
                              const char *leave) {
    const char *names[GRID_COLS * GRID_ROWS];
    for (int i = 0; i < count; i++)
        names[i] = items[i].name;
    while (true) {
        debug_checkpoint(CKPT_MENU, 0);
        page_items = items; /* again: what an item opened may be a page too */
        int pick;
        if (px_settings.menu_style == MENU_STYLE_LIST) {
            px_ui_list_t l = {title, names, count, NULL, NULL, "", {"ENTER", "", "", "", "", leave}, 0, -1, *selected};
            int r = ui_list(&menu_ui, &l);
            *selected = l.selected;
            pick = list_result(r, l.selected);
        } else {
            pick = run_grid(names, count, page_icon, selected, NULL, NULL, leave);
        }
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;
        edit_result_t r = items[pick].open();
        if (r == EDIT_POWER_OFF || (r == EDIT_OK && items[pick].leaves))
            return r;
        if (style_changed)
            return EDIT_CANCEL;
    }
}

/* ===================== name entry ===================== */

/* A state's or a file's name: only A-Z, 0-9 and '-', characters that are
 * safe in a file name on every computer. name holds the initial text and
 * receives the result. */
static edit_result_t edit_name(const char *title, char *name) {
    return edit_result(ui_text(&menu_ui, title, "Name:", name, STATEFS_NAME_MAX, PX_UI_CHARS_NAME));
}

/* ===================== the items ===================== */

/* ===================== Date & Time ===================== */

static int time_flags(void) {
    return px_settings.clock_24h ? 0 : PX_UI_TIME_12H;
}

/* To the minute: the seconds start at 0 on ENTER. */
static edit_result_t do_set_time(void) {
    int h, m, s;
    rtc_get_hms(&h, &m, &s);
    edit_result_t r = edit_result(ui_time(&menu_ui, "Set Time", "ENTER sets it, at :00.", &h, &m, &s, time_flags()));
    if (r == EDIT_OK)
        rtc_set_hms(h, m, 0);
    return r;
}

static edit_result_t do_set_date(void) {
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    int y = now.year, m = now.month, d = now.day;
    edit_result_t r = edit_result(ui_date(&menu_ui, "Set Date", &y, &m, &d));
    if (r == EDIT_OK)
        rtc_set_date(y, m, d);
    return r;
}

/* Indexed by rtc_date_format_t. */
static const char *const date_format_names[] = {"YYYY-MM-DD", "DD.MM.YYYY", "MM/DD/YYYY", "DOW-MON-DAY"};
static const char *const time_format_names[] = {"24-Hour", "12-Hour"};

/* Both are Free42's formats too (YMD/DMY/MDY, CLK24/CLK12). */
static void set_formats(uint8_t date_format, bool clock_24h) {
    px_settings.date_format = date_format;
    px_settings.clock_24h = clock_24h;
    free42_set_formats(date_format, clock_24h);
    settings_save();
}

static edit_result_t do_date_format(void) {
    int pick = choose("Date Format", date_format_names, 4, px_settings.date_format);
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick >= 0)
        set_formats((uint8_t) pick, px_settings.clock_24h);
    return EDIT_OK;
}

static edit_result_t do_time_format(void) {
    int pick = choose("Time Format", time_format_names, 2, px_settings.clock_24h ? 0 : 1);
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick >= 0)
        set_formats(px_settings.date_format, pick == 0);
    return EDIT_OK;
}

/* Tenths of a second per day, as "+1.5 s/day". */
static void format_tenths(char *buf, size_t size, int tenths) {
    int a = tenths < 0 ? -tenths : tenths;
    snprintf(buf, size, "%c%d.%d s/day", tenths < 0 ? '-' : '+', a / 10, a % 10);
}

enum { CAL_CORRECTION, CAL_MEASURED, CAL_COUNT };
static const char *const cal_items[CAL_COUNT] = {"Correction", "Measured"};

static void cal_value(int item, char *buf, size_t size) {
    switch (item) {
        case CAL_CORRECTION:
            format_tenths(buf, size, px_settings.time_trim);
            break;
        case CAL_MEASURED:
            if (px_settings.cal_span_h == 0)
                snprintf(buf, size, "None Yet");
            else
                snprintf(buf, size, "%s %d.%d s/day", px_settings.cal_drift >= 0 ? "gains" : "loses",
                         abs(px_settings.cal_drift) / 10, abs(px_settings.cal_drift) % 10);
            break;
    }
}

static void set_trim(int tenths) {
    rtc_set_trim(tenths);
    settings_save();
}

/* By hand: whether the clock runs slow or fast, then by how much. */
static edit_result_t edit_correction(void) {
    static const char *const ways[] = {"Clock Runs Slow", "Clock Runs Fast", "No Correction"};
    int pick = choose("Correction", ways, 3, px_settings.time_trim < 0 ? 1 : px_settings.time_trim > 0 ? 0 : 2);
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick < 0)
        return EDIT_CANCEL;
    if (pick == 2) {
        set_trim(0);
        return EDIT_OK;
    }
    int tenths = abs(px_settings.time_trim);
    edit_result_t r = edit_result(ui_number(&menu_ui, "Correction",
                                            pick == 0 ? "Tenths of a second a day it loses:"
                                                      : "Tenths of a second a day it gains:",
                                            &tenths, 0, 999));
    if (r != EDIT_OK)
        return r;
    set_trim(pick == 0 ? tenths : -tenths);
    return EDIT_OK;
}

/* "It gained 3.2 s/day over 2.1 days." */
static void measured_lines(char *line1, char *line2, size_t size) {
    snprintf(line1, size, "It %s %d.%d s/day over", px_settings.cal_drift >= 0 ? "gained" : "lost",
             abs(px_settings.cal_drift) / 10, abs(px_settings.cal_drift) % 10);
    snprintf(line2, size, "%u.%u days.", px_settings.cal_span_h / 24, px_settings.cal_span_h % 24 * 10 / 24);
}

/* The measured drift becomes part of the correction. */
static edit_result_t apply_measured(void) {
    char line1[32], line2[32], line3[32];
    measured_lines(line1, line2, sizeof(line1));
    set_trim(px_settings.time_trim - px_settings.cal_drift);
    format_tenths(line3, sizeof(line3), px_settings.time_trim);
    px_settings.cal_span_h = 0; /* used */
    settings_save();
    const char *lines[] = {"Applied.", line1, line2, "", "Correction now:", line3};
    return show_message("Time Calibration", lines, 6, TABS("ENTER"));
}

/* Auto calibration: the exact time, to the second. The clock is set to it,
 * and the drift since the last exact setting measured (6 hours or more
 * apart) -- offered to apply. */
static edit_result_t enter_exact_time(void) {
    int h, m, s;
    rtc_get_hms(&h, &m, &s);
    edit_result_t r = edit_result(ui_time(&menu_ui, "Exact Time", "ENTER on the second it shows.", &h, &m, &s,
                                          time_flags() | PX_UI_TIME_SECONDS));
    if (r != EDIT_OK)
        return r;
    rtc_set_hms(h, m, s);
    if (!rtc_set_measured()) {
        const char *lines[] = {"Clock set: the reference.", "", "Enter the exact time again",
                               "after a day or more (6 hours", "at least) to measure the", "drift."};
        return show_message("Time Calibration", lines, 6, TABS("ENTER"));
    }
    char line1[32], line2[32];
    measured_lines(line1, line2, sizeof(line1));
    const char *lines[] = {"Clock set. Since the last", "exact time:", line1, line2, "",
                           "Apply it as the correction?"};
    r = show_message("Time Calibration", lines, 6, CONFIRM("APPLY"));
    return r == EDIT_OK ? apply_measured() : r;
}

/* Measured: apply what's measured, or enter the exact time (to measure
 * it; that also offers to apply). */
static edit_result_t do_measured(void) {
    if (px_settings.cal_span_h == 0)
        return enter_exact_time();
    char apply[32];
    snprintf(apply, sizeof(apply), "Apply (%s %d.%d s/day)", px_settings.cal_drift >= 0 ? "gains" : "loses",
             abs(px_settings.cal_drift) / 10, abs(px_settings.cal_drift) % 10);
    const char *choices[] = {apply, "Enter Exact Time"};
    int selected = 0;
    int pick = run_list("Measured", choices, 2, NULL, &selected, "ENTER");
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick == 0)
        return apply_measured();
    if (pick == 1)
        return enter_exact_time();
    return EDIT_CANCEL;
}

/* Correction: the clock's rate, + speeding it up -- by hand. Measured:
 * from the exact time entered twice (or set from a computer), at least 6
 * hours apart (rtc.h). */
static edit_result_t do_time_calibration(void) {
    static int selected = 0;
    while (true) {
        int pick = run_list("Time Calibration", cal_items, CAL_COUNT, cal_value, &selected,
                            "ENTER");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;
        edit_result_t r = pick == CAL_CORRECTION ? edit_correction() : do_measured();
        if (r == EDIT_POWER_OFF)
            return r;
    }
}

enum { DT_TIME, DT_TIME_FORMAT, DT_DATE, DT_DATE_FORMAT, DT_CALIBRATION, DT_COUNT };
static const char *const dt_items[DT_COUNT] = {"Set Time", "Time Format", "Set Date", "Date Format",
                                               "Time Calibration"};

static void dt_value(int item, char *buf, size_t size) {
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    switch (item) {
        case DT_TIME:
            rtc_format_time(buf, size, &now, false);
            break;
        case DT_DATE:
            rtc_format_date(buf, size, &now);
            break;
        case DT_DATE_FORMAT:
            snprintf(buf, size, "%s", date_format_names[px_settings.date_format]);
            break;
        case DT_TIME_FORMAT:
            snprintf(buf, size, "%s", time_format_names[px_settings.clock_24h ? 0 : 1]);
            break;
        case DT_CALIBRATION:
            format_tenths(buf, size, px_settings.time_trim);
            break;
    }
}

static edit_result_t do_date_time(void) {
    static int selected = 0;
    while (true) {
        int pick = run_list("Date & Time", dt_items, DT_COUNT, dt_value, &selected, "ENTER");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;
        edit_result_t r = EDIT_OK;
        switch (pick) {
            case DT_TIME:
                r = do_set_time();
                break;
            case DT_DATE:
                r = do_set_date();
                break;
            case DT_DATE_FORMAT:
                r = do_date_format();
                break;
            case DT_TIME_FORMAT:
                r = do_time_format();
                break;
            case DT_CALIBRATION:
                r = do_time_calibration();
                break;
        }
        if (r == EDIT_POWER_OFF)
            return r;
    }
}

/* Minutes, 0-999, 0 = off. Saved to flash on OK. */
static edit_result_t do_minutes(const char *title, const char *what, uint16_t *value) {
    char note[40];
    snprintf(note, sizeof(note), "%s, minutes (0 = Off):", what);
    int v = *value;
    edit_result_t r = edit_result(ui_number(&menu_ui, title, note, &v, 0, 999));
    if (r != EDIT_OK)
        return r;
    *value = (uint16_t) v;
    settings_save();
    return EDIT_OK;
}

static edit_result_t do_info(void) {
    char lines[7][40];
    snprintf(lines[0], sizeof(lines[0]), "Plus42    %s, Thomas Okken", PLUS42_VERSION);
    snprintf(lines[1], sizeof(lines[1]), "Port      Alex Garza");
    snprintf(lines[2], sizeof(lines[2]), "Decimal   Intel %s", INTEL_LIB_VERSION);
    snprintf(lines[3], sizeof(lines[3]), "Firmware  %s, %s", PICO_PROGRAM_VERSION_STRING, __DATE__);
    int mv = battery_read_mv();
    snprintf(lines[4], sizeof(lines[4]), "Battery   %d.%02d V", mv / 1000, mv % 1000 / 10);
    snprintf(lines[5], sizeof(lines[5]), "Stack     %lu of %u bytes", (unsigned long) crash_core1_stack_peak(),
             32u * 1024u);
    /* TEMPORARY: largest block now, of the heap's size; at the last app's start. */
    snprintf(lines[6], sizeof(lines[6]), "Heap  %luK of %luK, app %luK", (unsigned long) (app_heap_largest() / 1024),
             (unsigned long) (app_heap_total() / 1024), (unsigned long) (app_heap_at_launch() / 1024));
    /* The viewer: room for more lines than a screen holds. */
    char text[7 * 41];
    text[0] = 0;
    for (int i = 0; i < 7; i++)
        snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s\n", lines[i]);
    return ui_view_leave(&menu_ui, "Info", text, "BACK") == PX_UI_QUIT ? EDIT_POWER_OFF : EDIT_OK;
}

/* ===================== Splash Image ===================== */

#define MAX_SPLASHES 32

/* The image full screen as it will look while off, with a hint bar over
 * the bottom; ENTER = use it. */
static edit_result_t preview_splash(void) {
    while (true) {
        splash_preview_draw();
        ui_tabs(&menu_ui, CONFIRM("USE"));
        display_flush();
        int r = tab_key();
        if (r >= 0)
            return (edit_result_t) r;
    }
}

/* ===================== Printer ===================== */

/* Indexed by screen_print_t. */
static const char *const screen_print_names[] = {
        "Portrait",
        "Landscape",
};

/* Indexed by printer_target_t. */
static const char *const printer_names[] = {
        "Off",
        "USB Serial",
        "HP 82240B",
        "USB Thermal",
};
#define PRINTER_CHOICES ((int) (sizeof(printer_names) / sizeof(printer_names[0])))

enum { PRINTER_FIELD, SCREEN_PRINT_FIELD, PRINTER_FIELDS };

static int printer_pick, screen_print_pick;

/* A pick counts at once (a settings page: no SAVE). A printer chosen says
 * where the printing goes; Screen Print (PRLCD) is only the HP 82240B's. */
static int printer_changed(px_ui_form_t *form, int field, void *ctx) {
    (void) ctx;
    if (field == SCREEN_PRINT_FIELD) {
        px_settings.screen_print = (uint8_t) screen_print_pick;
        return 0;
    }
    px_settings.printer = (uint8_t) printer_pick;
    /* Choosing a printer also turns Free42's printer on (PRON, flags 21, 55),
     * Off turns it off -- so the print commands just work. */
    free42_set_printer(printer_pick != PRINTER_OFF);
    form->fields[SCREEN_PRINT_FIELD].hidden = printer_pick != PRINTER_HP82240B;
    edit_result_t r = EDIT_OK;
    if (printer_pick == PRINTER_USB_SERIAL) {
        static const char *const lines[] = {"Printing to the USB serial", "port \"PX-42F Printer\".", "",
                                            "Open it in a terminal on the", "computer to see the output."};
        r = show_message("Printer", lines, 5, TABS("ENTER"));
    } else if (printer_pick == PRINTER_HP82240B) {
        static const char *const lines[] = {"Printing to an HP 82240B", "infrared printer.", "",
                                            "Aim the calculator at the", "printer's window. A line takes",
                                            "about 2 s to print."};
        r = show_message("Printer", lines, 6, TABS("ENTER"));
    } else if (printer_pick == PRINTER_ESCPOS) {
        static const char *const lines[] = {"A 58 mm ESC/POS thermal", "printer, by a USB-C cable.", "",
                                            "While it prints, the USB port", "is the printer's: the computer",
                                            "has it again 30 s after."};
        r = show_message("Printer", lines, 6, TABS("ENTER"));
    }
    return r == EDIT_POWER_OFF ? PX_UI_QUIT : 0;
}

/* Where printing goes, a form of pull-downs: Printer, and the HP 82240B's
 * Screen Print. */
static edit_result_t do_printer(void) {
    printer_pick = px_settings.printer < PRINTER_CHOICES ? px_settings.printer : 0;
    screen_print_pick = px_settings.screen_print < SCREEN_PRINTS ? px_settings.screen_print : 0;
    px_ui_field_t fields[PRINTER_FIELDS] = {
            [PRINTER_FIELD] = {"Printer", PX_UI_CHOICE, 0, &printer_pick, printer_names, PRINTER_CHOICES, NULL, 0, 0, 0, NULL},
            [SCREEN_PRINT_FIELD] = {"Screen Print", PX_UI_CHOICE, printer_pick != PRINTER_HP82240B, &screen_print_pick,
                                    screen_print_names, SCREEN_PRINTS, NULL, 0, 0, 0, NULL},
    };
    px_ui_form_t form = {"Printer", fields, PRINTER_FIELDS, {"ENTER"}, printer_changed, NULL, 0};
    int r = ui_form(&menu_ui, &form);
    settings_save(); /* once, on the way out */
    return r == PX_UI_QUIT ? EDIT_POWER_OFF : EDIT_CANCEL;
}

/* ===================== Key Sound ===================== */

/* "Off", then the sounds (sounds_key_list()): built-ins and /SOUNDS/KEY. */
#define KEY_SOUND_CHOICES 40
static char key_sound_names[KEY_SOUND_CHOICES][SOUND_NAME_MAX + 1];
static const char *key_sound_rows[KEY_SOUND_CHOICES];

static const char *key_sound_shown(void) {
    return px_settings.key_sound[0] ? px_settings.key_sound : "Off";
}

/* Fills key_sound_names[]: "Off", then every sound; the count, and which
 * one is in use in *current. */
static int key_sound_choices(int *current) {
    strcpy(key_sound_names[0], "Off");
    int count = 1 + sounds_key_list(&key_sound_names[1], KEY_SOUND_CHOICES - 1);
    *current = 0;
    for (int i = 0; i < count; i++) {
        key_sound_rows[i] = key_sound_names[i];
        if (strcasecmp(key_sound_names[i], key_sound_shown()) == 0)
            *current = i;
    }
    return count;
}

/* Takes key sound `pick` and plays its set (typing, an operation, ENTER),
 * or says why it can't be used and keeps the one in use: false then. */
static bool key_sound_try(int pick, bool *power_off) {
    const char *name = pick == 0 ? "" : key_sound_names[pick];
    if (strcasecmp(name, px_settings.key_sound) == 0)
        return true;
    char error[40];
    if (!sounds_key_select(name, error, sizeof(error))) {
        const char *lines[] = {key_sound_names[pick], "can't be used:", error, "",
                               "Key sounds: WAV files in", "/SOUNDS/KEY on the drive."};
        sounds_key_select(px_settings.key_sound, error, sizeof(error)); /* back to the current one */
        *power_off = show_message("Key Sound", lines, 6, TABS("ENTER")) == EDIT_POWER_OFF;
        return false;
    }
    snprintf(px_settings.key_sound, sizeof(px_settings.key_sound), "%s", name);
    for (int kind = 0; kind < KEY_SOUND_KINDS; kind++) {
        if (kind > 0)
            for (int i = 0; i < 15; i++) { /* 150ms apart */
                sleep_ms(10);
                watchdog_update();
            }
        sounds_key_play((key_sound_t) kind);
        sounds_wait(); /* key sounds don't wait by themselves */
    }
    return true;
}

/* ===================== Sounds ===================== */

enum {
    SOUND_KEY,
    SOUND_ON,
    SOUND_OFF,
    SOUND_ERROR,
    SOUND_QUIET,
    SOUND_COUNT
};

static const char *const sound_items[SOUND_COUNT] = {
        "Key Sound",
        "On Sound",
        "Off Sound",
        "Calc Error Sound",
        "Silence Plus42",
};

/* The fields' values, px_settings' as ints for the form. */
static int sound_key, sound_on, sound_off, sound_error, sound_quiet;

/* A box ticked: its setting at once, and the sound played. */
static int sound_changed(px_ui_form_t *form, int field, void *ctx) {
    (void) form, (void) ctx;
    switch (field) {
        case SOUND_KEY: {
            bool power_off = false;
            if (!key_sound_try(sound_key, &power_off))
                key_sound_choices(&sound_key); /* back to the one in use */
            return power_off ? PX_UI_QUIT : 0;
        }
        case SOUND_ON:
            px_settings.on_sound = sound_on != 0;
            if (sound_on)
                sounds_play_on();
            break;
        case SOUND_OFF:
            px_settings.off_sound = sound_off != 0;
            if (sound_off)
                sounds_play_off();
            break;
        case SOUND_ERROR:
            px_settings.error_sound = sound_error != 0;
            if (sound_error)
                error_beep();
            break;
        case SOUND_QUIET:
            free42_set_quiet(sound_quiet != 0);
            break;
    }
    return 0;
}

/* All the sound settings, a form: Key Sound a pull-down (the one picked
 * plays), the others check boxes that ENTER ticks, playing the sound when
 * turned on; each change counts at once (a settings page, not a record: no
 * SAVE). The files are in /SOUNDS on the drive. Silence Plus42 is Plus42's
 * own flag 26, not saved with these. The boot sound has no setting: it
 * always plays (its file can be replaced). */
static edit_result_t do_sounds(void) {
    int count = key_sound_choices(&sound_key);
    sound_on = px_settings.on_sound, sound_off = px_settings.off_sound;
    sound_error = px_settings.error_sound, sound_quiet = free42_quiet();
    px_ui_field_t fields[SOUND_COUNT] = {
            [SOUND_KEY] = {sound_items[SOUND_KEY], PX_UI_CHOICE, 0, &sound_key, key_sound_rows, count, NULL, 0, 0, 0, NULL},
            [SOUND_ON] = {sound_items[SOUND_ON], PX_UI_TOGGLE, 0, &sound_on, NULL, 0, NULL, 0, 0, 0, NULL},
            [SOUND_OFF] = {sound_items[SOUND_OFF], PX_UI_TOGGLE, 0, &sound_off, NULL, 0, NULL, 0, 0, 0, NULL},
            [SOUND_ERROR] = {sound_items[SOUND_ERROR], PX_UI_TOGGLE, 0, &sound_error, NULL, 0, NULL, 0, 0, 0, NULL},
            [SOUND_QUIET] = {sound_items[SOUND_QUIET], PX_UI_TOGGLE, 0, &sound_quiet, NULL, 0, NULL, 0, 0, 0, NULL},
    };
    static int selected = 0;
    px_ui_form_t form = {"Sounds", fields, SOUND_COUNT, {"ENTER"}, sound_changed, NULL, selected};
    int r = ui_form(&menu_ui, &form);
    selected = form.selected;
    settings_save(); /* once, on the way out: no flash write per try */
    return r == PX_UI_QUIT ? EDIT_POWER_OFF : EDIT_CANCEL;
}

/* ===================== States, USB Drive ===================== */

static edit_result_t no_disk(void) {
    static const char *const lines[] = {"The drive is not available."};
    edit_result_t r = show_message("Drive", lines, 1, TABS("ENTER"));
    return r == EDIT_POWER_OFF ? r : EDIT_CANCEL;
}

static edit_result_t do_save_state(void) {
    if (!statefs_ready())
        return no_disk();

    /* Default name: the current date and time, YYYYMMDD-HHMMSS -- unique,
     * and sorts oldest-first in the lists. BSP to type another. */
    char name[STATEFS_NAME_MAX + 1];
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    rtc_format_stamp(name, sizeof(name), &now);
    edit_result_t r = edit_name("Save State", name);
    if (r != EDIT_OK)
        return r;

    if (statefs_state_exists(name)) {
        char line[40];
        snprintf(line, sizeof(line), "%s already exists.", name);
        const char *const lines[] = {line, "Replace it?"};
        r = show_message("Save State", lines, 2, CONFIRM("REPLC"));
        if (r != EDIT_OK)
            return r;
    }

    show_busy("Save State", "Saving...");
    debug_checkpoint(CKPT_STATE_SAVE, 0);
    uint32_t errors_before = flashdisk_error_count();
    char path[48];
    statefs_state_path(name, path, sizeof(path));
    free42_save_state(path);
    statefs_sync();
    debug_checkpoint(CKPT_MENU, 1);

    char line[40];
    bool ok = statefs_state_exists(name) && flashdisk_error_count() == errors_before;
    snprintf(line, sizeof(line), ok ? "Saved %s" : "Could not save %s", name);
    if (!ok)
        error_beep();
    const char *const lines[] = {line};
    r = show_message("Save State", lines, 1, TABS("ENTER"));
    return r == EDIT_POWER_OFF ? r : EDIT_OK;
}

/* A one-line result ("Deleted X" / "Could not ..."), with the error beep
 * on failure; then back. */
static edit_result_t show_result(const char *title, bool ok, const char *line) {
    if (!ok)
        error_beep();
    const char *const lines[] = {line};
    edit_result_t r = show_message(title, lines, 1, TABS("ENTER"));
    return r == EDIT_POWER_OFF ? r : EDIT_OK;
}

/* Returns EDIT_OK only once a state has actually been loaded. */
static edit_result_t do_load_state(const char *name) {
    char line[40];
    snprintf(line, sizeof(line), "Load %s?", name);
    const char *const lines[] = {line, "", "This replaces everything in", "the calculator's memory."};
    edit_result_t r = show_message("Load State", lines, 4, CONFIRM("LOAD"));
    if (r != EDIT_OK)
        return r;

    show_busy("Load State", "Loading...");
    debug_checkpoint(CKPT_STATE_LOAD, 0);
    char path[48];
    statefs_state_path(name, path, sizeof(path));
    free42_load_state(path);
    statefs_sync();
    return EDIT_OK;
}

static edit_result_t do_rename_state(const char *old_name) {
    char new_name[STATEFS_NAME_MAX + 1];
    strcpy(new_name, old_name);
    edit_result_t r = edit_name("Rename State", new_name);
    if (r != EDIT_OK || strcmp(new_name, old_name) == 0)
        return r;

    char line[40];
    if (statefs_state_exists(new_name)) {
        snprintf(line, sizeof(line), "%s already exists", new_name);
        return show_result("Rename State", false, line);
    }
    bool ok = statefs_rename_state(old_name, new_name);
    snprintf(line, sizeof(line), ok ? "Renamed to %s" : "Could not rename %s", ok ? new_name : old_name);
    return show_result("Rename State", ok, line);
}

static edit_result_t do_delete_state(const char *name) {
    char line[40];
    snprintf(line, sizeof(line), "Delete %s?", name);
    const char *const lines[] = {line, "", "This cannot be undone."};
    edit_result_t r = show_message("Delete State", lines, 3, CONFIRM("DEL"));
    if (r != EDIT_OK)
        return r;

    bool ok = statefs_delete_state(name);
    snprintf(line, sizeof(line), ok ? "Deleted %s" : "Could not delete %s", name);
    return show_result("Delete State", ok, line);
}

/* ===================== alarms ===================== */

/* The HP-42S's characters as this font shows them: only code 0 can't go in a
 * C string. */
static void alarm_text(char *buf, size_t size, const alarm_t *a) {
    size_t n = (size_t) a->length < size - 1 ? (size_t) a->length : size - 1;
    for (size_t i = 0; i < n; i++)
        buf[i] = a->text[i] == 0 ? '?' : a->text[i];
    buf[n] = 0;
}

static void alarm_when(char *buf, size_t size, const alarm_t *a) {
    rtc_datetime_t when;
    char date[16], time[12];
    time_ms_to_datetime(a->at_ms, &when);
    rtc_format_date(date, sizeof(date), &when);
    rtc_format_time(time, sizeof(time), &when, true);
    snprintf(buf, size, "%s %s", date, time);
}

#define ALARM_SOUND_CHOICES 64
#define TEXT_CHOICES        24

/* New Alarm and Edit: XYZALM's fields as a form -- X the time, Y the date,
 * Z the repeat, ALPHA the message or (after ↑↑ or ↑) the program's label --
 * and the sound, which XYZALM leaves at Plus42's. */
enum { AF_TIME, AF_DATE, AF_REPEAT, AF_KIND, AF_TEXT, AF_SOUND, AF_COUNT };

static const char *const alarm_kind_names[] = {"Message", "Run program", "Run when off"};
static const char *const repeat_names[] = {"None", "Every hour", "Every day", "Every week", "Other..."};
static const uint64_t repeat_every[] = {0, 3600000, 86400000, 7 * 86400000ULL};
#define REPEAT_OTHER 4

static struct {
    int time[3], date[3];
    int repeat, kind, text_pick, sound; /* the pull-downs' choices */
    uint64_t repeat_ms;
    uint8_t length;                     /* the text: the message, or the label without its arrows */
    char text[ALARM_TEXT_MAX + 1];
    char date_shown[16], repeat_shown[16], text_shown[ALARM_TEXT_MAX + 1];
    /* Message / Program: "Type...", then the other alarms' messages or the
     * calculator's global labels. */
    char text_found[TEXT_CHOICES][ALARM_TEXT_MAX + 1];
    const char *text_items[TEXT_CHOICES + 1];
    int text_count;
    /* Sound: Plus42's tones, the WAVs in /SOUNDS, the songs in /MIDI. */
    char sound_files[ALARM_SOUND_CHOICES][ALARM_SOUND_NAME_MAX + 1];
    char sound_rows[ALARM_SOUND_CHOICES + 1][ALARM_SOUND_NAME_MAX + 5];
    const char *sound_items[ALARM_SOUND_CHOICES + 1];
    int wavs, songs;
    px_ui_field_t fields[AF_COUNT];
} af;

/* A label XEQ can type: letters and digits, 7 at most. */
static bool af_text_is_label(void) {
    if (af.length > 7)
        return false;
    for (int i = 0; i < af.length; i++) {
        char c = af.text[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return false;
    }
    return true;
}

static void af_text_choices(void) {
    int count = 0;
    af.text_items[count++] = "Type...";
    if (af.kind == ALARM_MESSAGE) {
        for (int i = 0; i < alarms_count() && count <= TEXT_CHOICES; i++) {
            const alarm_t *a = alarms_get(i);
            if (alarm_kind(a) != ALARM_MESSAGE || a->length == 0 || a->length > 24)
                continue;
            alarm_text(af.text_found[count - 1], sizeof(af.text_found[0]), a);
            bool again = false;
            for (int j = 1; j < count; j++)
                again = again || strcmp(af.text_items[j], af.text_found[count - 1]) == 0;
            if (!again)
                af.text_items[count] = af.text_found[count - 1], count++;
        }
    } else {
        static char programs[16][PROGRAM_LABEL_MAX + 1];
        int n = free42_list_programs(programs, 16);
        for (int p = 0; p < n && count <= TEXT_CHOICES; p++) {
            /* "ABC" "DEF": each label between quotes */
            for (const char *q = strchr(programs[p], '"'); q != NULL && count <= TEXT_CHOICES;) {
                const char *end = strchr(q + 1, '"');
                if (end == NULL)
                    break;
                int len = (int) (end - q - 1);
                bool ok = len >= 1 && len <= 7;
                for (int k = 0; ok && k < len; k++)
                    ok = q[1 + k] >= ' ' && q[1 + k] <= '~';
                if (ok) {
                    memcpy(af.text_found[count - 1], q + 1, (size_t) len);
                    af.text_found[count - 1][len] = 0;
                    af.text_items[count] = af.text_found[count - 1], count++;
                }
                q = strchr(end + 1, '"');
            }
        }
    }
    af.text_count = count;
    af.text_pick = 0;
    for (int i = 1; i < count; i++)
        if ((int) strlen(af.text_items[i]) == af.length && memcmp(af.text_items[i], af.text, af.length) == 0)
            af.text_pick = i;
    af.fields[AF_TEXT].items = af.text_items;
    af.fields[AF_TEXT].count = count;
}

static void af_sound_choices(uint8_t sound, const char *name) {
    af.wavs = statefs_list_files(SOUNDS_DIR, ".wav", (char *) af.sound_files, ALARM_SOUND_NAME_MAX + 1,
                                 ALARM_SOUND_CHOICES);
    af.songs = statefs_list_files(MIDI_DIR, ".mid", (char *) af.sound_files[af.wavs], ALARM_SOUND_NAME_MAX + 1,
                                  ALARM_SOUND_CHOICES - af.wavs);
    strcpy(af.sound_rows[0], "Plus42 tones");
    af.sound_items[0] = af.sound_rows[0];
    af.sound = 0;
    for (int i = 0; i < af.wavs + af.songs; i++) {
        alarm_sound_t kind = i < af.wavs ? ALARM_SOUND_WAV : ALARM_SOUND_MIDI;
        snprintf(af.sound_rows[i + 1], sizeof(af.sound_rows[0]), "%s%s", af.sound_files[i],
                 i < af.wavs ? ".wav" : ".mid");
        af.sound_items[i + 1] = af.sound_rows[i + 1];
        if (sound == kind && strcmp(name, af.sound_files[i]) == 0)
            af.sound = i + 1;
    }
    af.fields[AF_SOUND].items = af.sound_items;
    af.fields[AF_SOUND].count = af.wavs + af.songs + 1;
}

/* What the rows show, and which there are, from the values. */
static void af_refresh(void) {
    rtc_datetime_t dt;
    time_ms_to_datetime((uint64_t) time_days_from_civil(af.date[0], af.date[1], af.date[2]) * 86400000, &dt);
    rtc_format_date(af.date_shown, sizeof(af.date_shown), &dt);
    af.repeat = REPEAT_OTHER;
    for (int i = 0; i < REPEAT_OTHER; i++)
        if (af.repeat_ms == repeat_every[i])
            af.repeat = i;
    snprintf(af.repeat_shown, sizeof(af.repeat_shown), "%d:%02d:%02d", (int) (af.repeat_ms / 3600000),
             (int) (af.repeat_ms / 60000 % 60), (int) (af.repeat_ms / 1000 % 60));
    af.fields[AF_REPEAT].shown = af.repeat == REPEAT_OTHER ? af.repeat_shown : NULL;
    alarm_t a;
    a.length = af.length;
    memcpy(a.text, af.text, af.length);
    alarm_text(af.text_shown, sizeof(af.text_shown), &a);
    af.fields[AF_TEXT].label = af.kind == ALARM_MESSAGE ? "Message" : "Program";
    af.fields[AF_SOUND].hidden = af.kind == ALARM_CONTROL; /* it only runs its program: it never sounds */
    af.fields[AF_TIME].flags = time_flags() | PX_UI_TIME_SECONDS;
}

static int af_changed(px_ui_form_t *form, int field, void *ctx) {
    (void) form, (void) ctx;
    int r = PX_UI_ENTER;
    if (field == AF_REPEAT && af.repeat < REPEAT_OTHER) {
        af.repeat_ms = repeat_every[af.repeat];
    } else if (field == AF_REPEAT) {
        /* Z: any HHHH:MM:SS from a second to 9999 hours. */
        int h = (int) (af.repeat_ms / 3600000), m = (int) (af.repeat_ms / 60000 % 60), sc = (int) (af.repeat_ms / 1000 % 60);
        r = ui_time(&menu_ui, "Repeat", "Every hours:minutes:seconds", &h, &m, &sc,
                    PX_UI_TIME_DURATION | PX_UI_TIME_SECONDS);
        uint64_t ms = ((uint64_t) h * 3600 + (uint64_t) m * 60 + (uint64_t) sc) * 1000;
        if (r == PX_UI_ENTER && ms >= 1000)
            af.repeat_ms = ms;
        else if (r == PX_UI_ENTER)
            error_beep();
    } else if (field == AF_KIND) {
        /* What isn't a label is typed again rather than run as something else. */
        if (af.kind != ALARM_MESSAGE && !af_text_is_label())
            af.length = 0;
        af_text_choices();
    } else if (field == AF_TEXT && af.text_pick == 0) {
        char text[ALARM_TEXT_MAX + 1];
        memcpy(text, af.text, af.length);
        text[af.length] = 0;
        bool message = af.kind == ALARM_MESSAGE;
        r = ui_text(&menu_ui, message ? "Alarm Message" : "Alarm Program", message ? "Message:" : "Program label:",
                    text, message ? 24 : 7, message ? PX_UI_CHARS_TEXT : PX_UI_CHARS_LABEL);
        if (r == PX_UI_ENTER) {
            af.length = (uint8_t) strlen(text);
            memcpy(af.text, text, af.length);
        }
    } else if (field == AF_TEXT) {
        af.length = (uint8_t) strlen(af.text_items[af.text_pick]);
        memcpy(af.text, af.text_items[af.text_pick], af.length);
    } else if (field == AF_SOUND) {
        /* The one chosen plays; one that can't play isn't chosen. */
        sounds_stop();
        if (af.sound == 0) {
            sounds_play_tone(8, true);
            sounds_play_tone(9, false);
        } else {
            alarm_t candidate;
            candidate.sound = af.sound - 1 < af.wavs ? ALARM_SOUND_WAV : ALARM_SOUND_MIDI;
            snprintf(candidate.sound_name, sizeof(candidate.sound_name), "%s", af.sound_files[af.sound - 1]);
            char path[ALARM_SOUND_PATH_MAX];
            if (!alarm_sound_path(&candidate, path, sizeof(path))
                || !(candidate.sound == ALARM_SOUND_WAV ? sounds_play_file(path, false) : sounds_midi_play(path) == 0)) {
                const char *lines[] = {af.sound_items[af.sound], "can't be played."};
                af.sound = 0;
                r = ui_message(&menu_ui, "Alarm Sound", lines, 2, TABS("ENTER"));
            }
        }
    }
    af_text_choices(); /* the pick follows the text */
    af_refresh();
    return r == PX_UI_QUIT ? PX_UI_QUIT : 0;
}

/* SAVE: the alarm from the form, in place of `replacing` (-1 for a new one).
 * A time already past isn't taken. */
static edit_result_t af_save(const char *title, int replacing) {
    uint64_t now = rtc_now_ms();
    uint64_t at = (uint64_t) time_days_from_civil(af.date[0], af.date[1], af.date[2]) * 86400000
                  + (uint64_t) ((af.time[0] * 60 + af.time[1]) * 60 + af.time[2]) * 1000;
    if (at <= now) {
        const char *const lines[] = {"That time has passed:", "change the time or the date."};
        return show_message(title, lines, 2, TABS("ENTER")) == EDIT_POWER_OFF ? EDIT_POWER_OFF : EDIT_CANCEL;
    }
    char text[ALARM_TEXT_MAX + 2];
    int length = af.kind == ALARM_CONTROL ? 2 : af.kind == ALARM_CONDITIONAL ? 1 : 0;
    memset(text, ALARM_ARROW, (size_t) length); /* ↑↑LABEL, ↑LABEL */
    int n = af.length + length > ALARM_TEXT_MAX ? ALARM_TEXT_MAX - length : af.length;
    memcpy(text + length, af.text, (size_t) n);
    length += n;
    if (replacing >= 0)
        alarms_remove(replacing);
    if (!alarms_add(at, af.repeat_ms, text, length, now)) {
        const char *const lines[] = {"The alarms are full:", "delete one first."};
        return show_message(title, lines, 2, TABS("ENTER")) == EDIT_POWER_OFF ? EDIT_POWER_OFF : EDIT_CANCEL;
    }
    /* Ours is the last of any the same: alarms of a time keep the order they
     * were set in. */
    if (af.kind != ALARM_CONTROL) {
        for (int i = alarms_count() - 1; i >= 0; i--) {
            const alarm_t *a = alarms_get(i);
            if (a->at_ms == at && a->repeat_ms == af.repeat_ms && a->length == length
                && memcmp(a->text, text, (size_t) length) == 0) {
                alarm_sound_t kind = af.sound == 0 ? ALARM_SOUND_PLUS42
                                     : af.sound - 1 < af.wavs ? ALARM_SOUND_WAV : ALARM_SOUND_MIDI;
                alarms_set_sound(i, kind, af.sound == 0 ? "" : af.sound_files[af.sound - 1]);
                break;
            }
        }
    }
    return EDIT_OK;
}

/* The form, for a new alarm (editing -1) or alarm `editing`. */
static edit_result_t alarm_form(int editing) {
    const char *title = editing < 0 ? "New Alarm" : "Edit Alarm";
    memset(&af, 0, sizeof(af));
    uint64_t at;
    uint8_t sound = ALARM_SOUND_PLUS42;
    const char *sound_name = "";
    if (editing < 0) {
        at = (rtc_now_ms() / 3600000 + 1) * 3600000; /* the next whole hour, today or tomorrow */
    } else {
        const alarm_t *a = alarms_get(editing);
        at = a->at_ms;
        af.repeat_ms = a->repeat_ms;
        af.kind = alarm_kind(a);
        int length;
        const char *label = af.kind == ALARM_MESSAGE ? (length = a->length, a->text) : alarm_label(a, &length);
        af.length = (uint8_t) length;
        memcpy(af.text, label, (size_t) length);
        sound = a->sound;
        sound_name = a->sound_name;
    }
    rtc_datetime_t dt;
    time_ms_to_datetime(at, &dt);
    af.date[0] = dt.year, af.date[1] = dt.month, af.date[2] = dt.day;
    af.time[0] = dt.hour, af.time[1] = dt.min, af.time[2] = dt.sec;

    af.fields[AF_TIME] = (px_ui_field_t){"Time", PX_UI_TIME, 0, af.time, NULL, 0, NULL, 0, 0, 0, NULL};
    af.fields[AF_DATE] = (px_ui_field_t){"Date", PX_UI_DATE, 0, af.date, NULL, 0, NULL, 0, 0, 0, af.date_shown};
    af.fields[AF_REPEAT] = (px_ui_field_t){"Repeat", PX_UI_CHOICE, 0, &af.repeat, repeat_names, 5, NULL, 0, 0, 0, NULL};
    af.fields[AF_KIND] = (px_ui_field_t){"Kind", PX_UI_CHOICE, 0, &af.kind, alarm_kind_names, 3, NULL, 0, 0, 0, NULL};
    af.fields[AF_TEXT] = (px_ui_field_t){"Message", PX_UI_CHOICE, 0, &af.text_pick, NULL, 0, NULL, 0, 0, 0, af.text_shown};
    af.fields[AF_SOUND] = (px_ui_field_t){"Sound", PX_UI_CHOICE, 0, &af.sound, NULL, 0, NULL, 0, 0, 0, NULL};
    af_text_choices();
    af_sound_choices(sound, sound_name);
    af_refresh();

    px_ui_form_t form = {title, af.fields, AF_COUNT, {"SAVE"}, af_changed, NULL, 0};
    while (true) {
        int r = ui_form(&menu_ui, &form);
        sounds_stop();
        if (r == PX_UI_QUIT)
            return EDIT_POWER_OFF;
        if (r != 0) /* EXIT, back */
            return EDIT_CANCEL;
        edit_result_t saved = af_save(title, editing);
        if (saved != EDIT_CANCEL)
            return saved;
    }
}

/* Every alarm, earliest first; ! = past due. NEW and EDIT (or ENTER) open
 * the form, DEL asks first; only the keys that do something: nothing to
 * edit with no alarms. An app's alarm (px->alarm_set()) can only be deleted:
 * the app takes back its own. */
static edit_result_t do_alarms(void) {
    static char rows[ALARMS_MAX][48];
    static const char *names[ALARMS_MAX];
    px_ui_list_t l = {"Alarms", names, 0, NULL, NULL, "No alarms. NEW sets one.", {"NEW", "", "", "", "", "BACK"}, 0, -1, 0};
    while (true) {
        l.count = alarms_count();
        for (int i = 0; i < l.count; i++) {
            const alarm_t *a = alarms_get(i);
            char when[32], text[ALARM_TEXT_MAX + 1];
            alarm_when(when, sizeof(when), a);
            alarm_text(text, sizeof(text), a);
            snprintf(rows[i], sizeof(rows[i]), "%s%s %s", a->past_due ? "!" : "", when, text);
            names[i] = rows[i];
        }
        l.tabs[1] = l.count > 0 ? "EDIT" : "";
        l.tabs[2] = l.count > 0 ? "DEL" : "";
        int key = ui_list(&menu_ui, &l);
        if (key == PX_UI_QUIT)
            return EDIT_POWER_OFF;
        if (key == PX_UI_BACK)
            return EDIT_OK;
        edit_result_t r = EDIT_OK;
        if (key == 0) {
            r = alarm_form(-1);
        } else if (key == 2) {
            char line[40], text[ALARM_TEXT_MAX + 1], when[32];
            alarm_when(when, sizeof(when), alarms_get(l.selected));
            alarm_text(text, sizeof(text), alarms_get(l.selected));
            snprintf(line, sizeof(line), "%.24s", text);
            const char *const lines[] = {"Delete the alarm", when, line};
            r = show_message("Delete Alarm", lines, 3, CONFIRM("DEL"));
            if (r == EDIT_OK)
                alarms_remove(l.selected);
        } else if (alarms_get(l.selected)->owner[0] != 0) { /* EDIT or ENTER */
            char line[40];
            snprintf(line, sizeof(line), "%.28s set it.", alarms_get(l.selected)->owner);
            const char *const lines[] = {line, "It can only be deleted."};
            r = show_message("Edit Alarm", lines, 2, TABS("ENTER"));
        } else {
            r = alarm_form(l.selected);
        }
        if (r == EDIT_POWER_OFF)
            return r;
    }
}

/* Connect to PC -- like the DM42's USB disk mode: the computer gets
 * the drive, the calculator stays out of it, until the drive is ejected or
 * EXIT. */
static edit_result_t do_usb_connect(void) {
    if (!statefs_ready() && flashdisk_block_count() == 0)
        return no_disk();

    debug_checkpoint(CKPT_USB_DISK, 0);
    usbprint_release(); /* the port back from the thermal printer, if it has it */
    sounds_stop(); /* one playing from a file: closed before the computer gets the drive */
    statefs_usb_begin();
    edit_result_t result = EDIT_OK;
    bool redraw = true;
    bool was_mounted = !tud_mounted();

    while (true) {
        statefs_usb_poll();

        bool host = tud_mounted();
        if (redraw || host != was_mounted) {
            was_mounted = host;
            redraw = false;
            display_clear();
            ui_title(&menu_ui, "Connect to PC", NULL);
            draw_line(0, host ? "Connected: drive \"PX-42F\"" : "Waiting for a computer...");
            draw_line(2, "States: STATE/<name>.p42");
            draw_line(3, "Splash images: SPLASH/*.png");
            draw_line(4, "Programs: PROGRAMS/*.raw");
            draw_line(5, "Eject the drive, then EXIT.");
            ui_tabs(&menu_ui, TABS("", "", "", "", "", "EXIT"));
            display_flush();
        }

        if (usb_msc_ejected())
            break;

        int key = wait_key_timeout(10);
        if (key == K_POWEROFF) {
            result = EDIT_POWER_OFF;
            break;
        }
        if (is_back(key) || key == SOFT_EXIT) /* EXIT, <-, the EXIT tab */
            break;
        if (key == K_TICK || key == K_SHIFT)
            redraw = true;
        /* The sleep timer is paused while the computer has the drive. */
        idle_since = get_absolute_time();
    }

    show_busy("Connect to PC", "Writing to flash...");
    statefs_usb_end();
    splash_reload(); /* the chosen splash image may have been replaced */
    sounds_init(true); /* likewise the sounds -- and /SOUNDS made again if it was deleted */
    fonts_init(); /* and the fonts */
    return result;
}

/* Format Drive: an empty drive, asked twice -- also the way back from one
 * that isn't FAT any more (statefs_init() leaves those alone). Then the
 * folders, and the built-in sounds, as at the first boot. */
static edit_result_t do_format_drive(void) {
    if (flashdisk_block_count() == 0)
        return no_disk();
    static const char *const warning[] = {"Erase every file on the drive?", "", "States, programs, screenshots,",
                                          "your sounds, fonts and splash", "images. Settings stay."};
    edit_result_t r = show_message("Format Drive", warning, 5, CONFIRM("FORMT"));
    if (r != EDIT_OK)
        return r;
    static const char *const sure[] = {"Are you sure?", "", "This cannot be undone."};
    r = show_message("Format Drive", sure, 3, CONFIRM("FORMT"));
    if (r != EDIT_OK)
        return r;

    show_busy("Format Drive", "Formatting...");
    sounds_stop(); /* its file, if it's one */
    bool ok = statefs_format();
    if (ok) {
        app_folders();
        statefs_mkdir(PROGRAMS_DIR);
        sounds_init(true); /* the built-ins written again */
        fonts_init();      /* a font of the user's is gone: the built-in */
        splash_reload();   /* likewise the splash image */
        ok = statefs_sync();
    }
    return show_result("Format Drive", ok, ok ? "The drive is empty." : "Could not format the drive.");
}

/* Update Firmware: the boot ROM's update mode without the BOOT and RESET
 * buttons -- the RP2350 drive, which takes PX-42F-Plus.uf2 and then starts
 * it. The new firmware starts with Memory Clear, as after any flash (the
 * user's choice); the settings are kept. The screen keeps the instructions
 * while the processor is in the ROM. */
static edit_result_t do_update_firmware(void) {
    static const char *const how[] = {"The calculator becomes a drive", "named RP2350. Copy",
                                      "PX-42F-Plus.uf2 onto it: the", "new firmware then starts,",
                                      "with the memory cleared."};
    edit_result_t r = show_message("Update Firmware", how, 5, CONFIRM("START"));
    if (r != EDIT_OK)
        return r;
    settings_save_pending();

    display_clear();
    ui_title(&menu_ui, "Update Firmware", NULL);
    draw_line(0, "Copy PX-42F-Plus.uf2 onto");
    draw_line(1, "the RP2350 drive.");
    draw_line(3, "To cancel, press RESET.");
    display_flush();
    power_update_mode();
}

static const page_item_t usb_page[] = {
        {"Connect to PC", MENU_ICON_USB_CONNECT, do_usb_connect, false},
        {"Format Drive", MENU_ICON_USB_FORMAT, do_format_drive, false},
        {"Update Firmware", MENU_ICON_USB_UPDATE, do_update_firmware, false},
};

static edit_result_t do_usb_drive(void) {
    static int selected = 0;
    edit_result_t r = run_page("USB Drive", usb_page, sizeof(usb_page) / sizeof(usb_page[0]), &selected, "BACK");
    return r == EDIT_POWER_OFF ? r : EDIT_CANCEL;
}

/* The saved states, a list: SAVE makes one; LOAD (or ENTER), RENAM and
 * DEL act on the one picked, so with none only SAVE and EXIT show.
 * Returns EDIT_POWER_OFF to power off, EDIT_OK when a state was loaded
 * (back to the calculator, to show it), EDIT_CANCEL to go back. */
static edit_result_t do_states(void) {
    if (!statefs_ready())
        return no_disk();
    static char names[MAX_STATES][STATEFS_NAME_MAX + 1];
    static const char *items[MAX_STATES];
    static int selected = 0;
    px_ui_list_t l = {"States", items, 0, NULL, NULL, "No saved states yet.", {"SAVE", "", "", "", "", "BACK"}, 0, -1,
                      selected};
    while (true) {
        l.count = statefs_list_states(names, MAX_STATES);
        for (int i = 0; i < l.count; i++)
            items[i] = names[i];
        l.tabs[1] = l.count > 0 ? "LOAD" : "";
        l.tabs[2] = l.count > 0 ? "RENAM" : "";
        l.tabs[3] = l.count > 0 ? "DEL" : "";
        int key = ui_list(&menu_ui, &l);
        selected = l.selected;
        if (key == PX_UI_QUIT)
            return EDIT_POWER_OFF;
        if (key == PX_UI_BACK)
            return EDIT_CANCEL;
        edit_result_t r;
        if (key == 0) {
            r = do_save_state();
        } else if (key == 2) {
            r = do_rename_state(names[l.selected]);
        } else if (key == 3) {
            r = do_delete_state(names[l.selected]);
        } else { /* LOAD or ENTER */
            r = do_load_state(names[l.selected]);
            if (r == EDIT_OK)
                return EDIT_OK;
        }
        if (r == EDIT_POWER_OFF)
            return r;
    }
}

/* ===================== Programs ===================== */

/* Plus42's .raw program files, in PROGRAMS_DIR (free42_app.h): exported
 * from the calculator, imported into it -- the files the desktop Plus42
 * and Free42 read and write, and the program libraries online. */

static void program_path(const char *name, char *path, size_t size) {
    snprintf(path, size, "%s/%s.raw", PROGRAMS_DIR, name);
}

/* A file name for a program: its first label, without the quotes, in the
 * characters a file name and edit_name() take -- "PROGRAM" if none is left
 * (END, a program with no global label). */
static void name_from_label(const char *label, char *name) {
    int len = 0;
    for (const char *p = label + (label[0] == '"'); *p != 0 && *p != '"' && len < STATEFS_NAME_MAX; p++) {
        char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            name[len++] = c;
    }
    name[len] = 0;
    if (len == 0 || label[0] != '"')
        strcpy(name, "PROGRAM");
}

static edit_result_t do_export_programs(void) {
    if (!statefs_ready())
        return no_disk();

    static char labels[MAX_PROGRAMS + 1][PROGRAM_LABEL_MAX + 1];
    snprintf(labels[0], sizeof(labels[0]), "All Programs");
    int count = free42_list_programs(labels + 1, MAX_PROGRAMS);
    const char *items[MAX_PROGRAMS + 1];
    for (int i = 0; i <= count; i++)
        items[i] = labels[i];
    int selected = 0;
    int pick = run_list("Export Programs", items, count + 1, NULL, &selected, "EXPRT");
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick == LIST_BACK)
        return EDIT_CANCEL;

    char name[STATEFS_NAME_MAX + 1];
    if (pick == 0)
        strcpy(name, "ALL");
    else
        name_from_label(labels[pick], name);
    edit_result_t r = edit_name("Export Programs", name);
    if (r != EDIT_OK)
        return r;

    char path[48];
    program_path(name, path, sizeof(path));
    char line[40];
    if (statefs_file_exists(path)) {
        snprintf(line, sizeof(line), "%s.raw already exists.", name);
        const char *const lines[] = {line, "Replace it?"};
        r = show_message("Export Programs", lines, 2, CONFIRM("REPLC"));
        if (r != EDIT_OK)
            return r;
    }

    show_busy("Export Programs", "Saving...");
    statefs_mkdir(PROGRAMS_DIR);
    uint32_t errors_before = flashdisk_error_count();
    bool ok = free42_export_programs(pick - 1, path);
    statefs_sync();
    ok = ok && flashdisk_error_count() == errors_before;
    snprintf(line, sizeof(line), ok ? "Saved %s.raw" : "Could not save %s.raw", name);
    return show_result("Export Programs", ok, line);
}

/* Adds the file's programs to the calculator's -- nothing in it is
 * replaced, so no question first. */
static edit_result_t do_import_programs(const char *name) {
    show_busy("Import Programs", "Loading...");
    char path[48];
    program_path(name, path, sizeof(path));
    int added = free42_import_programs(path);
    char line[40];
    if (added < 0)
        snprintf(line, sizeof(line), "Could not read %s.raw", name);
    else
        snprintf(line, sizeof(line), "Imported %d program%s", added, added == 1 ? "" : "s");
    return show_result("Import Programs", added >= 0, line);
}

static edit_result_t do_rename_program_file(const char *old_name) {
    char new_name[STATEFS_NAME_MAX + 1]; /* a longer name (copied in) is cut to what can be typed */
    snprintf(new_name, sizeof(new_name), "%s", old_name);
    edit_result_t r = edit_name("Rename File", new_name);
    if (r != EDIT_OK || strcmp(new_name, old_name) == 0)
        return r;

    char old_path[48], new_path[48], line[40];
    program_path(old_name, old_path, sizeof(old_path));
    program_path(new_name, new_path, sizeof(new_path));
    if (statefs_file_exists(new_path)) {
        snprintf(line, sizeof(line), "%s.raw already exists", new_name);
        return show_result("Rename File", false, line);
    }
    bool ok = statefs_rename(old_path, new_path) == 0;
    statefs_sync();
    if (ok)
        snprintf(line, sizeof(line), "Renamed to %s.raw", new_name);
    else
        snprintf(line, sizeof(line), "Could not rename %.20s", old_name);
    return show_result("Rename File", ok, line);
}

static edit_result_t do_delete_program_file(const char *name) {
    char line[40];
    snprintf(line, sizeof(line), "Delete %.24s.raw?", name);
    const char *const lines[] = {line, "", "The programs in the calculator", "stay. This cannot be undone."};
    edit_result_t r = show_message("Delete File", lines, 4, CONFIRM("DEL"));
    if (r != EDIT_OK)
        return r;

    char path[48];
    program_path(name, path, sizeof(path));
    bool ok = statefs_remove(path) == 0;
    statefs_sync();
    snprintf(line, sizeof(line), ok ? "Deleted %s.raw" : "Could not delete %s.raw", name);
    return show_result("Delete File", ok, line);
}

/* The .raw files in PROGRAMS_DIR, a list: IMPRT (or ENTER) adds the picked
 * file's programs to the calculator's, EXPRT writes the calculator's to a
 * file, RENAM and DEL act on the one picked: with none, only EXPRT and
 * EXIT show. */
static edit_result_t do_programs(void) {
    if (!statefs_ready())
        return no_disk();
    static char names[MAX_PROGRAM_FILES][PROGRAM_FILE_MAX + 1];
    static const char *items[MAX_PROGRAM_FILES];
    static int selected = 0;
    px_ui_list_t l = {"Programs", items, 0, NULL, NULL, "No .raw files yet.", {""}, 0, -1, selected};
    while (true) {
        l.count = statefs_list_files(PROGRAMS_DIR, ".raw", (char *) names, PROGRAM_FILE_MAX + 1, MAX_PROGRAM_FILES);
        for (int i = 0; i < l.count; i++)
            items[i] = names[i];
        l.tabs[0] = l.count > 0 ? "IMPRT" : "";
        l.tabs[1] = "EXPRT";
        l.tabs[2] = l.count > 0 ? "RENAM" : "";
        l.tabs[3] = l.count > 0 ? "DEL" : "";
        int key = ui_list(&menu_ui, &l);
        selected = l.selected;
        if (key == PX_UI_QUIT)
            return EDIT_POWER_OFF;
        if (key == PX_UI_BACK)
            return EDIT_CANCEL;
        edit_result_t r = key == 1   ? do_export_programs()
                          : key == 2 ? do_rename_program_file(names[l.selected])
                          : key == 3 ? do_delete_program_file(names[l.selected])
                                     : do_import_programs(names[l.selected]); /* IMPRT or ENTER */
        if (r == EDIT_POWER_OFF)
            return r;
    }
}

/* ===================== Splash ===================== */

enum { SPLASH_TIMER, SPLASH_IMAGE, SPLASH_COUNT };

static char splash_names[MAX_SPLASHES][SPLASH_NAME_MAX + 1];
static const char *splash_rows[MAX_SPLASHES + 1];
static int splash_pick, splash_in_use;

/* Image picked from the pull-down: loaded and shown full screen; USE keeps
 * it, EXIT goes back to the one in use. */
static int splash_changed(px_ui_form_t *form, int field, void *ctx) {
    (void) form, (void) ctx;
    if (field != SPLASH_IMAGE || splash_pick == splash_in_use)
        return 0;
    const char *name = splash_pick == 0 ? NULL : splash_names[splash_pick - 1];
    char error[48];
    show_busy("Splash Image", "Loading...");
    edit_result_t r;
    if (!splash_preview_load(name, error, sizeof(error))) {
        error_beep();
        char line[40];
        snprintf(line, sizeof(line), "Can't use %.24s:", name);
        const char *const lines[] = {line, error, "", "Use a non-interlaced PNG;", "best at 250 x 122 pixels."};
        r = show_message("Splash Image", lines, 5, TABS("ENTER")) == EDIT_POWER_OFF ? EDIT_POWER_OFF : EDIT_CANCEL;
    } else {
        r = preview_splash();
    }
    if (r == EDIT_OK) {
        snprintf(px_settings.splash_image, sizeof(px_settings.splash_image), "%s", name != NULL ? name : "");
        splash_preview_use();
        settings_save();
        splash_in_use = splash_pick;
    } else {
        splash_pick = splash_in_use;
    }
    return r == EDIT_POWER_OFF ? PX_UI_QUIT : 0;
}

/* The picture shown while off, a form: Timer opens the minutes, Image is a
 * pull-down of Built-In and the PNGs in /SPLASH. */
static edit_result_t do_splash(void) {
    int count = statefs_list_files(STATEFS_SPLASH_DIR, ".png", (char *) splash_names, SPLASH_NAME_MAX + 1,
                                   MAX_SPLASHES);
    splash_rows[0] = "Built-In";
    splash_in_use = 0;
    for (int i = 0; i < count; i++) {
        splash_rows[i + 1] = splash_names[i];
        if (strcmp(splash_names[i], px_settings.splash_image) == 0)
            splash_in_use = i + 1;
    }
    splash_pick = splash_in_use;
    char timer[16];
    px_ui_field_t fields[SPLASH_COUNT] = {
            [SPLASH_TIMER] = {"Timer", PX_UI_BUTTON, 0, NULL, NULL, 0, NULL, 0, 0, 0, timer},
            [SPLASH_IMAGE] = {"Image", PX_UI_CHOICE, 0, &splash_pick, splash_rows, count + 1, NULL, 0, 0, 0, NULL},
    };
    static int selected = 0;
    px_ui_form_t form = {"Splash Image", fields, SPLASH_COUNT, {"ENTER"}, splash_changed, NULL, selected};
    while (true) {
        format_minutes(timer, sizeof(timer), px_settings.splash_minutes);
        int r = ui_form(&menu_ui, &form);
        selected = form.selected;
        if (r != PX_UI_ENTER) /* the only button: Timer */
            return r == PX_UI_QUIT ? EDIT_POWER_OFF : EDIT_CANCEL;
        if (do_minutes("Splash Timer", "Shown for", &px_settings.splash_minutes) == EDIT_POWER_OFF)
            return EDIT_POWER_OFF;
    }
}

/* ===================== Fonts ===================== */

#define MAX_FONTS 32

enum { FONT_ITEM_LARGE, FONT_ITEM_SMALL, FONT_ITEM_MINI, FONT_ITEM_COUNT };
static const char *const font_items[FONT_ITEM_COUNT] = {"Large Font (10x16)", "Small Font (8x12)",
                                                        "Mini Font (6x8)"};
static const font_kind_t font_item_kind[FONT_ITEM_COUNT] = {FONT_LARGE, FONT_SMALL, FONT_MINI};

static char *font_setting(font_kind_t kind) {
    return kind == FONT_SMALL ? px_settings.small_font : kind == FONT_LARGE ? px_settings.large_font
                                                                             : px_settings.mini_font;
}

static display_font_t *font_of(font_kind_t kind) {
    return kind == FONT_SMALL ? &font_small : kind == FONT_LARGE ? &font_large : &font_mini;
}

/* Every character of the font, as the calculator will draw it, with a hint
 * bar (in the menu's own font) over the bottom; ENTER = use it. */
static edit_result_t preview_font(const display_font_t *font) {
    int per_row = (DISPLAY_WIDTH - 2) / font->advance;
    while (true) {
        display_clear();
        for (int code = 0; code < 128; code += per_row) {
            char row[64]; /* the mini font: 41 a row */
            int n = 0;
            while (n < per_row && n < (int) sizeof(row) && code + n < 128) {
                row[n] = (char) (code + n);
                n++;
            }
            display_draw_chars(font, row, n, 1, (uint8_t) (code / per_row * (font->height + 1)), 0x00);
        }
        ui_tabs(&menu_ui, CONFIRM("USE"));
        display_flush();
        int r = tab_key();
        if (r >= 0)
            return (edit_result_t) r;
    }
}

static char font_names[FONT_ITEM_COUNT][MAX_FONTS][FONT_NAME_MAX + 1];
static const char *font_rows[FONT_ITEM_COUNT][MAX_FONTS + 1];
static int font_pick[FONT_ITEM_COUNT], font_in_use[FONT_ITEM_COUNT];

/* A font picked from its pull-down: loaded and shown, every character;
 * USE keeps it, EXIT goes back to the one in use. */
static int font_changed(px_ui_form_t *form, int field, void *ctx) {
    (void) form, (void) ctx;
    if (font_pick[field] == font_in_use[field])
        return 0;
    font_kind_t kind = font_item_kind[field];
    const char *title = font_items[field];
    char *setting = font_setting(kind);
    const char *name = font_pick[field] == 0 ? "" : font_names[field][font_pick[field] - 1];
    char error[40];
    edit_result_t r;
    show_busy(title, "Loading...");
    if (!fonts_select(kind, name, error, sizeof(error))) {
        error_beep();
        char line[40];
        snprintf(line, sizeof(line), "Can't use %.20s:", name);
        const char *const small[] = {line, error, "", "A small font is a PNG of",
                                     "128 x 96 pixels: 16 x 8", "glyphs of 8 x 12, no gaps."};
        const char *const large[] = {line, error, "", "A large font is a PNG of",
                                     "175 x 135 pixels: 16 x 8", "glyphs of 10 x 16, 1 apart."};
        const char *const mini[] = {line, error, "", "A mini font is a PNG of",
                                    "96 x 64 pixels: 16 x 8", "glyphs of 6 x 8, no gaps."};
        const char *const *help = kind == FONT_SMALL ? small : kind == FONT_LARGE ? large : mini;
        r = show_message(title, help, 6, TABS("ENTER")) == EDIT_POWER_OFF ? EDIT_POWER_OFF : EDIT_CANCEL;
    } else {
        r = preview_font(font_of(kind));
    }
    if (r == EDIT_OK) {
        snprintf(setting, FONT_SETTING_MAX + 1, "%s", name);
        settings_save();
        font_in_use[field] = font_pick[field];
    } else {
        /* Not taken: back to the one in use (the built-in font, should its
         * file have gone). */
        if (!fonts_select(kind, setting, error, sizeof(error)))
            fonts_select(kind, "", error, sizeof(error));
        font_pick[field] = font_in_use[field];
    }
    return r == EDIT_POWER_OFF ? PX_UI_QUIT : 0;
}

/* The calculator screen's fonts (fonts.h), a form of three pull-downs:
 * Built-In, then the sheets in /FONTS/LARGE, SMALL or MINI. This menu keeps
 * its own fonts. */
static edit_result_t do_fonts(void) {
    px_ui_field_t fields[FONT_ITEM_COUNT];
    for (int f = 0; f < FONT_ITEM_COUNT; f++) {
        int count = fonts_list(font_item_kind[f], font_names[f], MAX_FONTS);
        const char *setting = font_setting(font_item_kind[f]);
        font_rows[f][0] = "Built-In";
        font_in_use[f] = 0;
        for (int i = 0; i < count; i++) {
            font_rows[f][i + 1] = font_names[f][i];
            if (strcmp(font_names[f][i], setting) == 0)
                font_in_use[f] = i + 1;
        }
        font_pick[f] = font_in_use[f];
        fields[f] = (px_ui_field_t){font_items[f], PX_UI_CHOICE, 0, &font_pick[f], font_rows[f], count + 1,
                                    NULL, 0, 0, 0, NULL};
    }
    static int selected = 0;
    px_ui_form_t form = {"Fonts", fields, FONT_ITEM_COUNT, {"ENTER"}, font_changed, NULL, selected};
    int r = ui_form(&menu_ui, &form);
    selected = form.selected;
    return r == PX_UI_QUIT ? EDIT_POWER_OFF : EDIT_CANCEL;
}

/* ===================== Games & Apps ===================== */

/* ===================== the gear and Plus42 ===================== */

static edit_result_t do_sleep(void) {
    return do_minutes("Sleep Timer", "Off after", &px_settings.sleep_minutes);
}

static const char *const menu_style_names[MENU_STYLES] = {"Icons", "List"};

static edit_result_t do_menu_style(void) {
    int pick = choose("Menu Style", menu_style_names, MENU_STYLES, px_settings.menu_style);
    if (pick == LIST_POWER_OFF)
        return EDIT_POWER_OFF;
    if (pick == LIST_BACK)
        return EDIT_CANCEL;
    if (pick != px_settings.menu_style) {
        px_settings.menu_style = (uint8_t) pick;
        settings_save();
        style_changed = true;
    }
    return EDIT_OK;
}

/* Reset: Plus42's Memory Clear, asked twice as Format Drive, then a
 * restart. Only Plus42's memory (the user's choice): the RESET button also
 * clears the alarms and the stopwatch, this keeps them. */
static edit_result_t do_reset(void) {
    static const char *const what[] = {"Clear Plus42's memory and", "restart?", "",
                                       "Programs, variables, stack.", "Alarms, settings, files stay."};
    edit_result_t r = show_message("Reset", what, 5, CONFIRM("RESET"));
    if (r != EDIT_OK)
        return r;
    static const char *const sure[] = {"Are you sure?", "", "This cannot be undone."};
    r = show_message("Reset", sure, 3, CONFIRM("RESET"));
    if (r != EDIT_OK)
        return r;
    show_busy("Reset", "Restarting...");
    free42_memory_clear_restart();
}

static const page_item_t settings_page[] = {
        {"Date & Time", MENU_ICON_DATE_TIME, do_date_time, false},
        {"Fonts", MENU_ICON_FONTS, do_fonts, false},
        {"Sounds", MENU_ICON_SOUNDS, do_sounds, false},
        {"Printer", MENU_ICON_PRINTER, do_printer, false},
        {"Splash Image", MENU_ICON_SPLASH, do_splash, false},
        {"Sleep Timer", MENU_ICON_SLEEP, do_sleep, false},
        {"Menu Style", MENU_ICON_STYLE, do_menu_style, false},
        {"Reset", MENU_ICON_RESET, do_reset, false},
};

static const page_item_t plus42_page[] = {
        {"States", MENU_ICON_STATES, do_states, true},
        {"Programs", MENU_ICON_PROGRAMS, do_programs, false},
};

static edit_result_t do_settings(void) {
    static int selected = 0;
    return run_page("Settings", settings_page, sizeof(settings_page) / sizeof(settings_page[0]), &selected, "BACK");
}

static edit_result_t do_plus42(void) {
    static int selected = 0;
    return run_page("Plus42", plus42_page, sizeof(plus42_page) / sizeof(plus42_page[0]), &selected, "BACK");
}

/* ===================== the home screen ===================== */

/* An app's own icon, or the frame with its first letter. */
static void app_icon_bits(int app, uint8_t bits[MENU_ICON_BYTES], char *letter) {
    if (app_icon(app_dirs[app], app_names[app], bits))
        return;
    menu_icon_bits(MENU_ICON_FRAME, bits);
    char c = app_names[app][0];
    *letter = c >= 'a' && c <= 'z' ? (char) (c - 'a' + 'A') : c;
}

static void home_icon(int item, uint8_t bits[MENU_ICON_BYTES], char *letter) {
    int id = home_order[item];
    if (id < HOME_FIXED)
        menu_icon_bits(home_fixed[id].icon, bits);
    else
        app_icon_bits(id - HOME_FIXED, bits, letter);
}

/* UNFAV on a favourite: back to its folder. */
static const char *home_tab(int item) {
    return home_order[item] >= HOME_FIXED ? "UNFAV" : NULL;
}

/* ----- the Apps and Games folders ----- */

static void folder_icon(int item, uint8_t bits[MENU_ICON_BYTES], char *letter) {
    app_icon_bits(folder_order[item], bits, letter);
}

static const char *folder_tab(int item) {
    return "FAV";
}

static edit_result_t do_help(void) {
    static const char text[] =
            "THE ICONS\n"
            "The tabs: ENTER \x10 ^ \x0e \x0f and EXIT (BACK on the pages the home screen opens). "
            "\x10 \x0f, 4 6 and the arrow keys go to the icon before or after; ^ \x0e and 8 2 a row up "
            "or down. ENTER, or 5, opens the one picked. EXIT or BACK goes back; EXIT on the home "
            "screen goes back to the calculator, and on Off turns the calculator off.\n\n"
            "SHIFT\n"
            "On the home screen and in Apps and Games, SHIFT shows MOVE, FAV or UNFAV, and BACK, "
            "which puts the plain tabs back. On an icon that can't move or be a favourite, SHIFT "
            "changes nothing.\n\n"
            "MOVING ICONS\n"
            "SHIFT then MOVE picks up the icon picked: the arrows carry it over the others, which stay "
            "where they are. DONE (or ENTER) puts it down there; CANCL (or EXIT) puts it back where "
            "it was. Its old place is left empty; put down on another icon, the two change places. "
            "Off stays first.\n\n"
            "FAVOURITES\n"
            "In Apps and Games, SHIFT then FAV puts the app picked on the home screen, in its first "
            "empty place. On the home screen, SHIFT then UNFAV on a favourite sends it back to its "
            "folder.\n\n"
            "LISTS AND FORMS\n"
            "^ \x0e move, ENTER opens or chooses, EXIT or BACK goes back. A setting changes as soon "
            "as it's chosen; a font or a picture shows first, and USE keeps it.\n\n"
            "TURNING OFF\n"
            "Off, the first icon, SHIFT then EXIT, or hold EXIT for 2 seconds. From the calculator, "
            "SHIFT then EXIT opens this menu.";
    return ui_view_leave(&menu_ui, "Help", text, "BACK") == PX_UI_QUIT ? EDIT_POWER_OFF : EDIT_CANCEL;
}

/* Runs one; EDIT_POWER_OFF if it was ended to turn off. */
static edit_result_t run_app(int app) {
    bool power_off;
    const char *error = app_run(app_dirs[app], app_names[app], &power_off);
    idle_since = get_absolute_time();
    if (power_off)
        return EDIT_POWER_OFF;
    if (error == NULL)
        return EDIT_OK;
    error_beep();
    char head[40];
    snprintf(head, sizeof(head), "Can't start %.18s:", app_names[app]);
    const char *const lines[] = {head, error};
    return show_message(app_names[app], lines, 2, TABS("ENTER")) == EDIT_POWER_OFF ? EDIT_POWER_OFF : EDIT_CANCEL;
}

/* A folder's apps as icons, in their places; ENTER starts one, MOVE moves
 * it, FAV moves it to the home screen's first empty place (UNFAV there
 * brings it back). */
static edit_result_t do_folder(const char *title, const char *dir, const char *none) {
    static const grid_moves_t moves = {0, FOLDER_SLOTS, folder_move, folder_save};
    static int selected[2];
    int *sel = &selected[strcmp(dir, GAMES_DIR) == 0];
    while (true) {
        if (!statefs_ready())
            return no_disk();
        home_list(); /* again after each app: it may have been the drive's last look */
        int in_dir = folder_list(dir);
        if (folder_count == 0) {
            const char *const empty[] = {none, "", "Copy .bin files to the", dir + 1, "folder in USB Drive mode."};
            const char *const all_home[] = {"All of them are favourites,", "on the home screen. There, SHIFT",
                                            "then UNFAV puts one back here.", ""};
            edit_result_t r = in_dir == 0 ? show_message(title, empty, 5, TABS("ENTER"))
                                          : show_message(title, all_home, 3, TABS("ENTER"));
            return r == EDIT_POWER_OFF ? EDIT_POWER_OFF : EDIT_CANCEL;
        }
        int pick = run_grid(folder_names, folder_count, folder_icon, sel, &moves, folder_tab, "BACK");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;
        if (pick == GRID_SECOND) {
            if (!places_fav(*sel))
                error_beep(); /* the home screen is full */
            continue;
        }
        if (run_app(folder_order[pick]) == EDIT_POWER_OFF)
            return EDIT_POWER_OFF;
    }
}

/* ===================== the list style ===================== */

/* The apps or the games of dir, by name, as a list. */
static edit_result_t do_app_list(const char *title, const char *dir, const char *none) {
    if (!statefs_ready())
        return no_disk();
    home_list();
    static const char *items[2 * MAX_APPS];
    static int apps[2 * MAX_APPS];
    int count = 0;
    for (int app = 0; app < app_count; app++)
        if (strcmp(app_dirs[app], dir) == 0) {
            items[count] = app_names[app];
            apps[count++] = app;
        }
    if (count == 0) {
        const char *const lines[] = {none, "", "Copy .bin files to the", dir + 1, "folder in USB Drive mode."};
        return show_message(title, lines, 5, TABS("ENTER")) == EDIT_POWER_OFF ? EDIT_POWER_OFF : EDIT_CANCEL;
    }
    static int selected = 0;
    while (true) {
        int pick = run_list(title, items, count, NULL, &selected, "START");
        if (pick == LIST_POWER_OFF)
            return EDIT_POWER_OFF;
        if (pick == LIST_BACK)
            return EDIT_CANCEL;
        if (run_app(apps[pick]) == EDIT_POWER_OFF)
            return EDIT_POWER_OFF;
    }
}

enum {
    TEXT_OFF,
    TEXT_APPS,
    TEXT_GAMES,
    TEXT_DATE_TIME,
    TEXT_ALARMS,
    TEXT_SOUNDS,
    TEXT_FONTS,
    TEXT_SLEEP,
    TEXT_SPLASH,
    TEXT_PRINTER,
    TEXT_STATES,
    TEXT_PROGRAMS,
    TEXT_USB,
    TEXT_STYLE,
    TEXT_INFO,
    TEXT_RESET,
    TEXT_COUNT
};

static const char *const text_names[TEXT_COUNT] = {
        "Off",         "Apps",    "Games",  "Date & Time", "Alarms",    "Sounds",     "Fonts", "Sleep Timer",
        "Splash Image", "Printer", "States", "Programs",    "USB Drive", "Menu Style", "Info", "Reset",
};

static void text_value(int item, char *buf, size_t size) {
    switch (item) {
        case TEXT_OFF:
        case TEXT_ALARMS:
        case TEXT_INFO:
        case TEXT_RESET:
            buf[0] = 0;
            break;
        case TEXT_SLEEP:
            format_minutes(buf, size, px_settings.sleep_minutes);
            break;
        case TEXT_PRINTER:
            snprintf(buf, size, "%s", px_settings.printer < PRINTER_CHOICES ? printer_names[px_settings.printer] : "?");
            break;
        case TEXT_STYLE:
            snprintf(buf, size, "%s", menu_style_names[px_settings.menu_style]);
            break;
        default:
            snprintf(buf, size, ">");
            break;
    }
}

/* The menu as it was first: one list of everything. */
static settings_menu_result_t run_text_menu(void) {
    int selected = TEXT_OFF;
    while (true) {
        debug_checkpoint(CKPT_MENU, 0);
        int pick = run_list("Settings", text_names, TEXT_COUNT, text_value, &selected, "ENTER");
        if (pick == LIST_POWER_OFF)
            return SETTINGS_MENU_POWER_OFF;
        if (pick == LIST_BACK) /* EXIT on Off turns off, as on the icons */
            return selected == TEXT_OFF && last_key == K_EXIT ? SETTINGS_MENU_POWER_OFF : SETTINGS_MENU_EXIT;
        edit_result_t r = EDIT_CANCEL;
        switch (pick) {
            case TEXT_OFF: return SETTINGS_MENU_POWER_OFF;
            case TEXT_APPS: r = do_app_list("Apps", APPS_DIR, "No apps yet."); break;
            case TEXT_GAMES: r = do_app_list("Games", GAMES_DIR, "No games yet."); break;
            case TEXT_DATE_TIME: r = do_date_time(); break;
            case TEXT_ALARMS: r = do_alarms(); break;
            case TEXT_SOUNDS: r = do_sounds(); break;
            case TEXT_FONTS: r = do_fonts(); break;
            case TEXT_SLEEP: r = do_sleep(); break;
            case TEXT_SPLASH: r = do_splash(); break;
            case TEXT_PRINTER: r = do_printer(); break;
            case TEXT_STATES:
                if (do_states() == EDIT_OK)
                    return SETTINGS_MENU_EXIT; /* a state was loaded: show it */
                break;
            case TEXT_PROGRAMS: r = do_programs(); break;
            case TEXT_USB: r = do_usb_drive(); break;
            case TEXT_STYLE: r = do_menu_style(); break;
            case TEXT_INFO: r = do_info(); break;
            case TEXT_RESET: r = do_reset(); break;
        }
        if (r == EDIT_POWER_OFF)
            return SETTINGS_MENU_POWER_OFF;
        if (style_changed)
            return SETTINGS_MENU_EXIT;
    }
}

/* ===================== the icon style ===================== */

static settings_menu_result_t run_icon_menu(void) {
    static const grid_moves_t moves = {1, HOME_SLOTS, home_move, home_save};
    int selected = HOME_OFF;
    while (true) {
        debug_checkpoint(CKPT_MENU, 0); /* again after each item: what it did is over */
        home_list();
        int pick = run_grid(home_names, home_count, home_icon, &selected, &moves, home_tab, "EXIT");
        if (pick == LIST_POWER_OFF)
            return SETTINGS_MENU_POWER_OFF;
        if (pick == LIST_BACK) {
            /* EXIT is also the OFF key: on "Off" it turns the calculator
             * off, like ENTER. The backspace arrow still just leaves. */
            if (selected == HOME_OFF && last_key == K_EXIT)
                return SETTINGS_MENU_POWER_OFF;
            return SETTINGS_MENU_EXIT;
        }
        if (pick == GRID_SECOND) { /* UNFAV: its place goes blank, the others stay put */
            places_unfav(selected);
            continue;
        }

        edit_result_t r = EDIT_CANCEL;
        int id = home_order[pick];
        switch (id) {
            case HOME_OFF:
                return SETTINGS_MENU_POWER_OFF;
            case HOME_SETTINGS:
                r = do_settings();
                if (r == EDIT_OK)
                    return SETTINGS_MENU_EXIT; /* a state was loaded: show it */
                break;
            case HOME_PLUS42:
                r = do_plus42();
                if (r == EDIT_OK)
                    return SETTINGS_MENU_EXIT; /* a state was loaded: show it */
                break;
            case HOME_APPS: r = do_folder("Apps", APPS_DIR, "No apps yet."); break;
            case HOME_GAMES: r = do_folder("Games", GAMES_DIR, "No games yet."); break;
            case HOME_USB: r = do_usb_drive(); break;
            case HOME_INFO: r = do_info(); break;
            case HOME_ALARMS: r = do_alarms(); break;
            case HOME_HELP: r = do_help(); break;
            default: r = run_app(id - HOME_FIXED); break;
        }
        if (r == EDIT_POWER_OFF)
            return SETTINGS_MENU_POWER_OFF;
        if (style_changed)
            return SETTINGS_MENU_EXIT;
    }
}

settings_menu_result_t settings_menu_run(void) {
    shift_on = false;
    idle_since = get_absolute_time();
    while (true) {
        style_changed = false;
        settings_menu_result_t r = px_settings.menu_style == MENU_STYLE_LIST ? run_text_menu() : run_icon_menu();
        if (!style_changed || r == SETTINGS_MENU_POWER_OFF)
            return r;
    }
}
