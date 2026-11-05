#include <stdio.h>
#include <string.h>
#include <string>

#include "free42_app.h"
#include "core_main.h"
#include "core_globals.h"
#include "core_helpers.h"
#include "core_tables.h"
#include "core_display.h"
#include "display.h"
#include "keyboard.h"
#include "keymap.h"
#include "alphakeys.h"
#include "alarms.h"
#include "stopwatch.h"
#include "px_commands.h"
#include "core_commands2.h"
#include "core_equations.h"
#include "timemodule.h"
#include "power.h"
#include "sounds.h"
#include "fonts.h"
#include "display_fb.h"
#include "px_layer.h"
#include "icons.h"
#include "rtc.h"
#include "safefile.h"
#include "battery.h"
#include "splash.h"
#include "power.h"
#include "settings.h"
#include "sounds.h"
#include "printer.h"
#include "ramstate.h"
#include "statefs.h"
#include "shell.h"
#include "hardware/watchdog.h"
#include "watchdog_cfg.h"
#include "hardware/powman.h"
#include "pico/time.h"
#include "debug_checkpoint.h"

/* Key codes, from the standard Free42 keymap (see keymap.txt in the
 * upstream repo): digits 0-9, the four arithmetic operators, and ENTER.
 */
static int key_for_char(char c) {
    switch (c) {
        case '0': return 34;
        case '1': return 29;
        case '2': return 30;
        case '3': return 31;
        case '4': return 24;
        case '5': return 25;
        case '6': return 26;
        case '7': return 19;
        case '8': return 20;
        case '9': return 21;
        case '+': return 37;
        case '-': return 32;
        case '*': return 27;
        case '/': return 22;
        case '.':
        case ',': return 35; /* decimal point */
        case 'e':
        case 'E': return 16; /* EEX / enter exponent */
        case '\r':
        case '\n': return 13; /* ENTER */
        default: return 0;
    }
}

/* core_keydown() and core_keyup() (core_main.h) both return a "call me
 * again, right away, with key=0" flag -- set when the calculator is
 * running a user program (or continuing an interruptible operation) and
 * is only handing control back because it noticed a pending event. The
 * reference shells drive this from a timer, calling core_keydown(0, ...)
 * until it returns false.
 *
 * Programs run in time slices, like the reference shells: pump_running()
 * only records that the core wants the CPU back; the main loop then calls
 * free42_service() on every pass, which runs one slice -- core_keydown(0)
 * until shell_wants_cpu() (free42_slice_expired()) says the slice is used
 * up. Between slices the main loop feeds the watchdog and scans the
 * keyboard, so a long program can't starve the watchdog (it used to run
 * to completion inside one call, and anything over 100ms rebooted the
 * calculator), and EXIT or R/S stops it -- core_keydown() handles that
 * itself while a program runs. */
#define SLICE_US 20000 /* each slice: 20ms of program execution */

static bool run_pending = false;      /* the core wants core_keydown(0) calls */
static uint64_t slice_start_us;
/* The alarms: section below. */
static int alarm_active = -1;
static bool alarm_woke;      /* on only for an alarm: back off after it */
static int almcat_at = -1;
static void alarm_end(bool acknowledged);
static bool almcat_key(int code);
/* The stopwatch: its section is below the alarms'. */
static bool sw_showing = false;
static absolute_time_t sw_next;
static void sw_key_down(int code);
static void sw_key_up(void);

/* CLOCK: a message that keeps being redrawn until the next key, on the second
 * (the date's format: on the minute, which saves power, as on the HP-41). */
static bool clock_showing = false;
static absolute_time_t clock_next;

static bool timeout3_armed = false;   /* shell_request_timeout3(): PSE etc. */
static absolute_time_t timeout3_at;

static void pump_running(bool keep_running) {
    run_pending = keep_running;
}

bool free42_slice_expired(void) {
    return time_us_64() - slice_start_us >= SLICE_US;
}

/* Printing: on from the first line, off in free42_service() when the last has
 * been sent. */
#define PRINT_ICON_CHECK_MS 250
static bool print_icon;

/* A running alarm's next step, or the next alarm: alarm_service() below. */
static bool alarm_next_timeout(absolute_time_t *at);

bool free42_next_timeout(absolute_time_t *at) {
    absolute_time_t alarm;
    bool found = false;
    if (timeout3_armed) {
        *at = timeout3_at;
        found = true;
    }
    if (clock_showing && (!found || absolute_time_diff_us(*at, clock_next) < 0)) {
        *at = clock_next;
        found = true;
    }
    if (sw_showing && stopwatch_ticking() && (!found || absolute_time_diff_us(*at, sw_next) < 0)) {
        *at = sw_next;
        found = true;
    }
    if (alarm_next_timeout(&alarm) && (!found || absolute_time_diff_us(*at, alarm) < 0)) {
        *at = alarm;
        found = true;
    }
    if (print_icon) { /* free42_service() takes it away when printing ends */
        absolute_time_t soon = make_timeout_time_ms(PRINT_ICON_CHECK_MS);
        if (!found || absolute_time_diff_us(*at, soon) < 0)
            *at = soon;
        found = true;
    }
    return found;
}

void free42_request_timeout3(int delay_ms) {
    timeout3_at = make_timeout_time_ms((uint32_t) delay_ms);
    timeout3_armed = true;
}

/* ============================ the screen ============================ */

/* Plus42 draws its whole display -- stack, menus, messages, program
 * listings, the equation editor, plots -- into a bitmap of disp_r x disp_c
 * characters of 6 x 8 pixels and hands it to shell_blitter(). The PX
 * doesn't scale that bitmap up: it draws each character Plus42 put in a
 * cell with a font of its own (display_cell_char(), core_display.cc), the
 * menu as tabs from the keys' labels (display_key_label()), and only what
 * isn't a character -- a plot, the equation editor's cursor -- from
 * Plus42's pixels, cell by cell (draw_cell_pixels()).
 *
 * The number of rows is Plus42's own -- ROW+, ROW-, SETDS; 5 after Memory
 * Clear. A menu is always the same: tabs in the small font along the
 * bottom (MENU_AREA). The lines of text share what's left in equal slots,
 * each centred in its slot, so there's space above the first line and
 * below the last; with no menu, the last row is a line of text too and
 * they share it all. The lines get the biggest font whose slot (with a
 * menu up) leaves room for it (LAYOUTS, biggest first): the large font
 * only with a dotted line between every two lines (DIVIDER_GAP), the small
 * and mini ones with them when they fit between all of them. The fonts are
 * fonts.h's, from the drive or built in. The columns follow from the font:
 * as many as fit across (Plus42 won't go below 22). */
typedef struct {
    const display_font_t *font;
    uint8_t cell_w, cell_h;      /* a character on the panel */
    uint8_t cols;
    uint8_t min_slot;            /* a line's least height, with its space */
} layout_t;

#define DIVIDER_GAP  3   /* blank, dotted, blank */

static const layout_t LAYOUTS[] = {
    { &font_large, 11, 16, 22, 16 + DIVIDER_GAP },
    { &font_small,  8, 12, 31, 12 + 1 },
    { &font_mini,   6,  8, 41, 8 },
};
#define LAYOUT_COUNT (sizeof(LAYOUTS) / sizeof(LAYOUTS[0]))

#define STATUS_H     12  /* font_small's height */
#define MENU_H       14  /* the menu tabs: font_small and a 2px underline */
#define MENU_AREA    (MENU_H + 2)
#define LCD_TOP      STATUS_H
#define LCD_AVAIL    (DISPLAY_HEIGHT - STATUS_H)
#define TEXT_AVAIL   (LCD_AVAIL - MENU_AREA) /* with a menu up */
#define MAX_ROWS     (1 + TEXT_AVAIL / 8)

/* The keyboard is printed like the HP-42S's: CATALOG's first row has FCN
 * and MEM (skin flags 1 and 2, see core_repaint_display()); Shift-0 is
 * TOP.FCN. */
#define SKIN_FLAGS   3

static const layout_t *layout = &LAYOUTS[0];

/* Plus42's bitmap (shell_blitter()): rows of lcd_bpl bytes, LSB-first. */
static const char *lcd_bits;
static int lcd_bpl;

/* The screen changed since it was last sent to the panel. */
static bool screen_dirty = false;

void shell_blitter(const char *bits, int bytesperline, int x, int y,
                    int width, int height) {
    lcd_bits = bits;
    lcd_bpl = bytesperline;
    screen_dirty = true;
}

/* ROW+, SETDS...: fit_display() picks the new size up after the key. */
void shell_request_display_size(int rows, int cols) {
}

/* The equation editor's alternate key legends -- this keyboard's are
 * printed on it. */
void shell_set_skin_mode(int mode) {
}

/* Plus42 at the rows asked for (at most MAX_ROWS), the columns of the
 * biggest font whose lines fit with a menu up -- the smallest slot is
 * TEXT_AVAIL / lines, rounded down. attach: just started, from a state or not -- Plus42 has to draw
 * its whole screen again, even at the size it already has: a state brings
 * its screen's pixels, but not what core_display.cc's PX42 hooks learned
 * from drawing them (which character is in each cell, the menu's labels),
 * and without those the PX can only stretch the pixels. */
static void fit_display(bool attach) {
    int rows = requested_disp_r < 2 ? 2 : requested_disp_r > MAX_ROWS ? MAX_ROWS : requested_disp_r;
    int slot = TEXT_AVAIL / (rows - 1);
    layout = &LAYOUTS[LAYOUT_COUNT - 1];
    for (size_t i = 0; i < LAYOUT_COUNT; i++)
        if (LAYOUTS[i].min_slot <= slot) {
            layout = &LAYOUTS[i];
            break;
        }
    if (attach)
        force_redisplay = true;
    if (rows != disp_r || layout->cols != disp_c || force_redisplay)
        core_repaint_display(rows, layout->cols, SKIN_FLAGS);
}

/* The top of line r's slot, of lines sharing height; row_top(lines) is
 * the bottom of the last. */
static int row_top(int r, int lines, int height) {
    return LCD_TOP + r * height / lines;
}

static bool lcd_pixel(int x, int y) {
    return (lcd_bits[y * lcd_bpl + (x >> 3)] >> (x & 7)) & 1;
}

/* Plus42's pixels for cell (row, col), stretched to w x h at (x, y): from 6
 * x 8 to 11 x 16, the glyph's 5 columns are doubled and the gap isn't. */
static void draw_cell_pixels(int row, int col, int x, int y, int w, int h) {
    for (int py = 0; py < h; py++) {
        int sy = row * 8 + py * 8 / h;
        for (int px = 0; px < w; px++) {
            int sx = col * 6 + px * 6 / w;
            if (sx < disp_w && lcd_pixel(sx, sy))
                display_set_pixel((uint8_t) (x + px), (uint8_t) (y + py), true);
        }
    }
}

/* A plot: Plus42's whole bitmap, as big as fits below the status bar, the
 * same scale both ways. */
static void draw_plot() {
    int out_w = DISPLAY_WIDTH, out_h = disp_h * DISPLAY_WIDTH / disp_w;
    if (out_h > LCD_AVAIL) {
        out_h = LCD_AVAIL;
        out_w = disp_w * LCD_AVAIL / disp_h;
    }
    int x0 = (DISPLAY_WIDTH - out_w) / 2, y0 = LCD_TOP + (LCD_AVAIL - out_h) / 2;
    for (int py = 0; py < out_h; py++)
        for (int px = 0; px < out_w; px++)
            if (lcd_pixel(px * disp_w / out_w, py * disp_h / out_h))
                display_set_pixel((uint8_t) (x0 + px), (uint8_t) (y0 + py), true);
}

/* The character the fonts draw for cell (row, col), which holds ch, or -1
 * if they have none (Plus42's pixels then). Plus42 writes the stack's
 * labels with characters of its own: "x\200", "\201\200", "z\200",
 * "\203\200" for x: y: z: t:, in small capitals, and "\204\200" (LASTx),
 * "\205\200" (ALPHA) -- \200 a narrow colon. The fonts have capitals and a
 * colon: X: Y: Z: T: L: A:. */
static int font_char(int row, int col, int ch) {
    switch (ch) {
        case 0200: return ':';
        case 0201: return 'Y';
        case 0203: return 'T';
        case 0204: return 'L';
        case 0205: return 'A';
        case 'x':
        case 'z':
            return display_cell_char(row, col + 1) == 0200 ? ch - 'a' + 'A' : ch;
        default:
            return ch < 128 ? ch : -1;
    }
}

/* Menu key i (0-5) as a tab along the bottom MENU_H rows: a black
 * rectangle with its white label, truncated to what fits. The top corners are trimmed
 * (2px, then 1px) to read as rounded; the label sits at the top, leaving
 * the rows below it for a highlighted item's underline (RAD in MODES while
 * in RAD mode). A reversed key is white with a black frame. Every tab
 * keeps a 1px gap after it, the last one included. */
static void draw_menu_tab(int i, const char *text, int text_len, bool highlight, bool reverse) {
    int x0 = (i * DISPLAY_WIDTH) / 6;
    int x1 = ((i + 1) * DISPLAY_WIDTH) / 6 - 2;
    int y0 = DISPLAY_HEIGHT - MENU_H;
    int bottom = DISPLAY_HEIGHT - 1;

    display_fill_rect((uint8_t) (x0 + 2), (uint8_t) y0, (uint8_t) (x1 - 2), (uint8_t) y0, true);
    display_fill_rect((uint8_t) (x0 + 1), (uint8_t) (y0 + 1), (uint8_t) (x1 - 1), (uint8_t) (y0 + 1), true);
    display_fill_rect((uint8_t) x0, (uint8_t) (y0 + 2), (uint8_t) x1, (uint8_t) bottom, true);
    if (reverse)
        display_fill_rect((uint8_t) (x0 + 1), (uint8_t) (y0 + 2), (uint8_t) (x1 - 1), (uint8_t) (bottom - 1), false);

    int max_chars = (x1 - x0 + 1) / font_small.advance;
    if (text_len > max_chars)
        text_len = max_chars;
    int tx = x0 + ((x1 - x0 + 1) - text_len * font_small.advance) / 2;
    for (int k = 0; k < text_len; k++)
        tx = display_draw_char(&font_small, text[k], (uint8_t) tx, (uint8_t) y0, reverse ? 0x00 : 0xFF);

    if (highlight)
        display_fill_rect((uint8_t) x0, (uint8_t) (bottom - 1), (uint8_t) x1, (uint8_t) bottom, reverse);
}

/* The core's state, as alphakeys.c needs it. */
static alpha_state_t alpha_state() {
    int menu = get_front_menu();
    alpha_state_t s;
    s.mode = px_settings.alpha_mode;
    s.shift = mode_shift;
    s.menu = !alpha_active()           ? ALPHA_MENU_NONE
             : menu == MENU_ALPHA1     ? ALPHA_MENU_PAGE1
             : menu == MENU_ALPHA2     ? ALPHA_MENU_PAGE2
                                       : ALPHA_MENU_GROUP;
    s.reg_entry = mode_command_entry && (incomplete_command == CMD_ASTO || incomplete_command == CMD_ARCL);
    s.reg_length = incomplete_length;
    s.reg_alpha = incomplete_alpha;
    return s;
}

static bool shapes_showing();

/* The calculator's part of the panel, below the status bar, from scratch. */
static void draw_lcd() {
    display_fill_rect(0, LCD_TOP, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1, false);
    if (lcd_bits == NULL)
        return;
    if (mode_plot_viewer) {
        draw_plot();
        return;
    }
    const layout_t *l = layout;
    int x0 = (DISPLAY_WIDTH - disp_c * l->cell_w) / 2;
    if (x0 < 0)
        x0 = 0;

    bool menu = false, shown[6];
    const char *labels[6];
    int lengths[6];
    bool highlights[6], reverses[6];
    for (int i = 0; i < 6; i++) {
        shown[i] = display_key_label(i, &labels[i], &lengths[i], &highlights[i], &reverses[i]);
        menu = menu || shown[i];
    }
    alpha_state_t as = alpha_state();
    bool hidden = alpha_menu_hidden(&as);
    /* The stopwatch has all the keys, and the core waiting for it hasn't redrawn
     * since the key that started it: those tabs would be the catalog's, or nobody's. */
    for (int i = 0; i < 6; i++)
        if (shown[i] && !hidden && !sw_showing)
            draw_menu_tab(i, labels[i], lengths[i], highlights[i], reverses[i]);

    int lines = menu ? disp_r - 1 : disp_r;
    int height = menu ? TEXT_AVAIL : LCD_AVAIL;
    if (lines < 1)
        return;
    /* Dotted lines between all the lines of text, or none. */
    /* A picture (CLLCD, graphics) has no lines of text to divide. */
    bool dividers = height / lines - l->cell_h >= DIVIDER_GAP && mode_message_lines != ALL_LINES && !shapes_showing();
    int prev_bottom = -1;
    for (int r = 0; r < lines; r++) {
        int top = row_top(r, lines, height);
        int y = top + (row_top(r + 1, lines, height) - top - l->cell_h) / 2;
        if (dividers && prev_bottom >= 0)
            display_draw_dotted_hline(0, DISPLAY_WIDTH - 1, (uint8_t) ((prev_bottom + y) / 2));
        prev_bottom = y + l->cell_h;
        for (int c = 0; c < disp_c; c++) {
            int x = x0 + c * l->cell_w;
            int ch = display_cell_char(r, c);
            if (ch == ' ')
                continue;
            int fc = ch >= 0 ? font_char(r, c, ch) : -1;
            if (fc >= 0)
                display_draw_char(l->font, (char) fc, (uint8_t) x, (uint8_t) y, 0x00);
            else
                draw_cell_pixels(r, c, x, y, l->cell_w, l->cell_h);
        }
    }
}

/* Shapes (BOX, ELLIPSE ...) use the panel's 250 x 122, which Plus42's bitmap
 * can't: it has as many dots as its rows and columns make, and those are redrawn
 * in fonts. So they're two pictures on top of what's drawn, each a whole panel:
 * dots painted black and dots painted white, either way over Plus42's own text
 * and over the status bar. They last as Plus42's graphics do: a message's lines
 * are what keeps them (mode_message_lines), so a key clears them, and so does
 * CLLCD (free42_forget_shapes()). The memory is only taken by a program that
 * draws, and then kept. */
static uint8_t *shape_ink, *shape_wipe;
static bool shapes_shown;

static bool shapes_showing() {
    return shapes_shown && mode_message_lines != 0;
}

bool free42_shape_memory(void) {
    if (shape_ink == NULL) {
        shape_ink = (uint8_t *) malloc(2 * FB_SIZE);
        if (shape_ink == NULL)
            return false;
        shape_wipe = shape_ink + FB_SIZE;
    }
    return true;
}

bool free42_shape_canvases(px_canvas_t *ink, px_canvas_t *wipe) {
    if (!free42_shape_memory())
        return false;
    if (!shapes_showing()) {
        memset(shape_ink, 0, 2 * FB_SIZE);
        shapes_shown = true;
    }
    *ink = {shape_ink, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE};
    *wipe = {shape_wipe, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE};
    return true;
}

void free42_shapes_changed(void) {
    screen_dirty = true;
}

void free42_forget_shapes(void) {
    shapes_shown = false;
}

static void draw_shapes() {
    if (!shapes_showing())
        return;
    const px_canvas_t ink = {shape_ink, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE};
    const px_canvas_t wipe = {shape_wipe, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE};
    px_layer_compose(&display_fb[0][0], &ink, &wipe);
}

/* The HP-42S's annunciators, in the status bar: the last value of each
 * (-1 = unchanged). PRT is on for all of a listing, between its lines too: part
 * of what keeps print_icon on, see free42_service(). */
static bool ann_updn, ann_shift, ann_prt, ann_run, ann_grad, ann_rad;

void shell_annunciators(int updn, int shf, int prt, int run, int g, int rad) {
    if (updn != -1) ann_updn = updn;
    if (shf != -1) ann_shift = shf;
    if (prt != -1) ann_prt = prt;
    if (run != -1) ann_run = run;
    if (g != -1) ann_grad = g;
    if (rad != -1) ann_rad = rad;
    screen_dirty = true;
}

/* ===================== date and time formats ===================== */

/* Free42's own: flags DMY/YMD and its CLK12/CLK24 mode -- the commands
 * DMY, MDY, YMD, CLK12, CLK24 set them. px_settings keeps a copy, for the
 * PX-42F's own screens and for when the calculator isn't running (the
 * splash's clock while off). */
static uint8_t free42_date_format() {
    return flags.f.ymd ? RTC_YMD : flags.f.dmy ? RTC_DMY : RTC_MDY;
}

/* Free42 has no weekday-month-day mode: MDY, the same order, stands in. */
static uint8_t as_free42(uint8_t date_format) {
    return date_format == RTC_WMD ? RTC_MDY : date_format;
}

void free42_set_formats(uint8_t date_format, bool clock_24h) {
    date_format = as_free42(date_format);
    flags.f.ymd = date_format == RTC_YMD;
    flags.f.dmy = date_format == RTC_DMY;
    mode_time_clk24 = clock_24h;
}

/* A program (or the keyboard) may have run DMY, CLK12...: the copy follows.
 * Saved to flash at OFF, not on every change. */
static void mirror_formats() {
    uint8_t date_format = free42_date_format();
    if (date_format != as_free42(px_settings.date_format) || mode_time_clk24 != px_settings.clock_24h) {
        px_settings.date_format = date_format;
        px_settings.clock_24h = mode_time_clk24;
        settings_save_later();
    }
}

/* Status bar, white on black in font_small (12 pixels tall: no margin):
 * Plus42's header line -- the directory ("{ HOME }"), and the flags or the
 * BASE / TVM status right after it -- or, with HEADER off, the date and
 * time ("FRI-OCT-19 12:43PM") on the left, and from the right
 * edge leftwards: the battery, the angle mode, shift, the up/down arrows
 * and the running mark, each in a fixed place. A path too long to fit
 * loses its start, so the end of it and the status stay in view. */
#define STATUS_CHAR      8 /* font_small's advance: font sizes are fixed (fonts.h) */
#define STATUS_BATTERY_X (DISPLAY_WIDTH - 19) /* icons.h: 18 x 10, then a 1px margin */
#define STATUS_ANGLE_X   (STATUS_BATTERY_X - 2 - STATUS_CHAR)
#define STATUS_SHIFT_X   (STATUS_ANGLE_X - 2 - STATUS_CHAR)
#define STATUS_UPDN_X    (STATUS_SHIFT_X - 2 - 2 * STATUS_CHAR)
#define STATUS_RUN_X     (STATUS_UPDN_X - 2 - STATUS_CHAR)
#define STATUS_BELL_X    (STATUS_RUN_X - 2 - STATUS_CHAR)
#define STATUS_PRINT_X   (STATUS_BELL_X - 2 - ICON_PRINTER_W)
#define STATUS_ALPHA_W   (3 * STATUS_CHAR) /* [a]: in ALPHA, the header gives way, and to the printer */
#define STATUS_HEADER_X  1

static void status_text(const char *s, int length, int x) {
    display_draw_chars(&font_small, s, length, (uint8_t) x, 0, 0xFF);
}

/* font_small's character c, white, mirrored left to right. */
static void status_char_mirrored(char c, int x) {
    const display_font_t *f = &font_small;
    unsigned char uc = (unsigned char) c;
    if (uc < f->first_char || uc > f->last_char)
        return;
    const uint8_t *g = f->data + (uc - f->first_char) * f->bytes_per_char;
    for (int r = 0; r < f->height; r++)
        for (int col = 0; col < f->width; col++) {
            int bit = r * f->width + col;
            if ((g[bit >> 3] >> (7 - (bit & 7))) & 1)
                display_set_pixel((uint8_t) (x + f->width - 1 - col), (uint8_t) r, false);
        }
}

/* The status bar's text, at most max_chars: Plus42's header line, or
 * with HEADER off the date and time. */
static std::string header_text(int max_chars) {
    if (mode_header) {
        std::string path, info;
        display_header_text(&path, &info);
        if (!info.empty())
            path += " " + info;
        if ((int) path.length() > max_chars)
            path = "\032" + path.substr(path.length() - (max_chars - 1)); /* … */
        return path;
    }
    /* One clock reading for both, so date and time can't disagree across
     * midnight. In the calculator's formats (YMD/DMY/MDY, CLK12/CLK24). */
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    char date[12], time[12];
    rtc_format_day_month(date, sizeof(date), &now);
    rtc_format_time(time, sizeof(time), &now, true);
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%s %s", date, time);
    return std::string(buf, n < max_chars ? n : max_chars);
}

/* GRAD sets both annunciators ("G" and "RAD" on the HP-42S). ∡ is
 * Plus42's POLAR mark (HPOLAR), so DEG is a degree sign. */
static const char *angle_mark() {
    return ann_grad ? "G" : ann_rad ? "\007" : "\023"; /* G, π, ° */
}

/* Graphics have the whole panel, the bar's rows too, until they go. */
static void draw_status_bar(bool with_print_icon = true) {
    if (shapes_showing()) {
        display_fill_rect(0, 0, DISPLAY_WIDTH - 1, STATUS_H - 1, false);
        return;
    }
    display_fill_rect(0, 0, DISPLAY_WIDTH - 1, STATUS_H - 1, true);

    bool alpha = alpha_active();
    bool printing = print_icon && with_print_icon;
    int right = printing ? STATUS_PRINT_X : STATUS_BELL_X;
    int alpha_x = right - 2 - STATUS_ALPHA_W;
    std::string header = header_text(((alpha ? alpha_x : right) - 2 - STATUS_HEADER_X) / STATUS_CHAR);
    status_text(header.data(), (int) header.length(), STATUS_HEADER_X);
    if (alpha) /* the keyboard's mode in ALPHA: [ ] HP-42S, [a], [A] */
        status_text(px_settings.alpha_mode == ALPHA_MODE_LOWER ? "[a]"
                    : px_settings.alpha_mode == ALPHA_MODE_UPPER ? "[A]" : "[ ]", 3, alpha_x);

    status_text(angle_mark(), 1, STATUS_ANGLE_X);
    if (ann_updn)
        status_text("\136\016", 2, STATUS_UPDN_X); /* ↑↓ */
    if (ann_shift)
        status_char_mirrored('\r', STATUS_SHIFT_X); /* ↵ (code 13), pointing right */
    if (ann_run)
        status_text("\006", 1, STATUS_RUN_X); /* ▶ */
    if (alarms_count() > 0)
        icon_bell(STATUS_BELL_X, 1);
    if (printing)
        icon_printer(STATUS_PRINT_X, 1);

    icon_battery(STATUS_BATTERY_X, 1, battery_level());
}

#define PAPER_BAR_H 10 /* the status bar: a character, a dot above and below */
#define PAPER_GAP   5  /* between lines: blank, dotted in the middle, blank */

int free42_draw_paper(int paper_w) {
    int w = disp_c * 6;
    if (lcd_bits == NULL || mode_plot_viewer || w > paper_w || shapes_showing())
        return 0;
    int x0 = (paper_w - w) / 2;
    display_fill_rect(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1, false);

    display_fill_rect((uint8_t) (x0 - 1), 0, (uint8_t) (x0 + w), PAPER_BAR_H - 1, true);
    std::string header = header_text(disp_c - 2);
    display_draw_chars(&font_mini, header.data(), (int) header.length(), (uint8_t) x0, 1, 0xFF);
    display_draw_chars(&font_mini, angle_mark(), 1, (uint8_t) (x0 + w - 6), 1, 0xFF);

    bool menu = false;
    for (int i = 0; i < 6; i++) {
        const char *label;
        int length;
        bool highlight, reverse;
        menu = display_key_label(i, &label, &length, &highlight, &reverse) || menu;
    }
    int y = PAPER_BAR_H + 2;
    for (int r = 0; r < (menu ? disp_r - 1 : disp_r); r++) {
        if (r > 0) {
            display_draw_dotted_hline((uint8_t) x0, (uint8_t) (x0 + w - 1), (uint8_t) (y + PAPER_GAP / 2));
            y += PAPER_GAP;
        }
        for (int c = 0; c < disp_c; c++) {
            int ch = display_cell_char(r, c);
            int fc = ch >= 0 ? font_char(r, c, ch) : -1;
            if (fc >= 0)
                display_draw_char(&font_mini, (char) fc, (uint8_t) (x0 + c * 6), (uint8_t) y, 0x00);
            else
                draw_cell_pixels(r, c, x0 + c * 6, y, 6, 8);
        }
        y += 8;
    }
    alpha_state_t as = alpha_state();
    if (menu && !alpha_menu_hidden(&as)) {
        y += 3;
        for (int c = 0; c < disp_c; c++) /* Plus42's own menu keys, as the HP-42S prints them */
            draw_cell_pixels(disp_r - 1, c, x0 + c * 6, y, 6, 8);
        y += 8;
    }
    return y;
}

/* Draws the whole screen and sends it to the panel: after every key, and
 * while a program runs. A ROW+ or SETDS first gets its new size. */
/* Plus42 shows an error ("Divide by 0") silently; its squeak is for keys it
 * can't take. display_error() is wrapped at link time (CMakeLists.txt), so the
 * core stays as it is: errors flag 25 or a trap swallow never get here, and
 * those shown by core_display.cc itself (a menu with no variables) aren't
 * seen, the linker only wrapping calls from other files. Interrupted: the
 * user's own EXIT. */
static bool error_shown = false;

extern "C" void __real__Z13display_errori(int error);

extern "C" void __wrap__Z13display_errori(int error) {
    __real__Z13display_errori(error);
    if (error != ERR_INTERRUPTED)
        error_shown = true;
}

static void refresh() {
    mirror_formats();
    fit_display(false);
    draw_status_bar();
    draw_lcd();
    draw_shapes();
    debug_checkpoint(CKPT_DISPLAY_FLUSH, 0);
    display_flush();
    screen_dirty = false;
    /* After the panel: squeak() waits for the sound, the message comes first. */
    if (error_shown) {
        error_shown = false;
        if (px_settings.error_sound)
            squeak();
    }
}

/* ============================ start / state ============================ */

/* Where free42_sleep() puts the calculator's memory if it doesn't fit in
 * the RAM buffer (ramstate.c) -- at the drive's root, out of the STATE
 * list. */
#define OFF_STATE_FILE "/OFFSTATE.p42"

/* Plus42's state files carry their own version: core_init() only needs the
 * name. rows/cols come back as the state's display size; fit_display()
 * then sets the size this screen uses for its rows. */
static void core_start(const char *path) {
    int rows = 0, cols = 0;
    lcd_bits = NULL; /* core_cleanup() freed it */
    core_init(&rows, &cols, path);
    fit_display(true);
}

/* A copy of the memory on the drive (what a RESET press, an app starting
 * and a crash leave) -- for the boot log. */
bool free42_drive_copy_exists(void) {
    return statefs_ready() && statefs_file_exists(OFF_STATE_FILE);
}

static const char *state_source_name = "-";

const char *free42_state_source(void) {
    return state_source_name;
}

/* The alarms stay in RAM and reach the drive at OFF, as the settings do: a
 * program setting one in a loop must not wear the flash out. */
#define ALARMS_FILE "/ALARMS.DAT"

static void alarms_save() {
    if (!alarms_dirty() || !statefs_ready())
        return;
    size_t size;
    const void *image = alarms_image(&size);
    if (alarms_count() == 0 ? safefile_remove(ALARMS_FILE) : safefile_save(ALARMS_FILE, image, size))
        alarms_mark_saved();
}

#define STOPWATCH_FILE "/STOPWATCH.DAT"

static void stopwatch_save() {
    if (!stopwatch_dirty() || !statefs_ready())
        return;
    size_t size;
    const void *image = stopwatch_image(&size);
    if (stopwatch_is_zero() ? safefile_remove(STOPWATCH_FILE) : safefile_save(STOPWATCH_FILE, image, size))
        stopwatch_mark_saved();
}

static void stopwatch_read() {
    static char image[64];
    if (!statefs_ready())
        return;
    size_t size = safefile_read(STOPWATCH_FILE, false, image, sizeof(image));
    if (!stopwatch_load(image, size))
        stopwatch_load(image, safefile_read(STOPWATCH_FILE, true, image, sizeof(image)));
}

static void alarms_read() {
    static char image[sizeof(uint32_t) * 2 + ALARMS_MAX * sizeof(alarm_t) + 1];
    if (!statefs_ready())
        return;
    size_t size = safefile_read(ALARMS_FILE, false, image, sizeof(image));
    if (!alarms_load(image, size))
        alarms_load(image, safefile_read(ALARMS_FILE, true, image, sizeof(image)));
}

void free42_init(bool after_crash, bool memory_clear) {
    alarm_woke = power_woke_for_alarm();
    if (memory_clear) { /* with the rest of the memory, as on the HP-41 */
        if (statefs_ready()) {
            safefile_remove(ALARMS_FILE);
            safefile_remove(STOPWATCH_FILE);
        }
    } else {
        alarms_read();
        stopwatch_read();
    }
    /* The memory saved at the last OFF, from RAM or, failing that, the
     * drive. It's kept after loading, so a crash later on comes back to it
     * (the memory as of this ON) instead of Memory Clear -- but not when
     * this boot itself follows a crash: reading the RAM state uses it up,
     * and the file is deleted, so a state that crashes the calculator can
     * do it once more at most. */
    display_clear();
    /* The RESET button (main.c): Memory Clear. Neither copy of the memory
     * may come back at a later boot either -- the RAM one after a crash,
     * the drive's when the RAM one is missing. */
    if (memory_clear) {
        if (px_settings.alpha_mode != ALPHA_MODE_HP42S) { /* the keyboard too: as new */
            px_settings.alpha_mode = ALPHA_MODE_HP42S;
            settings_save_later();
        }
        ramstate_discard();
        if (statefs_ready() && statefs_file_exists(OFF_STATE_FILE)) {
            statefs_remove(OFF_STATE_FILE);
            statefs_sync();
        }
    }
    bool from_ram = ramstate_valid();
    if (from_ram) {
        state_source_name = "RAM";
        debug_checkpoint(CKPT_CORE_START, 0);
        core_start(RAMSTATE_PATH);
        debug_checkpoint(CKPT_CORE_POWERCYCLE, 0);
        pump_running(core_powercycle());
    } else if (statefs_ready() && statefs_file_exists(OFF_STATE_FILE)) {
        state_source_name = "drive";
        debug_checkpoint(CKPT_CORE_START, 1);
        core_start(OFF_STATE_FILE);
        if (after_crash) {
            statefs_remove(OFF_STATE_FILE);
            statefs_sync();
        }
        debug_checkpoint(CKPT_CORE_POWERCYCLE, 1);
        pump_running(core_powercycle());
    } else {
        state_source_name = "none (Memory Clear)";
        debug_checkpoint(CKPT_CORE_START, 2);
        core_start(NULL);
    }
    /* The formats as last saved (they follow Free42's DMY, CLK12... at
     * OFF): kept across a reboot, a reflash or Memory Clear. */
    free42_set_formats(px_settings.date_format, px_settings.clock_24h);
    watchdog_update(); /* loading a big memory, then drawing it */
    refresh();
    if (from_ram && !after_crash)
        ramstate_keep(); /* loaded and drawn fine */
}

void free42_process_char(char c) {
    int key = key_for_char(c);
    if (key == 0)
        return;

    bool enqueued;
    int repeat;
    pump_running(core_keydown(key, &enqueued, &repeat));
    pump_running(core_keyup());

    refresh();
}

/* ============================ the keyboard ============================ */

/* keymap.h's key codes are the core's. */
static_assert(HPK_STO == KEY_STO && HPK_RCL == KEY_RCL && HPK_RDN == KEY_RDN && HPK_SIN == KEY_SIN
              && HPK_COS == KEY_COS && HPK_TAN == KEY_TAN && HPK_SWAP == KEY_SWAP && HPK_CHS == KEY_CHS
              && HPK_E == KEY_E && HPK_UP == KEY_UP && HPK_DOWN == KEY_DOWN && HPK_DIV == KEY_DIV
              && HPK_MUL == KEY_MUL && HPK_SUB == KEY_SUB && HPK_ADD == KEY_ADD && HPK_DOT == KEY_DOT
              && HPK_RUN == KEY_RUN && HPK_EXIT == KEY_EXIT && HPK_0 == KEY_0 && HPK_1 == KEY_1
              && HPK_2 == KEY_2 && HPK_3 == KEY_3 && HPK_4 == KEY_4 && HPK_5 == KEY_5 && HPK_6 == KEY_6
              && HPK_7 == KEY_7 && HPK_8 == KEY_8 && HPK_9 == KEY_9 && HPK_ENTER == KEY_ENTER,
              "keymap.h's key codes differ from core_globals.h's");

/* State for the currently-held physical key, set by free42_key_down() and
 * consulted by the timeout/repeat/release functions below. held_key == 0
 * means "no key currently down" (either nothing is pressed, or the last
 * pressed position has no function mapped). */
static int held_key = 0;
static bool held_enqueued = false;
static int held_repeat = 0;

static bool settings_requested = false;

static_assert(FREE42_KEY_ENTER == KEY_ENTER && FREE42_KEY_BSP == KEY_BSP && FREE42_KEY_UP == KEY_UP
                      && FREE42_KEY_DOWN == KEY_DOWN && FREE42_KEY_SHIFT == KEY_SHIFT && FREE42_KEY_MINUS == KEY_SUB
                      && FREE42_KEY_EXIT == KEY_EXIT,
              "free42_app.h's key codes");

/* The key down, from free42_key_down() to free42_key_up(), and the shift
 * before it -- for free42_key_cancel(). */
static int down_code = 0;
static bool shift_before_down = false;

/* Only SHIFT has done anything by now that can be taken back: put the
 * shift as it was. Through the core's own SHIFT key, not by setting
 * mode_shift -- that also redraws the menus whose labels change with shift
 * (core_keydown_2()). With no calculator key down (the settings menu),
 * nothing to do. */
void free42_key_cancel(void) {
    if (sw_showing) { /* the core is waiting: it would queue the key */
        set_shift(shift_before_down);
        return;
    }
    if (down_code == KEY_SHIFT && mode_shift != shift_before_down) {
        bool enqueued;
        int repeat;
        pump_running(core_keydown(KEY_SHIFT, &enqueued, &repeat));
    }
}

int free42_key_code(uint8_t row, uint8_t col) {
    if (row >= KB_ROWS || col >= KB_COLS)
        return 0;
    return key_matrix[row][col];
}

/* Typing -- a number (digits, '.', E, CHS, and the editing ←, which is CLX
 * otherwise), text in ALPHA (the soft keys pick letters there too), a
 * command's argument (STO 01, XEQ "NAME") -- is ENTRY; so are SHIFT (it
 * changes nothing yet), the arrows and EXIT (they move, not compute). ENTER
 * is ENTER; anything else, a shifted key included, runs a command: OP. */
key_sound_t free42_key_sound(uint8_t row, uint8_t col) {
    int code = free42_key_code(row, col);
    if (code == KEY_ENTER && !mode_shift)
        return KEY_SOUND_ENTER;
    if (alpha_active() || mode_command_entry)
        return KEY_SOUND_ENTRY;
    switch (code) {
        case 0: /* no key */
        case KEY_SHIFT:
        case KEY_UP:
        case KEY_DOWN:
        case KEY_EXIT:
            return KEY_SOUND_ENTRY;
    }
    if (mode_shift)
        return KEY_SOUND_OP;
    if (hp_key_digit(code) || code == KEY_DOT || code == KEY_E || code == KEY_CHS || code == KEY_BSP)
        return KEY_SOUND_ENTRY;
    return KEY_SOUND_OP;
}

bool free42_settings_requested(void) {
    bool r = settings_requested;
    settings_requested = false;
    return r;
}

bool free42_settings_pending(void) {
    return settings_requested;
}

void free42_key_down(uint8_t row, uint8_t col) {
    down_code = free42_key_code(row, col);
    if (alarm_active >= 0) {
        /* STO keeps it as past due; anything else acknowledges it. */
        held_key = 0;
        held_enqueued = false;
        held_repeat = 0;
        alarm_end(down_code != KEY_STO);
        return;
    }
    alarm_woke = false;
    if (sw_showing) {
        shift_before_down = mode_shift;
        held_key = 0;
        held_enqueued = false;
        held_repeat = 0;
        sw_key_down(down_code);
        return;
    }
    if (almcat_at >= 0 && almcat_key(down_code)) {
        held_key = 0;
        held_enqueued = false;
        held_repeat = 0;
        return;
    }
    clock_showing = false; /* the core takes its message away, and does the key */
    shift_before_down = mode_shift;
    held_key = 0;
    held_enqueued = false;
    held_repeat = 0;

    if (row >= KB_ROWS || col >= KB_COLS)
        return;

    /* A key during PSE (or the brief linger after SHOW/MEM): tell the core
     * a key is on its way, as the reference shells do -- the key itself
     * then ends the pause. */
    if (timeout3_armed) {
        timeout3_armed = false;
        core_timeout3(false);
    }

    bool enqueued;
    int repeat;
    int code = key_matrix[row][col];

    /* SHIFT+EXIT -- OFF on the real HP-42S -- opens the PX-42 settings menu
     * instead (its first item is Off). Taken before the core ever sees the
     * key, in every mode; the main loop runs the menu once the key is
     * released (free42_settings_requested()). OFF as a command -- XEQ "OFF"
     * or in a program -- still powers off directly via shell_powerdown(). */
    if (mode_shift && code == KEY_EXIT) {
        set_shift(false);
        settings_requested = true;
        refresh();
        return;
    }

    /* R/S stops a sound from PLAYWAV or PLAYMID, which can outlast the program.
     * A running program stops too, with the key; with none running the key
     * does no more, or it would start the program again. GETKEY and anything
     * being typed get the key as ever. */
    if (!mode_shift && code == KEY_RUN && sounds_file_playing()) {
        if (program_running()) {
            if (!mode_getkey)
                sounds_stop();
        } else if (!alpha_active() && !mode_command_entry && !flags.f.prgm_mode) {
            sounds_stop();
            return;
        }
    }

    /* ALPHA's keyboard modes (alphakeys.h); outside ALPHA, the key. */
    alpha_state_t state = alpha_state();
    alpha_key_t k = alpha_key(row, col, &state);
    if (k.kind == ALPHA_KEY_NEXT_MODE) {
        /* Remembered for the next ALPHA -- saved at OFF. */
        set_shift(false);
        px_settings.alpha_mode = (uint8_t) ((px_settings.alpha_mode + 1) % ALPHA_MODES);
        settings_save_later();
        refresh();
        return;
    }
    if (k.shift != (bool) mode_shift)
        set_shift(k.shift);
    int key = k.kind == ALPHA_KEY_CHAR ? 1024 + k.value : k.value;
    if (key == 0)
        return;

    held_key = key;
    pump_running(core_keydown(key, &enqueued, &repeat));
    held_enqueued = enqueued;
    held_repeat = repeat;

    /* A pick from a group's menu: the core stays in the group, for the
     * next letter -- back to the hidden first page instead. */
    if (k.to_page1 && alpha_active()) {
        if (mode_commandmenu >= MENU_ALPHA1 && mode_commandmenu <= MENU_ALPHA_MISC2)
            mode_commandmenu = MENU_ALPHA1;
        else
            set_menu(MENULEVEL_ALPHA, MENU_ALPHA1);
        redisplay();
    }

    refresh();
}

bool free42_key_is_enqueued(void) {
    return held_key != 0 && held_enqueued;
}

/* 0 = no auto-repeat, 1 = slow (SST/BST), 2 = fast (number/alpha entry),
 * 3 = extra slow (the equation editor's list) -- see core_keydown()'s
 * 'repeat' parameter in core_main.h. While this is nonzero the caller
 * drives free42_key_repeat_tick() instead of the timeout1/timeout2 hold
 * sequence, as the reference shells do. */
int free42_key_repeat_mode(void) {
    return held_key != 0 ? held_repeat : 0;
}

void free42_key_repeat_tick(void) {
    if (held_key == 0)
        return;

    held_repeat = core_repeat();
    refresh();
}

/* Key held for 1/4 second: the core shows its function name. */
void free42_key_timeout1(void) {
    if (held_key == 0 || held_enqueued)
        return;
    core_keytimeout1();
    refresh();
}

/* Key held for 2 seconds: the core shows NULL, and releasing it now
 * cancels the key. */
void free42_key_timeout2(void) {
    if (held_key == 0 || held_enqueued)
        return;
    core_keytimeout2();
    refresh();
}

void free42_key_up(void) {
    down_code = 0;
    if (held_key != 0 && !held_enqueued)
        pump_running(core_keyup());

    held_key = 0;
    held_enqueued = false;
    held_repeat = 0;

    if (sw_showing)
        sw_key_up();
    refresh();
}

/* SHIFT held down while an arrow goes down, in ALPHA: BST (UP) or SST
 * (DOWN), as on the DM42 -- SHIFT then an arrow switches the keyboard mode
 * there (free42_key_down()), and the core's ALPHA has no BST. Through the
 * commands themselves, so text entry ends first as with the core's SST. */
bool free42_shift_arrow(int code) {
    if (sw_showing || !alpha_active() || (code != KEY_UP && code != KEY_DOWN))
        return false;
    set_shift(false);
    bool enqueued;
    int repeat;
    pump_running(core_keydown(2048 + (code == KEY_UP ? CMD_BST : CMD_SST), &enqueued, &repeat));
    pump_running(core_keyup());
    refresh();
    return true;
}

char free42_alpha_char(uint8_t row, uint8_t col) {
    if (row >= KB_ROWS || col >= KB_COLS)
        return 0;
    return alpha_key_matrix[row][col];
}

/* ============================ states ============================ */

/* The same sequence as the reference shells use to save and switch
 * states: core_save_state() writes a complete .p42 file; loading tears the
 * core down and starts it again from the file. */
void free42_save_state(const char *path) {
    core_save_state(path);
}

void free42_load_state(const char *path) {
    held_key = 0;
    core_cleanup();
    core_start(path);
    pump_running(core_powercycle());
    refresh();
}

/* What a failed save of OFF_STATE_FILE leaves: the core writes to
 * "OFFSTATE.p42.<date><time>.crash" first and only renames it on success. */
static void remove_crash_files(void) {
    char names[8][40], path[48];
    int n = statefs_list_files("/", ".crash", (char *) names, sizeof(names[0]), 8);
    for (int i = 0; i < n; i++)
        if (strncmp(names[i], OFF_STATE_FILE + 1, strlen(OFF_STATE_FILE) - 1) == 0) {
            snprintf(path, sizeof(path), "/%s.crash", names[i]);
            statefs_remove(path);
        }
    statefs_sync();
}

bool free42_save_to_drive(void) {
    if (!statefs_ready())
        return false;
    /* The RAM copy only goes once the drive has one: a full drive fails the
     * save (the core leaves its half-written .crash file and no
     * OFF_STATE_FILE), the app isn't started, and the RAM copy is still
     * there for a crash before the next OFF. */
    core_save_state(OFF_STATE_FILE);
    if (!statefs_sync() || !statefs_file_exists(OFF_STATE_FILE)) {
        remove_crash_files();
        return false;
    }
    ramstate_discard();
    return true;
}

void free42_memory_to_ram(void) {
    core_save_state(RAMSTATE_PATH);
    if (ramstate_valid() && statefs_ready() && statefs_file_exists(OFF_STATE_FILE)) {
        statefs_remove(OFF_STATE_FILE);
        statefs_sync();
    }
}

/* ============================ programs ============================ */

/* Set by the core's shell_message(): a file it couldn't open, read or
 * write -- core_export_programs() and core_import_programs() say nothing
 * else about it. */
static bool core_complained = false;

void free42_core_message(const char *message) {
    printf("%s\n", message);
    core_complained = true;
}

/* How many programs core_list_programs() listed: its first 4 bytes, big-endian. */
static int listed_count(const char *buf) {
    const unsigned char *b = (const unsigned char *) buf;
    return buf == NULL ? 0 : (int) ((uint32_t) b[0] << 24 | (uint32_t) b[1] << 16 | (uint32_t) b[2] << 8 | b[3]);
}

static int program_count(void) {
    char *buf = core_list_programs();
    int count = listed_count(buf);
    free(buf);
    return count;
}

int free42_list_programs(char names[][PROGRAM_LABEL_MAX + 1], int max) {
    char *buf = core_list_programs();
    if (buf == NULL)
        return 0;
    int count = listed_count(buf);
    const char *p = buf + 4;
    int n = 0;
    for (; n < count && n < max; n++) {
        snprintf(names[n], PROGRAM_LABEL_MAX + 1, "%s", p);
        p += strlen(p) + 1;
    }
    free(buf);
    return n;
}

bool free42_export_programs(int index, const char *path) {
    int count = 1;
    int *indexes = &index;
    if (index < 0) {
        count = program_count();
        indexes = (int *) malloc(count * sizeof(int));
        if (count == 0 || indexes == NULL) {
            free(indexes);
            return false;
        }
        for (int i = 0; i < count; i++)
            indexes[i] = i;
    }
    core_complained = false;
    core_export_programs(count, indexes, path);
    if (indexes != &index)
        free(indexes);
    return !core_complained && statefs_file_exists(path);
}

/* Whether the last program, .END., is empty: an import fills it first. */
static bool end_empty(void) {
    return core_program_size(program_count() - 1) == 0;
}

/* The programs added: the import starts in .END. if it's empty, and the
 * file's last program becomes .END. -- so with .END. empty before and not
 * after, one more than the count went up by. */
int free42_import_programs(const char *path) {
    int count_before = program_count();
    bool empty_before = end_empty();
    core_complained = false;
    core_import_programs(0, path);
    if (core_complained)
        return -1;
    int added = program_count() - count_before + (empty_before && !end_empty() ? 1 : 0);
    screen_dirty = true;
    return added > 0 ? added : 0;
}

bool free42_auto_off_enabled(void) {
    return !shell_always_on(-1) && !sw_showing; /* the manual's page 267 */
}

void free42_set_printer(bool on) {
    /* Both flags, as PRON / PROFF: with 55 alone the keyboard's PRX printed,
     * but a program's print commands did nothing, without a word. */
    flags.f.printer_exists = on ? 1 : 0; /* flag 55 */
    flags.f.printer_enable = on ? 1 : 0; /* flag 21 */
}

bool free42_printer_on(void) {
    return flags.f.printer_exists;
}

void free42_set_quiet(bool quiet) {
    flags.f.audio_enable = quiet ? 0 : 1; /* flag 26, what QUIET toggles */
}

bool free42_quiet(void) {
    return !flags.f.audio_enable;
}

void free42_draw_screen(bool with_print_icon) {
    draw_status_bar(with_print_icon);
    draw_lcd();
    draw_shapes();
}

static bool print_stop_armed, print_stop_pressed, print_stop_program;

static bool print_stop_key_held() {
    for (int r = 0; r < KB_ROWS; r++)
        for (int c = 0; c < KB_COLS; c++)
            if ((key_matrix[r][c] == KEY_EXIT || key_matrix[r][c] == KEY_RUN) && keyboard_is_held((uint8_t) r, (uint8_t) c))
                return true;
    return false;
}

void free42_print_stop_begin(void) {
    print_stop_armed = false;
    print_stop_pressed = false;
}

bool free42_print_stop(void) {
    if (!print_stop_pressed) {
        bool held = print_stop_key_held();
        if (held && print_stop_armed)
            print_stop_pressed = true;
        else if (!held)
            print_stop_armed = true;
    }
    return print_stop_pressed;
}

#define PRINT_STOP_RELEASE_MS 5000 /* a stuck key mustn't hang the calculator */

void free42_print_stop_end(void) {
    if (!print_stop_pressed)
        return;
    for (int i = 0; i < PRINT_STOP_RELEASE_MS / 10 && print_stop_key_held(); i++) {
        watchdog_update();
        sleep_ms(10);
    }
    sleep_ms(20); /* its contacts' chatter isn't a new press */
    print_stop_program = program_running();
}

/* Sent to the panel at once: PRLCD holds core 1 until it has been sent, up to 2
 * minutes, with nothing else drawn. */
void free42_printing_show(void) {
    if (print_icon)
        return;
    print_icon = true;
    draw_status_bar();
    display_flush();
}

/* ============================ running ============================ */

#define RUN_REDRAW_US 250000 /* screen refresh while a program runs */

void free42_clock_start(void) {
    clock_showing = true;
    clock_next = get_absolute_time();
}

static void clock_tick(void) {
    if (!clock_showing || program_running() || !time_reached(clock_next))
        return;
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    char text[24];
    rtc_format_clock(text, sizeof(text), &now, px_settings.clock_with_date);
    draw_message(0, text, (int) strlen(text));
    refresh();
    clock_next = make_timeout_time_ms(px_settings.clock_with_date ? (uint32_t) (60000 - (now.sec * 1000 + now.ms))
                                                                  : (uint32_t) (1000 - now.ms));
}

/* ============================ alarms ============================ */

/* When each kind comes due goes by the manual's table (HP-41CX, volume 2,
 * appendix B): a message alarm sounds and shows itself, whatever is going on;
 * a control alarm runs its label, as a subroutine of a running program; a
 * conditional one runs it only if off or showing the clock. Running: two tones,
 * and past due. Otherwise it behaves as a message. Keys, settings and apps aren't
 * interrupted: this only runs from the main loop, between a program's slices. */

#define ALARM_TICK_MS      250
#define ALARM_FLASH_TICKS  4   /* a second of steady display first */
#define ALARM_TONES_TICKS  16  /* five flashes, then the tones: */
#define ALARM_END_TICKS    80  /* ... 16 pairs, one a second, then it's past due */
#define ALARM_RETRY_MS     250 /* one that has to wait for a moment it can run in */

static int alarm_ticks;
static absolute_time_t alarm_tick_at;
static bool alarm_reminded;
static bool alarm_own_sound; /* the active alarm started a file */
static bool almcat_draw_due;

static void alarm_tones() {
    if (flags.f.audio_enable) {
        sounds_play_tone(8, true);
        sounds_play_tone(9, false);
    }
}

/* Its own sound, or the tones. Also if its file has gone from the drive: an
 * alarm must not go silent for that. */
static void alarm_sound(const alarm_t *a) {
    if (!flags.f.audio_enable)
        return;
    char path[ALARM_SOUND_PATH_MAX];
    if (alarm_sound_path(a, path, sizeof(path))
        && (a->sound == ALARM_SOUND_WAV ? sounds_play_file(path, false) : sounds_midi_play(path) == 0)) {
        alarm_own_sound = true;
        return;
    }
    alarm_tones();
}

static int alarm_rows(const alarm_t *a) {
    return a->length == 0 ? 1 : (a->length + disp_c - 1) / disp_c;
}

/* A stopwatch counting up through zero: sounds and shows like a message alarm, but
 * isn't in the table and can't become past due (the manual's page 274). */
#define ALARM_TIMER ALARMS_MAX
static alarm_t timer_alarm;

static const alarm_t *active_alarm() {
    return alarm_active == ALARM_TIMER ? &timer_alarm : alarms_get(alarm_active);
}

static void alarm_draw(bool shown) {
    const alarm_t *a = active_alarm();
    if (!shown) {
        for (int r = 0; r < alarm_rows(a); r++)
            draw_message(r, "", 0, false);
    } else if (a->length == 0) { /* no message: the time and date */
        rtc_datetime_t now;
        rtc_get_datetime(&now);
        char text[24];
        rtc_format_clock(text, sizeof(text), &now, true);
        draw_message(0, text, (int) strlen(text), false);
    } else {
        draw_long_message(0, a->text, a->length, false);
    }
    refresh();
}

static void alarm_end(bool acknowledged) {
    if (alarm_own_sound)
        sounds_stop();
    if (alarm_active == ALARM_TIMER)
        ; /* nothing to keep */
    else if (acknowledged)
        alarms_done(alarm_active, rtc_now_ms());
    else
        alarms_mark_past_due(alarm_active);
    alarm_active = -1;
    clear_message();
    redisplay();
    refresh();
}

static void alarm_activate(int i) {
    alarm_active = i;
    alarm_ticks = 0;
    alarm_own_sound = false;
    alarm_tick_at = get_absolute_time();
    clock_showing = false;
}

static void alarm_tick() {
    if (!time_reached(alarm_tick_at))
        return;
    int tick = alarm_ticks++;
    alarm_tick_at = make_timeout_time_ms(ALARM_TICK_MS);
    /* A song longer than the alarm's 20 s plays on until it ends or a key. */
    bool playing = alarm_own_sound && sounds_file_playing();
    if (tick >= ALARM_END_TICKS && !playing) {
        alarm_end(false);
        return;
    }
    if (tick == 0)
        alarm_draw(true);
    else if (tick >= ALARM_FLASH_TICKS)
        alarm_draw(tick % 2 == 0);
    if (!playing && (tick == 0 || (tick >= ALARM_TONES_TICKS && tick % 4 == 0)))
        alarm_sound(active_alarm());
}

/* Its label, by name, as a key mapped to XEQ "NAME" does: nothing in the core
 * had to change for it. Empty: on from the current line. */
static void alarm_run_at_keyboard(const alarm_t *a) {
    int length;
    const char *label = alarm_label(a, &length);
    bool enqueued;
    int repeat;
    if (length == 0) {
        pump_running(core_keydown(KEY_RUN, &enqueued, &repeat));
    } else {
        char name[8];
        memcpy(name, label, (size_t) length);
        name[length] = 0;
        pump_running(core_keydown_command(name, 1, &enqueued, &repeat));
    }
    pump_running(core_keyup());
}

static void alarm_run_as_subroutine(const alarm_t *a) {
    int length;
    const char *label = alarm_label(a, &length);
    if (length == 0)
        return; /* the current line: it's on its way there already */
    arg_struct target;
    target.type = ARGTYPE_STR;
    target.length = (unsigned short) length;
    memcpy(target.val.text, label, (size_t) length);
    int error = docmd_xeq(&target);
    if (error != ERR_NONE)
        display_error(error);
}

static void alarm_service() {
    if (!alarm_reminded) {
        alarm_reminded = true;
        if (!alarm_woke && alarms_any_past_due())
            alarm_tones(); /* only a reminder, as on the HP-41 */
    }
    if (alarm_active >= 0) {
        alarm_tick();
        return;
    }
    if (almcat_at >= 0 || sw_showing || held_key != 0)
        return; /* alarms wait for the stopwatch's keyboard, as on the HP-41 */

    uint64_t now = rtc_now_ms();
    uint64_t timer = stopwatch_timer_at();
    if (timer != 0 && now >= timer) {
        stopwatch_timer_done(now);
        memset(&timer_alarm, 0, sizeof(timer_alarm));
        timer_alarm.length = 11;
        memcpy(timer_alarm.text, "TIMER ALARM", 11);
        alarm_activate(ALARM_TIMER);
        return;
    }
    int due = alarms_due(now);
    if (due < 0) {
        if (alarm_woke && !program_running() && !run_pending) {
            alarm_woke = false;
            free42_request_powerdown();
        }
        return;
    }

    const alarm_t *a = alarms_get(due);
    alarm_kind_t kind = alarm_kind(a);
    if (kind == ALARM_MESSAGE) {
        alarm_activate(due);
    } else if (program_running()) {
        if (kind == ALARM_CONDITIONAL) {
            alarm_sound(a);
            alarms_mark_past_due(due);
        } else if (!mode_getkey && !mode_pause && mode_interruptible == NULL) {
            alarm_run_as_subroutine(a);
            alarms_done(due, now);
            refresh();
        }
    } else if (kind == ALARM_CONDITIONAL && !alarm_woke && !clock_showing) {
        alarm_activate(due);
    } else if (!flags.f.prgm_mode && !alpha_active() && !mode_command_entry && !eqn_active()
               && mode_interruptible == NULL) {
        alarm_t copy = *a;
        clock_showing = false;
        alarms_done(due, now);
        alarm_run_at_keyboard(&copy);
        refresh();
    }
}

/* An alarm's or the stopwatch's timer: what wakes the calculator from OFF too. */
static uint64_t next_alarm_ms() {
    uint64_t alarm = alarms_next_at(), timer = stopwatch_timer_at();
    return alarm == 0 ? timer : timer == 0 || alarm < timer ? alarm : timer;
}

static bool alarm_next_timeout(absolute_time_t *at) {
    if (alarm_active >= 0) {
        *at = alarm_tick_at;
        return true;
    }
    uint64_t next = next_alarm_ms();
    if (next == 0)
        return false;
    uint64_t now = rtc_now_ms();
    uint64_t wait = next > now ? next - now : ALARM_RETRY_MS;
    *at = make_timeout_time_ms(wait > 3600000 ? 3600000 : (uint32_t) wait);
    return true;
}

/* ALMCAT: the list on the top two lines, ↑ and ↓ to move, ← deletes, EXIT
 * leaves; any other key leaves and does its job. The drawing waits for the core
 * to have finished the command. */
void free42_almcat_start(void) {
    if (alarms_count() == 0) {
        draw_message(0, "No Alarms", 9, false);
        return;
    }
    almcat_at = 0;
    almcat_draw_due = true;
}

static void almcat_draw() {
    const alarm_t *a = alarms_get(almcat_at);
    rtc_datetime_t when;
    time_ms_to_datetime(a->at_ms, &when);
    char date[16], time[12], line[40];
    rtc_format_date(date, sizeof(date), &when);
    rtc_format_time(time, sizeof(time), &when, true);
    int n = snprintf(line, sizeof(line), "%d%s %s %s", almcat_at + 1, a->past_due ? "!" : "", date, time);
    draw_message(0, line, n > disp_c ? disp_c : n, false);
    draw_message(1, a->text, a->length > disp_c ? disp_c : a->length, false);
    refresh();
}

static bool almcat_key(int code) {
    if (code == KEY_UP || code == KEY_DOWN) {
        int count = alarms_count();
        almcat_at = (almcat_at + (code == KEY_UP ? count - 1 : 1)) % count;
        almcat_draw();
        return true;
    }
    if (code == KEY_BSP) {
        alarms_remove(almcat_at);
        if (alarms_count() > 0) {
            if (almcat_at >= alarms_count())
                almcat_at = alarms_count() - 1;
            almcat_draw();
            return true;
        }
    }
    almcat_at = -1;
    clear_message();
    redisplay();
    refresh();
    return code == KEY_EXIT || code == KEY_BSP;
}

/* ============================ stopwatch ============================ */

/* The rules are stopwatch.c's. What's here is the PX's: the keys, the top line,
 * and a redraw 10 times a second while it runs, as the HP-41's tenths change. */

#define SW_REDRAW_MS 100

static bool sw_draw_due;
static int sw_error;

void free42_stopwatch_open(int store, int recall, bool delta) {
    stopwatch_keyboard_open(store, recall, delta);
    sw_showing = true;
    sw_draw_due = true;
    sw_error = ERR_NONE;
    clock_showing = false;
}

bool free42_stopwatch_showing(void) {
    return sw_showing;
}

void free42_stopwatch_close(void) {
    sw_showing = false;
    sw_draw_due = false;
    set_shift(false);
    clear_message();
}

int free42_stopwatch_error(void) {
    return sw_error;
}

static void sw_draw() {
    char text[STOPWATCH_TEXT_MAX];
    stopwatch_text(text, sizeof(text), rtc_now_ms(), px_stopwatch_registers());
    draw_message(0, text, (int) strlen(text), false);
    refresh();
    sw_next = make_timeout_time_ms(SW_REDRAW_MS);
}

static void sw_result(int result) {
    if ((result & STOPWATCH_BEEP) && flags.f.audio_enable)
        sounds_play_tone(8, false);
    if (result & STOPWATCH_LEAVE) {
        sw_error = (result & STOPWATCH_NONEXISTENT) ? ERR_NONEXISTENT : ERR_NONE;
        free42_stopwatch_close();
        refresh(); /* free42_service() has the core go on */
    } else {
        sw_draw();
    }
}

/* Where the HP-41's overlay has them, which is the HP-42S's ENTER row with +/-
 * and E for its CHS and EEX. SHIFT ← is EXIT, as there; the PX has EXIT too. */
static stopwatch_key_t sw_key_of(int code, bool shift) {
    switch (code) {
        case KEY_RUN: return STOPWATCH_KEY_RUN;
        case KEY_BSP: return STOPWATCH_KEY_CLEAR;
        case KEY_ENTER: return STOPWATCH_KEY_SPLIT;
        case KEY_RCL: return STOPWATCH_KEY_RECALL;
        case KEY_CHS: return STOPWATCH_KEY_DELTA;
        case KEY_DOWN: return shift ? STOPWATCH_KEY_BST : STOPWATCH_KEY_SST;
        case KEY_UP: return STOPWATCH_KEY_BST;
        case KEY_E: return shift ? STOPWATCH_KEY_RNNN : STOPWATCH_KEY_REG;
        case KEY_0: return STOPWATCH_KEY_0;
        case KEY_1: case KEY_2: case KEY_3:
            return (stopwatch_key_t) (STOPWATCH_KEY_0 + 1 + code - KEY_1);
        case KEY_4: case KEY_5: case KEY_6:
            return (stopwatch_key_t) (STOPWATCH_KEY_0 + 4 + code - KEY_4);
        case KEY_7: case KEY_8: case KEY_9:
            return (stopwatch_key_t) (STOPWATCH_KEY_0 + 7 + code - KEY_7);
        default: return STOPWATCH_KEY_OTHER;
    }
}

static void sw_key_down(int code) {
    if (code == KEY_SHIFT) {
        set_shift(!mode_shift);
        refresh();
        return;
    }
    bool shift = mode_shift;
    set_shift(false);
    if (code == KEY_EXIT || (code == KEY_BSP && shift))
        sw_result(STOPWATCH_LEAVE);
    else
        sw_result(stopwatch_key_down(sw_key_of(code, shift), rtc_now_ms(), px_stopwatch_registers()));
}

static void sw_key_up(void) {
    sw_result(stopwatch_key_up(px_stopwatch_registers()));
}

/* The keyboard being on, a timer reaching zero is only the two tones. */
static void sw_tick() {
    uint64_t now = rtc_now_ms();
    uint64_t timer = stopwatch_timer_at();
    if (timer != 0 && now >= timer) {
        stopwatch_timer_done(now);
        alarm_tones();
        sw_draw_due = true;
    }
    if (sw_draw_due || (stopwatch_ticking() && time_reached(sw_next))) {
        sw_draw_due = false;
        sw_draw();
    }
}

bool free42_service(void) {
    static uint64_t last_draw_us;

    /* Still going: a listing, a line just sent, or a program held up for room in
     * the printer's buffer to print its next. */
    if (print_icon && !ann_prt && !printer_busy() && !(run_pending && !printer_ready())) {
        print_icon = false;
        refresh();
    }
    if (print_stop_program) {
        print_stop_program = false;
        bool enqueued;
        int repeat;
        pump_running(core_keydown(KEY_EXIT, &enqueued, &repeat));
        pump_running(core_keyup());
        refresh();
    }
    clock_tick();
    alarm_service();
    if (alarm_active >= 0)
        return false; /* a running program waits for it */
    if (sw_showing) {
        sw_tick();
        return false; /* and the core for this, until EXIT (px_commands.cc) */
    }
    if (almcat_draw_due && !program_running()) {
        almcat_draw_due = false;
        almcat_draw();
    }

    if (timeout3_armed && time_reached(timeout3_at)) {
        timeout3_armed = false;
        if (core_timeout3(true)) /* the PSE ended: carry on running */
            run_pending = true;
        refresh();
    }

    if (!run_pending)
        return false;

    /* A print waiting for the printer's buffer waits here, in the main
     * loop's time: inside shell_print() the keys were only read for an
     * instant between lines, 1.8 s apart. A PRP looked like a hang, and a
     * tap of EXIT rarely stopped it. */
    if (!printer_ready())
        return true;

    slice_start_us = time_us_64();
    bool enqueued;
    int repeat;
    run_pending = core_keydown(0, &enqueued, &repeat);

    uint64_t now = time_us_64();
    if (!run_pending || (screen_dirty && now - last_draw_us >= RUN_REDRAW_US)) {
        last_draw_us = now;
        refresh(); /* progress while running, and the result at the end */
    }
    return run_pending;
}

void free42_redisplay(void) {
    refresh();
}

/* ============================ OFF ============================ */

/* OFF (the OFF command, the settings menu, holding EXIT) ends up in
 * shell_powerdown(), which only records the request: it's called from
 * deep inside core_keyup(), so the actual sleep happens later, from the
 * main loop, via free42_sleep(). */
static bool powerdown_requested = false;

void free42_request_powerdown(void) {
    powerdown_requested = true;
}

bool free42_powerdown_requested(void) {
    return powerdown_requested;
}

void free42_sleep(bool quiet) {
    powerdown_requested = false;
    if (alarm_active >= 0)
        alarm_end(false); /* holding ON through one: past due, as it would become */
    held_key = 0;
    held_enqueued = false;
    held_repeat = 0;

    if (px_settings.off_sound && !quiet)
        sounds_play_off(); /* the screen still showing what it did */

    /* The ON key wakes the calculator, same as the real HP-42S -- it's the
     * EXIT key's matrix position. */
    uint8_t on_row = 0, on_col = 0;
    for (int r = 0; r < KB_ROWS; r++)
        for (int c = 0; c < KB_COLS; c++)
            if (key_matrix[r][c] == KEY_EXIT) {
                on_row = (uint8_t) r;
                on_col = (uint8_t) c;
            }

    /* The deep power-off keeps only SRAM bank 1, and ON reboots: the
     * calculator's memory goes into the RAM buffer there, and free42_init()
     * picks it up again. A memory too big for the buffer goes to the drive
     * instead. */
    core_save_state(RAMSTATE_PATH);
    if (!ramstate_valid()) {
        core_save_state(OFF_STATE_FILE);
        statefs_sync();
    } else if (statefs_ready() && statefs_file_exists(OFF_STATE_FILE)) {
        statefs_remove(OFF_STATE_FILE); /* from when the memory was bigger: stale now */
        statefs_sync();
    }
    settings_save_pending(); /* DMY, CLK12, a time set exactly... since the last save */
    alarms_save();
    stopwatch_save();
    /* Each flash write ends by turning the 100ms watchdog back on, and the
     * splash below waits longer than that: off again (the caller had
     * stopped it; power_deep_off() starts its own). */
    watchdog_pause();

    /* With a Splash Time set the panel keeps showing the splash while off,
     * for that long -- the chip wakes briefly each minute to update its
     * clock (power.h); otherwise it's blanked. */
    uint64_t splash_until_ms = 0;
    if (px_settings.splash_minutes != 0) {
        splash_show_static();
        splash_until_ms = powman_timer_get_ms() + (uint64_t) px_settings.splash_minutes * 60000;
    }
    power_deep_off(on_row, on_col, splash_until_ms, next_alarm_ms());
}
