#include <stdio.h>
#include <string.h>

#include "free42_app.h"
#include "core_main.h"
#include "core_globals.h"
#include "core_helpers.h"
#include "core_tables.h"
#include "core_variables.h"
#include "display.h"
#include "keyboard.h"
#include "font_10x16.h"
#include "font_8x12.h"
#include "rtc.h"
#include "shell.h"

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

/* LCD row assignment for the Alpha register and the 4-level stack, in
 * pixels -- font_10x16 is 16px tall, with a requested 4px gap between
 * lines, so each line is ROW_STRIDE (20px) below the last, starting at
 * ALPHA_ROW. A dotted separator is drawn centered in each 4px gap (see
 * display_stack()), hence ROW_GAP / 2 below. */
#define ALPHA_ROW    13
#define ROW_GAP      4
#define ROW_STRIDE   (16 + ROW_GAP) /* font_10x16 height + gap */
#define STACK_ROW_T  (ALPHA_ROW + ROW_STRIDE)
#define STACK_ROW_Z  (STACK_ROW_T + ROW_STRIDE)
#define STACK_ROW_Y  (STACK_ROW_Z + ROW_STRIDE)
#define STACK_ROW_X  (STACK_ROW_Y + ROW_STRIDE)

/* Starting pixel column for every text row (leaves a 4px left margin). */
#define STACK_COL_START 4

/* Last error/status message text from shell_status_message() (shell.cc),
 * shown on the Y row in place of Y's value for as long as the core
 * considers it active (flags.f.message). 22 bytes matches Free42's own
 * lasterr_text[22] -- the core never hands us anything longer. */
static char status_message[23];
static int status_message_length = 0;

void free42_set_status_message(const char *text, int length) {
    if (length > (int) sizeof(status_message) - 1)
        length = sizeof(status_message) - 1;
    memcpy(status_message, text, length);
    status_message_length = length;
}

/* Transient override for the Y row while a key is held down, matching the
 * real HP-42S/Free42 "hold to confirm" behavior: hold a key for 1/4 second
 * and its function name appears (see free42_key_timeout1()); keep holding
 * to 2 seconds and it switches to "NULL", meaning releasing now cancels the
 * key instead of performing it (free42_key_timeout2()). Cleared on release
 * (free42_key_up()). Takes priority over status_message -- it's describing
 * the key currently under your finger, not a past error. */
static bool key_hint_active = false;
static char key_hint_text[12];
static int key_hint_length = 0;

/* Both fonts are 10px wide with an 11px advance per character (10px
 * glyph + 1px gap), giving (250 - STACK_COL_START) / 11 = ~22 characters
 * per line, matching Free42's own native 22-character display width. */
#define LINE_CHARS 22

/* Draws label+value on one row, truncating from the front if the
 * combined text doesn't fit -- same convention Free42's own core uses
 * (confirmed directly in the "draw_number:" path, core_keydown.cc:718:
 * draw_char(0, cmdline_row, 26)): drop everything but the last
 * LINE_CHARS-1 characters and prefix them with a single ellipsis glyph
 * (byte 26, drawn directly as font entry 26) in place of the label.
 * Without this, a value long enough to overflow the line just silently
 * clips at the right edge instead of showing Free42's actual "..."
 * tail-scroll behavior. */
static void display_line(const char *label, const char *value, uint8_t y) {
    char full[64];
    int llen = (int) strlen(label);
    int vlen = (int) strlen(value);
    int total = llen + vlen;
    if (total > (int) sizeof(full) - 1)
        total = (int) sizeof(full) - 1;
    memcpy(full, label, llen);
    memcpy(full + llen, value, (size_t) (total - llen));
    full[total] = 0;

    /* Free42 uses raw byte 24 as its internal exponent marker -- both in
     * the number-entry buffer (cmdline) and in vartype2string()'s own
     * formatted scientific-notation output (core_helpers.cc's
     * phloat2string() explicitly treats 'E'/'e'/24 as equivalent). Now that
     * font_10x16 covers the full 0-127 range, byte 24 draws directly as
     * font entry 24 -- whatever glyph lives there (a dedicated exponent
     * "E", distinct from a plain letter 'E') -- no translation needed. */

    if (total <= LINE_CHARS) {
        display_draw_string(&font_10x16, full, STACK_COL_START, y, 0x00);
    } else {
        char buf[LINE_CHARS + 1];
        int tail_len = LINE_CHARS - 1;
        buf[0] = 26;
        memcpy(buf + 1, full + (total - tail_len), tail_len);
        buf[LINE_CHARS] = 0;
        display_draw_string(&font_10x16, buf, STACK_COL_START, y, 0x00);
    }
}

static void display_reg(const char *label, int index, uint8_t y) {
    char buf[48];
    /* Default 12 mantissa digits (same as Free42's own vartype2string()
     * default), worst case ~19 chars with sign/decimal/exponent --
     * display_line() truncates from the front if it ever doesn't fit. */
    int len = vartype2string(stack[index], buf, sizeof(buf) - 1);
    buf[len] = 0;
    display_line(label, buf, y);
}

/* Alpha register (reg_alpha/reg_alpha_length, core_globals.h) -- unlike
 * the stack registers, this is raw text, not a value to format. Not
 * null-terminated in the core's own representation, so copy + terminate
 * before handing it to display_line(). */
static void display_alpha(uint8_t y) {
    char buf[sizeof(reg_alpha) + 1];
    int len = reg_alpha_length;
    if (len > (int) sizeof(reg_alpha))
        len = (int) sizeof(reg_alpha);
    memcpy(buf, reg_alpha, (size_t) len);
    buf[len] = 0;
    display_line("A:", buf, y);
}

/* While typing a number, Free42's own core never updates X (stack[sp])
 * with each keystroke -- it stays at whatever value was there before
 * entry started. The live digits being typed live in a separate buffer,
 * cmdline/cmdline_length (see keydown_number_entry()'s "draw_number:"
 * path in core_keydown.cc, which builds its own string from cmdline
 * instead of calling display_x()/vartype2string()). We do the same here:
 * show the raw entry buffer, not the stale X register, whenever
 * mode_number_entry is set. */
static void display_entry(uint8_t y) {
    char buf[64];
    int n = 0;
    for (int i = 0; i < cmdline_length && n < (int) sizeof(buf) - 2; i++)
        buf[n++] = cmdline[i]; /* display_line() translates the exponent marker (24) */
    buf[n++] = '_'; /* entry cursor, matching the real HP-42S/Free42 display */
    buf[n] = 0;
    display_line("X:", buf, y);
}

static void append_buf(char *buf, int bufsize, int *n, const char *s, int len) {
    if (*n + len > bufsize)
        len = bufsize - *n;
    if (len > 0) {
        memcpy(buf + *n, s, len);
        *n += len;
    }
}

/* While mode_command_entry is set (e.g. right after FIX, SF, STO, GTO...
 * -- a command waiting for a numeric or alpha argument), replaces the X
 * row with a prompt: the command name, then either a quoted alpha string
 * in progress or one underscore per remaining digit -- e.g. "FIX_" before
 * typing, "FIX3" once a digit's in. Mirrors Free42's own
 * display_incomplete_command() (core_display.cc) case by case, reading
 * the same incomplete_* globals (core_globals.h) directly -- this port
 * doesn't call that function itself, since it draws through the internal
 * single-line pixel renderer this port has never used elsewhere. Bytes in
 * cmd->name with the high bit packed on (see the ENTER key-hint fix) are
 * unmasked (&127) rather than dropped, matching how
 * display_incomplete_command() actually draws them -- via
 * draw_string()->draw_char()->get_char(), whose "&127 undefined_char()"
 * handling is what the ENTER fix was modeled on -- not draw_key()'s
 * separate hide_meta path the menu-footer labels use. */
static void display_incomplete(uint8_t y) {
    char buf[40];
    int n = 0;

    if (incomplete_command == CMD_ASSIGNb) {
        append_buf(buf, sizeof(buf), &n, "ASSIGN \"", 8);
        append_buf(buf, sizeof(buf), &n, pending_command_arg.val.text, pending_command_arg.length);
        append_buf(buf, sizeof(buf), &n, "\" TO _", 6);
    } else if (incomplete_argtype == ARG_MKEY) {
        append_buf(buf, sizeof(buf), &n, "KEY _", 5);
    } else {
        if (incomplete_command == CMD_SIMQ) {
            append_buf(buf, sizeof(buf), &n, "Number of Unknowns ", 19);
        } else {
            const command_spec *cmd = &cmd_array[incomplete_command];
            char name[24];
            int name_len = 0;
            for (int i = 0; i < cmd->name_length && name_len < (int) sizeof(name); i++) {
                unsigned char c = (unsigned char) cmd->name[i];
                if (undefined_char(c))
                    c &= 127;
                name[name_len++] = (char) c;
            }
            append_buf(buf, sizeof(buf), &n, name, name_len);
            append_buf(buf, sizeof(buf), &n, " ", 1);
        }

        if (incomplete_ind)
            append_buf(buf, sizeof(buf), &n, "IND ", 4);

        if (incomplete_alpha) {
            append_buf(buf, sizeof(buf), &n, "\"", 1);
            append_buf(buf, sizeof(buf), &n, incomplete_str, incomplete_length);
            append_buf(buf, sizeof(buf), &n, "_", 1);
        } else {
            int d = 1;
            for (int i = 0; i < incomplete_length - 1; i++)
                d *= 10;
            for (int i = 0; i < incomplete_maxdigits && n < (int) sizeof(buf); i++) {
                if (i < incomplete_length) {
                    char digit = (char) ('0' + (incomplete_num / d) % 10);
                    append_buf(buf, sizeof(buf), &n, &digit, 1);
                    d /= 10;
                } else {
                    append_buf(buf, sizeof(buf), &n, "_", 1);
                }
            }
        }
    }

    if (n > (int) sizeof(buf) - 1)
        n = (int) sizeof(buf) - 1;
    buf[n] = 0;
    display_line("", buf, y); /* same front-truncate-with-ellipsis convention as display_line()'s other callers */
}

/* Status header: date, time, angle mode, flags 0-4, shift indicator, and
 * battery, left to right, in the top HEADER_HEIGHT rows -- solid black
 * background with inverted (white-on-black) text, using font_8x12 (12px
 * tall, an exact fit for the header height, so glyphs run edge to edge
 * with no vertical margin). Each field gets a fixed starting column
 * (not chained off the previous field's width) so a shorter/longer value
 * -- GRAD vs DEG, say -- never shifts anything after it. A set flag (0-4)
 * gets an extra highlight on top of the normal styling: a small white
 * patch behind its digit, drawn non-inverted so the digit reads as
 * ordinary black-on-white, standing out against the rest of the
 * white-on-black header. */
#define HEADER_HEIGHT        12
#define HEADER_DATE_X        2
#define HEADER_TIME_X        47
#define HEADER_ANGLE_X       92
#define HEADER_FLAGS_X       129
#define HEADER_SHIFT_X       174
/* font_8x12's advance == width (no gap baked in -- see font.h), so 3
 * glyphs occupy exactly 3*8px with nothing between them. */
#define HEADER_BATTERY_WIDTH (3 * 8)
#define HEADER_BATTERY_X     (DISPLAY_WIDTH - HEADER_BATTERY_WIDTH - 1) /* 1px margin from the right edge */

/* Battery icon glyphs, from the font_12x8.png sheet's raw byte codes
 * (16-22): 16/17/18 are the left cap with 0/1/2 bars filled, 19/20/21 are
 * the right cap with 0/1/2 bars filled, 22 is the terminal nub -- three
 * glyphs concatenated give a 5-step battery gauge. No battery-sense
 * hardware is wired up yet (see the "skip battery for now" decision when
 * the header was first added), so this always draws the 100% icon. */
static const char battery_100[] = {18, 21, 22, 0};

static void display_header() {
    display_fill_rect(0, 0, DISPLAY_WIDTH - 1, HEADER_HEIGHT - 1, true);

    uint4 date;
    shell_get_time_date(NULL, &date, NULL);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d/%02d", (int) (date / 100 % 100), (int) (date % 100));
    display_draw_string(&font_8x12, buf, HEADER_DATE_X, 0, 0xFF);

    int hour, min, sec;
    rtc_get_hms(&hour, &min, &sec);
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, min);
    display_draw_string(&font_8x12, buf, HEADER_TIME_X, 0, 0xFF);

    const char *angle = flags.f.rad ? "RAD" : flags.f.grad ? "GRAD" : "DEG";
    display_draw_string(&font_8x12, angle, HEADER_ANGLE_X, 0, 0xFF);

    uint8_t x = HEADER_FLAGS_X;
    for (int i = 0; i < 5; i++) {
        char digit = (char) ('0' + i);
        if (flags.farray[i]) {
            display_fill_rect(x, 0, (uint8_t) (x + font_8x12.width - 1), HEADER_HEIGHT - 1, false);
            x = display_draw_char(&font_8x12, digit, x, 0, 0x00);
        } else {
            x = display_draw_char(&font_8x12, digit, x, 0, 0xFF);
        }
    }

    if (mode_shift)
        display_draw_string(&font_8x12, "^", HEADER_SHIFT_X, 0, 0xFF);

    display_draw_string(&font_8x12, battery_100, HEADER_BATTERY_X, 0, 0xFF);
}

/* Footer: 6 menu-key "tabs" in the bottom FOOTER_HEIGHT rows, replicating
 * the subset of Free42's own redisplay() (core_display.cc) needed to read
 * the current menu's 6 labels without going through its internal pixel
 * renderer (draw_key()/the single-line display[] buffer), which this
 * port has never used anywhere else. Covers the two most common menu
 * sources: general app menus (the `menus[]` table -- MODES, DISP, CLEAR,
 * and similar) and VARMENU (the variables menu). Not covered: CATALOG,
 * CUSTOM1-3, and PROGRAMMABLE menus, whose label text lives in variables
 * private (static) to core_display.cc -- the footer just stays blank for
 * those rather than showing something wrong. Also not covered: the
 * CMD_BINM/OCTM/DECM/HEXM highlight cases (need get_base(), not exported)
 * and shifted alternate labels on a handful of specific top-level menus
 * (MENU_TOP_FCN and friends) -- both narrow cosmetic gaps; the physical
 * keys underneath still do the right thing regardless of what the label
 * says. */
#define FOOTER_Y0     108
#define FOOTER_HEIGHT 14
#define FOOTER_TABS   6

static int current_menu_id() {
    if (mode_commandmenu != MENU_NONE) return mode_commandmenu;
    if (mode_alphamenu != MENU_NONE) return mode_alphamenu;
    if (mode_transientmenu != MENU_NONE) return mode_transientmenu;
    if (mode_plainmenu != MENU_NONE) return mode_plainmenu;
    if (mode_appmenu != MENU_NONE) return mode_appmenu;
    return MENU_NONE;
}

/* Matches draw_key()'s hide_meta=1 behavior for cmd_array-sourced labels:
 * some command names pack a letter as ASCII|0x80 for their full
 * spelled-out form (see the ENTER key-hint fix, free42_key_timeout1()),
 * but a menu key label is meant to be a compact abbreviation -- upstream
 * drops those bytes entirely here rather than unmasking them. */
static int copy_menu_label(char *dst, int dst_size, const char *src, int src_len) {
    int n = 0;
    for (int i = 0; i < src_len && n < dst_size; i++) {
        unsigned char c = (unsigned char) src[i];
        if (undefined_char(c))
            continue;
        dst[n++] = (char) c;
    }
    return n;
}

/* The is_flag switch from redisplay()'s general-menu path, copied
 * verbatim case by case (same command IDs, same flag/mode reads -- all
 * of these globals are already extern in core_globals.h). Determines
 * whether a menu item represents the calculator's current setting (e.g.
 * RAD is highlighted while flags.f.rad is set) so it can be drawn as the
 * "active" tab. */
static bool menu_item_is_flag(int cmd_id) {
    switch (cmd_id) {
        case CMD_FIX: return flags.f.fix_or_all && !flags.f.eng_or_all;
        case CMD_SCI: return !flags.f.fix_or_all && !flags.f.eng_or_all;
        case CMD_ENG: return !flags.f.fix_or_all && flags.f.eng_or_all;
        case CMD_ALL: return flags.f.fix_or_all && flags.f.eng_or_all;
        case CMD_RDXDOT: return flags.f.decimal_point;
        case CMD_RDXCOMMA: return !flags.f.decimal_point;
        case CMD_DEG: return !flags.f.rad && !flags.f.grad;
        case CMD_RAD: return flags.f.rad;
        case CMD_GRAD: return !flags.f.rad && flags.f.grad;
        case CMD_POLAR: return flags.f.polar;
        case CMD_RECT: return !flags.f.polar;
        case CMD_QUIET: return !flags.f.audio_enable;
        case CMD_CPXRES: return !flags.f.real_result_only;
        case CMD_REALRES: return flags.f.real_result_only;
        case CMD_KEYASN: return !flags.f.local_label;
        case CMD_LCLBL: return flags.f.local_label;
        case CMD_BSIGNED: return flags.f.base_signed;
        case CMD_BWRAP: return flags.f.base_wrap;
        case CMD_DECINT: return mode_dec_int;
        case CMD_BINSEP: return mode_bin_sep;
        case CMD_OCTSEP: return mode_oct_sep;
        case CMD_DECSEP: return mode_dec_sep;
        case CMD_HEXSEP: return mode_hex_sep;
        case CMD_MDY: return !flags.f.ymd && !flags.f.dmy;
        case CMD_DMY: return !flags.f.ymd && flags.f.dmy;
        case CMD_YMD: return flags.f.ymd;
        case CMD_CLK12: return !mode_time_clk24;
        case CMD_CLK24: return mode_time_clk24;
        case CMD_4STK: return !flags.f.big_stack;
        case CMD_NSTK: return flags.f.big_stack;
        case CMD_CAPS: return mode_menu_caps;
        case CMD_MIXED: return !mode_menu_caps;
        case CMD_STATIC: return mode_menu_static;
        case CMD_DYNAMIC: return !mode_menu_static;
        case CMD_PON: return flags.f.printer_exists;
        case CMD_POFF: return !flags.f.printer_exists;
        case CMD_MAN: return !flags.f.trace_print && !flags.f.normal_print;
        case CMD_NORM: return !flags.f.trace_print && flags.f.normal_print;
        case CMD_TRACE: return flags.f.trace_print && !flags.f.normal_print;
        case CMD_STRACE: return flags.f.trace_print && flags.f.normal_print;
        case CMD_ALLSIGMA: return flags.f.all_sigma;
        case CMD_LINSIGMA: return !flags.f.all_sigma;
        case CMD_LINF: return flags.f.lin_fit;
        case CMD_LOGF: return flags.f.log_fit;
        case CMD_EXPF: return flags.f.exp_fit;
        case CMD_PWRF: return flags.f.pwr_fit;
        case CMD_WRAP: return !flags.f.grow;
        case CMD_GROW: return flags.f.grow;
        default: return false; /* includes CMD_BINM/OCTM/DECM/HEXM -- see comment above */
    }
}

/* Draws tab i (0-5) of FOOTER_TABS equal-width segments: a filled
 * rectangle (white if highlighted, black otherwise) with its label
 * centered and drawn in the opposite tone, truncated to whatever fits
 * the tab's width. A 1px gap separates adjacent tabs (except after the
 * last one, which runs to the display's right edge) so they read as
 * distinct tabs rather than one continuous bar. */
static void draw_footer_tab(int i, const char *text, int text_len, bool highlight) {
    int x0 = (i * DISPLAY_WIDTH) / FOOTER_TABS;
    int x1 = ((i + 1) * DISPLAY_WIDTH) / FOOTER_TABS - 1;
    int fill_x1 = (i < FOOTER_TABS - 1) ? x1 - 1 : x1;

    display_fill_rect((uint8_t) x0, FOOTER_Y0, (uint8_t) fill_x1, FOOTER_Y0 + FOOTER_HEIGHT - 1, !highlight);

    int max_chars = (fill_x1 - x0 + 1) / font_8x12.advance;
    if (text_len > max_chars)
        text_len = max_chars;
    int text_width = text_len * font_8x12.advance;
    int tx = x0 + ((fill_x1 - x0 + 1) - text_width) / 2;
    int ty = FOOTER_Y0 + (FOOTER_HEIGHT - font_8x12.height) / 2;

    uint8_t color = highlight ? 0x00 : 0xFF;
    for (int k = 0; k < text_len; k++)
        tx = display_draw_char(&font_8x12, text[k], (uint8_t) tx, (uint8_t) ty, color);
}

static void display_footer() {
    int menu_id = current_menu_id();
    if (menu_id == MENU_NONE)
        return;

    if (menu_id == MENU_VARMENU) {
        if (varmenu_length == 0)
            return;
        for (int i = 0; i < 6; i++)
            draw_footer_tab(i, varmenu_labeltext[i], varmenu_labellength[i], false);
        return;
    }

    if (menu_id == MENU_CATALOG || menu_id == MENU_CUSTOM1 || menu_id == MENU_CUSTOM2
            || menu_id == MENU_CUSTOM3 || menu_id == MENU_PROGRAMMABLE)
        return; /* not covered yet -- see the comment above display_footer() */

    const menu_spec *m = menus + menu_id;
    for (int i = 0; i < 6; i++) {
        const menu_item_spec *mi = m->child + i;
        char buf[8];
        int len;
        bool highlight = false;
        if (mi->menuid == MENU_NONE || (mi->menuid & 0x3000) == 0) {
            len = copy_menu_label(buf, sizeof(buf), mi->title, mi->title_length);
        } else {
            int cmd_id = mi->menuid & 0xfff;
            const command_spec *cmd = &cmd_array[cmd_id];
            if ((mi->menuid & 0x2000) != 0)
                highlight = menu_item_is_flag(cmd_id);
            len = copy_menu_label(buf, sizeof(buf), cmd->name, cmd->name_length);
        }
        draw_footer_tab(i, buf, len, highlight);
    }
}

static void display_stack() {
    display_clear();

    display_header();
    display_footer();
    display_alpha(ALPHA_ROW);
    display_draw_dotted_hline(0, DISPLAY_WIDTH - 1, STACK_ROW_T - ROW_GAP / 2);

    display_reg("T:", REG_T, STACK_ROW_T);
    display_draw_dotted_hline(0, DISPLAY_WIDTH - 1, STACK_ROW_Z - ROW_GAP / 2);

    display_reg("Z:", REG_Z, STACK_ROW_Z);
    display_draw_dotted_hline(0, DISPLAY_WIDTH - 1, STACK_ROW_Y - ROW_GAP / 2);

    if (key_hint_active && key_hint_length > 0) {
        char buf[sizeof(key_hint_text)];
        memcpy(buf, key_hint_text, key_hint_length);
        buf[key_hint_length] = 0;
        display_draw_string(&font_10x16, buf, STACK_COL_START, STACK_ROW_Y, 0x00);
    } else if (flags.f.message && status_message_length > 0) {
        /* No "Y:" label here -- this isn't Y's value, it's an override
         * message (e.g. "DIVIDE BY 0"), matching how Free42's own
         * display_error() replaces the line wholesale, unlabeled. */
        char buf[sizeof(status_message)];
        memcpy(buf, status_message, status_message_length);
        buf[status_message_length] = 0;
        display_draw_string(&font_10x16, buf, STACK_COL_START, STACK_ROW_Y, 0x00);
    } else {
        display_reg("Y:", REG_Y, STACK_ROW_Y);
    }
    display_draw_dotted_hline(0, DISPLAY_WIDTH - 1, STACK_ROW_X - ROW_GAP / 2);

    if (mode_number_entry)
        display_entry(STACK_ROW_X);
    else if (mode_command_entry)
        display_incomplete(STACK_ROW_X);
    else
        display_reg("X:", REG_X, STACK_ROW_X);
    display_flush();
}

void free42_init(void) {
    core_init(0, 0, NULL, 0);
    display_stack();
}

void free42_process_char(char c) {
    int key = key_for_char(c);
    if (key == 0)
        return;

    bool enqueued;
    int repeat;
    core_keydown(key, &enqueued, &repeat);
    core_keyup();

    display_stack();
}

/* Physical keyboard matrix -> HP-42S key code (1-37, see keymap.txt in
 * the upstream repo), as wired on this hardware. 0 = nothing mapped at
 * that position yet (row 7 isn't wired to a function yet). */
static const int key_matrix[KB_ROWS][KB_COLS] = {
        /*      C0            C1        C2        C3           C4 */
        /*R0*/ { 33,          34,       35,       36,          37 }, /* ON/EXIT   .   R/S    + */
        /*R1*/ {  1,           2,        3,        4,           5 }, /* Sigma+  1/x sqrt   LOG   LN */
        /*R2*/ {  7,           8,        9,       10,          11 }, /*    STO RCL  Rdn  SIN  COS */
        /*R3*/ { 13,          14,       15,       16,          17 }, /*  ENTER X<>Y  CHS    E  BSP */
        /*R4*/ { 18,          19,       20,       21,          22 }, /*     Up    7    8    9    / */
        /*R5*/ { 23,          24,       25,       26,          27 }, /*   Down    4    5    6    x */
        /*R6*/ { 28,          29,       30,       31,          32 }, /*  SHIFT    1    2    3    - */
        /*R7*/ {  0,           0,        0,        0,           0 },
};

/* Human-readable name for an HP-42S key code 1-37 (verified against
 * keymap.txt in the upstream repo), used only for the serial debug print
 * below -- purely diagnostic, Free42's core never sees this string. */
static const char *key_name(int key) {
    switch (key) {
        case 1: return "Sigma+";
        case 2: return "1/X";
        case 3: return "SQRT";
        case 4: return "LOG";
        case 5: return "LN";
        case 6: return "XEQ";
        case 7: return "STO";
        case 8: return "RCL";
        case 9: return "R-down";
        case 10: return "SIN";
        case 11: return "COS";
        case 12: return "TAN";
        case 13: return "ENTER";
        case 14: return "X<>Y";
        case 15: return "+/-";
        case 16: return "E";
        case 17: return "BSP";
        case 18: return "UP";
        case 19: return "7";
        case 20: return "8";
        case 21: return "9";
        case 22: return "/";
        case 23: return "DOWN";
        case 24: return "4";
        case 25: return "5";
        case 26: return "6";
        case 27: return "*";
        case 28: return "SHIFT";
        case 29: return "1";
        case 30: return "2";
        case 31: return "3";
        case 32: return "-";
        case 33: return "EXIT";
        case 34: return "0";
        case 35: return ".";
        case 36: return "R/S";
        case 37: return "+";
        default: return "?";
    }
}

/* State for the currently-held physical key, set by free42_key_down() and
 * consulted by the timeout/repeat/release functions below. held_key == 0
 * means "no key currently down" (either nothing is pressed, or the last
 * pressed position has no function mapped). */
static int held_key = 0;
static bool held_enqueued = false;
static int held_repeat = 0;

void free42_key_down(uint8_t row, uint8_t col) {
    held_key = 0;
    held_enqueued = false;
    held_repeat = 0;

    if (row >= KB_ROWS || col >= KB_COLS)
        return;

    int key = key_matrix[row][col];
    if (key == 0)
        return;

    printf("R%uC%u: %s\n", row, col, key_name(key));

    held_key = key;

    bool enqueued;
    int repeat;
    core_keydown(key, &enqueued, &repeat);
    held_enqueued = enqueued;
    held_repeat = repeat;

    display_stack();
}

bool free42_key_is_enqueued(void) {
    return held_key != 0 && held_enqueued;
}

/* 0 = no auto-repeat, 1 = slow (SST/BST), 2 = fast (number/alpha entry) --
 * see core_keydown()'s 'repeat' parameter in core_main.h. While this is
 * nonzero the caller should drive free42_key_repeat_tick() instead of the
 * timeout1/timeout2 hold sequence -- matching the real shells, which never
 * show a key name/NULL for an auto-repeating key. */
int free42_key_repeat_mode(void) {
    return held_key != 0 ? held_repeat : 0;
}

void free42_key_repeat_tick(void) {
    if (held_key == 0)
        return;

    held_repeat = core_repeat();
    display_stack();
}

/* Key has been held for 1/4 second: show its function name on the Y row.
 * Deliberately does NOT call the real core_keytimeout1() -- that function's
 * only job besides this gating check is to draw into Free42's internal
 * single-line pixel display buffer (display_command()/display_x()/
 * flush_display()), a code path this port never uses anywhere else (we
 * render our own 4-line LCD directly from stack/cmdline state) and so is
 * untested in this minimal, skin-less build. The gating logic itself
 * (mirrored below from core_keytimeout1()'s source) has no side effects,
 * so re-deriving the text ourselves gets the same behavior without going
 * anywhere near that unexercised internal renderer. */
void free42_key_timeout1(void) {
    if (held_key == 0 || held_enqueued)
        return;

    if (pending_command != CMD_NONE && pending_command != CMD_CANCELLED
            && !(cmd_array[pending_command].flags & FLAG_NO_SHOW)) {
        key_hint_length = cmd_array[pending_command].name_length;
        if (key_hint_length > (int) sizeof(key_hint_text) - 1)
            key_hint_length = (int) sizeof(key_hint_text) - 1;
        /* Some command names pack a letter as ASCII|0x80 -- e.g. ENTER is
         * literally stored "ENT\305R" (0305 octal = 0xC5 = 'E' + 0x80),
         * not a real symbol. Confirmed against Free42's own renderer
         * (core_display.cc's draw_key()): it does `c &= 127` for any
         * undefined_char() byte (>=139) before drawing. Without that mask
         * here, our font has no glyph for the raw byte, so it drew as a
         * blank space -- "ENT R" instead of "ENTER". */
        const char *name = cmd_array[pending_command].name;
        for (int i = 0; i < key_hint_length; i++) {
            unsigned char c = (unsigned char) name[i];
            if (undefined_char(c))
                c &= 127;
            key_hint_text[i] = (char) c;
        }
        key_hint_active = true;
        display_stack();
    }
}

/* Key has been held for 2 seconds: switch to "NULL", meaning release now
 * cancels the key. Also skips the real core_keytimeout2() for the same
 * reason as free42_key_timeout1() above -- same unexercised pixel-display
 * calls. The one effect we actually need from it, pending_command =
 * CMD_CANCELLED (which is what makes the upcoming free42_key_up()'s
 * core_keyup() call skip the action instead of performing it), is just a
 * plain global write, so we do it directly ourselves. */
void free42_key_timeout2(void) {
    if (held_key == 0 || held_enqueued)
        return;

    if (pending_command != CMD_NONE && pending_command != CMD_CANCELLED
            && !(cmd_array[pending_command].flags & FLAG_NO_SHOW)) {
        pending_command = CMD_CANCELLED;
        memcpy(key_hint_text, "NULL", 4);
        key_hint_length = 4;
        key_hint_active = true;
        display_stack();
    }
}

void free42_key_up(void) {
    if (held_key != 0 && !held_enqueued)
        core_keyup();

    held_key = 0;
    held_enqueued = false;
    held_repeat = 0;
    key_hint_active = false;

    display_stack();
}
