#include <stdio.h>
#include <string.h>

#include "free42_app.h"
#include "core_main.h"
#include "core_globals.h"
#include "core_helpers.h"
#include "core_tables.h"
#include "core_variables.h"
#include "core_display.h"
#include "display.h"
#include "keyboard.h"
#include "fonts.h"
#include "icons.h"
#include "rtc.h"
#include "battery.h"
#include "splash.h"
#include "power.h"
#include "settings.h"
#include "sounds.h"
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
 * is only handing control back because it noticed a pending event. Free42's
 * own reference shells (e.g. free42-upstream/gtk/shell_main.cc) drive this
 * via a timer, calling core_keydown(0, ...) repeatedly until it returns
 * false, to let the program actually keep executing.
 *
 * This port was missing that entirely -- every call site here just
 * discarded the return value. As long as nothing ever set the calculator
 * running, that went unnoticed; the first keypress that did (R/S, or XEQ
 * on a program) left it stuck forever in a running-but-never-pumped
 * limbo: our own main-loop keyboard scan kept working fine (so presses
 * still showed up on the debug serial log), but nothing ever advanced
 * the program, and the display just kept re-showing the same frozen
 * state.
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
static bool timeout3_armed = false;   /* shell_request_timeout3(): PSE etc. */
static absolute_time_t timeout3_at;

static void pump_running(bool keep_running) {
    run_pending = keep_running;
}

bool free42_slice_expired(void) {
    return time_us_64() - slice_start_us >= SLICE_US;
}

bool free42_next_timeout(absolute_time_t *at) {
    if (!timeout3_armed)
        return false;
    *at = timeout3_at;
    return true;
}

void free42_request_timeout3(int delay_ms) {
    timeout3_at = make_timeout_time_ms((uint32_t) delay_ms);
    timeout3_armed = true;
}

/* LCD row assignment for the Alpha register and the 4-level stack, in
 * pixels -- font_10x16 is 16px tall, with a 3px gap between lines (one
 * row of space, the dotted separator, one row of space), so each line is
 * ROW_STRIDE (19px) below the last, starting at ALPHA_ROW. ROW_SEP(next)
 * gives the middle row of the 3px gap right before 'next', where
 * display_stack() draws that gap's separator. */
#define ALPHA_ROW    13
#define ROW_GAP      3
#define ROW_STRIDE   (16 + ROW_GAP) /* font_10x16 height + gap */
#define STACK_ROW_T  (ALPHA_ROW + ROW_STRIDE)
#define STACK_ROW_Z  (STACK_ROW_T + ROW_STRIDE)
#define STACK_ROW_Y  (STACK_ROW_Z + ROW_STRIDE)
#define STACK_ROW_X  (STACK_ROW_Y + ROW_STRIDE)
#define ROW_SEP(next) ((next) - (ROW_GAP + 1) / 2)

/* Starting pixel column for every text row (leaves a 4px left margin). */
#define STACK_COL_START 4

/* A message from the core -- an error, VIEW, AVIEW, PROMPT... -- is the
 * text it drew on its own two-row display (core_display.cc's
 * display_row_text()), shown for as long as the core considers it active
 * (flags.f.message): row 0 in place of Y, and row 1 in place of X for a
 * two-line message. */
static void draw_message_row(int row, uint8_t y) {
    int length;
    const char *text = display_row_text(row, &length);
    display_draw_chars(&font_large, text, length, STACK_COL_START, y, 0x00);
}

/* MEM (in the CATALOG) and shift-VARMENU draw both rows without setting
 * flags.f.message: a "lingering" display, up while the key is held and
 * until the next key or a timeout (core_main.cc's CMD_LINGER1/2). */
static bool lingering() {
    return pending_command == CMD_LINGER1 || pending_command == CMD_LINGER2;
}

/* Only with text to show: the flag also comes back set from a loaded state
 * (turned off, or crashed, with a message up) whose text is gone -- the Y
 * row was blank then. */
static bool message_active() {
    int length0, length1;
    display_row_text(0, &length0);
    display_row_text(1, &length1);
    return (flags.f.message || lingering()) && length0 + length1 > 0;
}

static bool message_two_lines() {
    return (flags.f.message && flags.f.two_line_message) || lingering();
}

/* Transient override for the Y row while a key is held down, matching the
 * real HP-42S/Free42 "hold to confirm" behavior: hold a key for 1/4 second
 * and its function name appears (see free42_key_timeout1()); keep holding
 * to 2 seconds and it switches to "NULL", meaning releasing now cancels the
 * key instead of performing it (free42_key_timeout2()). Cleared on release
 * (free42_key_up()). Takes priority over a message -- it's describing
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
/* value is vlen bytes, not a C string: Free42 text can contain byte 0 --
 * the divide sign -- so it's drawn by length (display_draw_chars()). */
static void display_line_n(const char *label, const char *value, int vlen, uint8_t y) {
    char full[64];
    int llen = (int) strlen(label);
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
        display_draw_chars(&font_large, full, total, STACK_COL_START, y, 0x00);
    } else {
        char buf[LINE_CHARS];
        int tail_len = LINE_CHARS - 1;
        buf[0] = 26;
        memcpy(buf + 1, full + (total - tail_len), tail_len);
        display_draw_chars(&font_large, buf, LINE_CHARS, STACK_COL_START, y, 0x00);
    }
}

static void display_line(const char *label, const char *value, uint8_t y) {
    display_line_n(label, value, (int) strlen(value), y);
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

/* SHOW (Shift+. -- see free42_key_down()): full-precision X only, matching
 * the real HP-42S/Free42 -- not Y, and no label. Ignores the current
 * FIX/SCI/ENG digit-count setting entirely, the same way Free42's own
 * show() does (core_display.cc) -- dispmode 3/"ALL" and digits 0 passed
 * directly to phloat2string() rather than derived from flags, so the
 * value's true stored precision is visible regardless of how numbers are
 * normally displayed. The retry-without-thousands-separators-on-overflow
 * is copied verbatim from show(): a 45-char buffer is what its own mantissa
 * formatting is sized for, and thousands separators are the one thing that
 * can push a fully-precise mantissa past that. Non-real types (complex,
 * string, matrix) fall back to vartype2string(). Draws into the last two
 * display rows (STACK_ROW_Y/STACK_ROW_X) as ONE value: on STACK_ROW_Y
 * alone if it fits LINE_CHARS, wrapping onto STACK_ROW_X too if not -- same
 * "row 0, then row 1 if needed" layout as show()'s own
 * show_one_or_two_lines, and deliberately not truncated with an ellipsis
 * like display_line()'s other callers, since showing every digit is the
 * whole point. */
static void display_show_x() {
    char buf[48];
    int len;
    vartype *v = stack[REG_X];
    if (v->type == TYPE_REAL) {
        len = phloat2string(((vartype_real *) v)->x, buf, 45,
                             2, 0, 3, flags.f.thousands_separators, MAX_MANT_DIGITS);
        if (len == 45)
            len = phloat2string(((vartype_real *) v)->x, buf, 44,
                                 2, 0, 3, 0, MAX_MANT_DIGITS);
    } else {
        len = vartype2string(v, buf, sizeof(buf) - 1, MAX_MANT_DIGITS);
    }
    if (len > (int) sizeof(buf) - 1)
        len = (int) sizeof(buf) - 1;
    buf[len] = 0;

    if (len <= LINE_CHARS) {
        display_draw_string(&font_large, buf, STACK_COL_START, STACK_ROW_Y, 0x00);
    } else {
        char first[LINE_CHARS + 1];
        memcpy(first, buf, LINE_CHARS);
        first[LINE_CHARS] = 0;
        display_draw_string(&font_large, first, STACK_COL_START, STACK_ROW_Y, 0x00);
        display_draw_string(&font_large, buf + LINE_CHARS, STACK_COL_START, STACK_ROW_X, 0x00);
    }
}

/* Alpha register (reg_alpha/reg_alpha_length, core_globals.h) -- unlike
 * the stack registers, this is raw text, not a value to format. Drawn by
 * length: it can contain byte 0 (the divide sign). */
static void display_alpha(uint8_t y) {
    int len = reg_alpha_length;
    if (len > (int) sizeof(reg_alpha))
        len = (int) sizeof(reg_alpha);
    display_line_n("A:", reg_alpha, len, y);
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
    /* By length: a STO-divide prompt, or an ALPHA argument, can contain
     * byte 0 (the divide sign). Same front-truncate-with-ellipsis
     * convention as display_line()'s other callers. */
    display_line_n("", buf, n, y);
}

/* Status header: date, time, angle mode, flags 0-4, shift indicator, and
 * battery, left to right, in the top HEADER_HEIGHT rows -- solid black
 * background with inverted (white-on-black) text, using font_8x12 (12px
 * tall, an exact fit for the header height, so glyphs run edge to edge
 * with no vertical margin). Every field has a fixed place, whatever the
 * date and time formats: each as wide as its longest value -- the date
 * "FRI-OCT-19", the time "12:43PM", the angle mode (one character: ∡ DEG,
 * π RAD, G GRAD), the five flags, the shift mark -- with a character's
 * width between them, so a shorter value (10/09, 15:43) leaves the end of
 * its field blank and nothing moves. A set flag (0-4) shows
 * its digit in its own column; an unset one leaves the column blank. */
#define HEADER_HEIGHT    12
#define HEADER_CHAR      8 /* font_small's advance: font sizes are fixed (fonts.h) */
#define HEADER_GAP       HEADER_CHAR
#define HEADER_DATE_X    1
#define HEADER_TIME_X    (HEADER_DATE_X + 10 * HEADER_CHAR + HEADER_GAP)
#define HEADER_ANGLE_X   (HEADER_TIME_X + 7 * HEADER_CHAR + HEADER_GAP)
#define HEADER_FLAGS_X   (HEADER_ANGLE_X + HEADER_CHAR + HEADER_GAP)
#define HEADER_SHIFT_X   (HEADER_FLAGS_X + 5 * HEADER_CHAR + HEADER_GAP)
#define HEADER_BATTERY_X (DISPLAY_WIDTH - 19) /* icons.h: 18px, then a 1px margin */
static_assert(HEADER_SHIFT_X + HEADER_CHAR + 2 <= HEADER_BATTERY_X, "header fields overlap the battery");

/* ===================== date and time formats ===================== */

/* Free42's own: flags DMY/YMD and its CLK12/CLK24 mode -- the commands
 * DMY, MDY, YMD, CLK12, CLK24 set them. px_settings keeps a copy, for the
 * PX-42F's own screens and for when Free42 isn't running (the splash's
 * clock while off). */
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

static void display_header() {
    mirror_formats();
    display_fill_rect(0, 0, DISPLAY_WIDTH - 1, HEADER_HEIGHT - 1, true);

    /* One clock reading for both, so date and time can't disagree across
     * midnight. In the calculator's formats (YMD/DMY/MDY, CLK12/CLK24). */
    rtc_datetime_t now;
    rtc_get_datetime(&now);
    char buf[12];
    rtc_format_day_month(buf, sizeof(buf), &now);
    display_draw_string(&font_small, buf, HEADER_DATE_X, 0, 0xFF);
    rtc_format_time(buf, sizeof(buf), &now, true);
    display_draw_string(&font_small, buf, HEADER_TIME_X, 0, 0xFF);

    char angle = flags.f.rad ? 7 : flags.f.grad ? 'G' : 23; /* π, G, ∡ */
    display_draw_char(&font_small, angle, HEADER_ANGLE_X, 0, 0xFF);

    for (int i = 0; i < 5; i++)
        if (flags.farray[i])
            display_draw_char(&font_small, (char) ('0' + i), (uint8_t) (HEADER_FLAGS_X + i * HEADER_CHAR), 0, 0xFF);

    if (mode_shift)
        display_draw_char(&font_small, '\r', HEADER_SHIFT_X, 0, 0xFF); /* ↵, code 13 */

    icon_battery(HEADER_BATTERY_X, 0, battery_level());
}

/* Footer: 6 menu-key "tabs" in the bottom FOOTER_HEIGHT rows, replicating
 * the subset of Free42's own redisplay() (core_display.cc) needed to read
 * the current menu's 6 labels without going through its internal pixel
 * renderer (draw_key()/the single-line display[] buffer), which this
 * port has never used anywhere else: general app menus (the `menus[]`
 * table -- MODES, DISP, CLEAR, and similar) and VARMENU (the variables
 * menu). The CATALOG, CUSTOM1-3 and PROGRAMMABLE menus, whose label text
 * lives in variables private to core_display.cc, are shown as the core
 * drew them instead (display_key_label()). Not covered: the
 * CMD_BINM/OCTM/DECM/HEXM highlight cases (need get_base(), not exported)
 * and shifted alternate labels on a handful of specific top-level menus
 * (MENU_TOP_FCN and friends) -- both narrow cosmetic gaps; the physical
 * keys underneath still do the right thing regardless of what the label
 * says. */
#define FOOTER_Y0     108
#define FOOTER_HEIGHT 14
#define FOOTER_TABS   6

/* Manual override for the LBL digit/alpha ambiguity (lbl_alpha_hidden()
 * right below, and free42_key_down()'s own lbl_wants_digit), toggled by
 * Shift+ENTER -- the real HP-42S's own ALPHA key (ENTER's shifted
 * function, per its keyboard legend) -- while the LBL prompt is up.
 * free42_key_down() resets this to false whenever we're not actively in
 * that ambiguous window, so it can't leak into a later, unrelated LBL
 * prompt. */
static bool lbl_alpha_override = false;

/* True while LBL's argument prompt is ambiguous between a numeric label
 * (e.g. "LBL 12") and a named one, and nothing has broken the tie yet --
 * see free42_key_down()'s comment on lbl_wants_digit for why this
 * ambiguity needs resolving on this port at all (real hardware doesn't
 * need it). One flag drives both sides of it: the on-screen ALPHA
 * soft-key labels (current_menu_id(), right below) stay hidden and the
 * overlaid digit keys type plain digits for as long as this is true --
 * typing an actual letter (which sets incomplete_alpha) or toggling
 * lbl_alpha_override both end it, showing the labels and switching
 * every overlaid key to its letter for the rest of this LBL entry. */
static bool lbl_alpha_hidden() {
    return mode_command_entry && incomplete_command == CMD_LBL
            && !incomplete_alpha && !lbl_alpha_override;
}

static int current_menu_id() {
    if (lbl_alpha_hidden())
        return MENU_NONE;
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

/* Draws tab i (0-5) of FOOTER_TABS equal-width segments: a black
 * rectangle with its (inverted, white) label, truncated to whatever fits
 * the tab's width. Every tab (the last one included) keeps a 1px gap
 * after it, matching the "1px margin from the edge" convention already
 * used elsewhere (the header's battery icon) -- without it, the last
 * tab's rounded corner had nothing beyond it to contrast against and so
 * was unreadable right at the display's physical edge.
 *
 * FOOTER_HEIGHT (14) minus the font's height (12) leaves only 2 spare
 * rows total, so there's a tradeoff: the label sits flush against the
 * top (no centering) so both spare rows land at the bottom, free for a
 * highlighted tab's 2px underline. Top corners get a small 2-step inset
 * (2px, then 1px) to read as rounded -- since the label is centered
 * horizontally with margin on both sides, this corner trim (2px into
 * the far left/right of the top row) essentially never touches actual
 * glyph ink; and even if it did, both are the same white/background
 * tone, so there's nothing to clash. */
static void draw_footer_tab(int i, const char *text, int text_len, bool highlight) {
    int x0 = (i * DISPLAY_WIDTH) / FOOTER_TABS;
    int x1 = ((i + 1) * DISPLAY_WIDTH) / FOOTER_TABS - 1;
    int fill_x1 = x1 - 1;
    int bottom = FOOTER_Y0 + FOOTER_HEIGHT - 1;

    display_fill_rect((uint8_t) (x0 + 2), FOOTER_Y0, (uint8_t) (fill_x1 - 2), FOOTER_Y0, true);
    display_fill_rect((uint8_t) (x0 + 1), (uint8_t) (FOOTER_Y0 + 1), (uint8_t) (fill_x1 - 1), (uint8_t) (FOOTER_Y0 + 1), true);
    display_fill_rect((uint8_t) x0, (uint8_t) (FOOTER_Y0 + 2), (uint8_t) fill_x1, (uint8_t) bottom, true);

    int max_chars = (fill_x1 - x0 + 1) / font_small.advance;
    if (text_len > max_chars)
        text_len = max_chars;
    int text_width = text_len * font_small.advance;
    int tx = x0 + ((fill_x1 - x0 + 1) - text_width) / 2;

    for (int k = 0; k < text_len; k++)
        tx = display_draw_char(&font_small, text[k], (uint8_t) tx, FOOTER_Y0, 0xFF);

    if (highlight)
        display_fill_rect((uint8_t) x0, (uint8_t) (bottom - 1), (uint8_t) fill_x1, (uint8_t) bottom, false);
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
            || menu_id == MENU_CUSTOM3 || menu_id == MENU_PROGRAMMABLE) {
        /* Their labels aren't in the menu tables -- the catalog's come from
         * the function, program and variable lists, CUSTOM's from the key
         * assignments, a programmable menu's from KEYG/KEYX -- so they're
         * taken as the core drew them (core_display.cc's
         * display_key_label()). */
        for (int i = 0; i < 6; i++) {
            int len;
            bool highlight;
            const char *text = display_key_label(i, &len, &highlight);
            draw_footer_tab(i, text, len, highlight);
        }
        return;
    }

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

/* Five fixed text rows, evenly spaced between the header and footer --
 * the same slots display_stack() below uses for Alpha+T+Z+Y+X. There's
 * nothing stack-specific about these y-coordinates, so display_prgm_listing()
 * reuses them as-is for the program listing. */
#define PRGM_WINDOW_ROWS 5
static const uint8_t prgm_row_y[PRGM_WINDOW_ROWS] =
        { ALPHA_ROW, STACK_ROW_T, STACK_ROW_Z, STACK_ROW_Y, STACK_ROW_X };

/* Live feedback for a number being typed into a program line -- mirrors
 * the real core's own "draw_number:" label in keydown_number_entry()
 * (core_keydown.cc) byte for byte: 2-digit line number, then the same
 * highlight byte prgmline2buf() uses for the current line, then the raw
 * cmdline buffer, then an entry cursor. That code path draws straight
 * into Free42's internal single-line pixel buffer, which (like
 * everywhere else in this file) this port never flushes to the panel --
 * so without this, every digit typed into a program line was invisible,
 * not just off by a row. */
static void display_prgm_number_entry(uint8_t y, int4 line) {
    char buf[LINE_CHARS + 1];
    int n = 0;
    if (line < 10)
        char2buf(buf, sizeof(buf), &n, '0');
    n += int2string(line, buf + n, (int) sizeof(buf) - n);
    char2buf(buf, sizeof(buf), &n, 6); /* current-line marker, matching prgmline2buf() */
    string2buf(buf, sizeof(buf), &n, cmdline, cmdline_length);
    char2buf(buf, sizeof(buf), &n, '_');
    if (n > (int) sizeof(buf) - 1)
        n = (int) sizeof(buf) - 1;
    buf[n] = 0;
    display_draw_string(&font_large, buf, STACK_COL_START, y, 0x00);
}

/* While flags.f.prgm_mode is set, replaces the Alpha+T+Z+Y+X stack view
 * with a scrolling window of program lines centered on the current line
 * (pc2line(pc)), reusing prgm_row_y[] above. The real HP-42S/Free42 only
 * ever shows 1-2 program lines at a time (display_prgm_line(),
 * core_display.cc) because that's all its own display has room for; this
 * hardware has room for 5, so this shows up to 2 lines of context on
 * either side of the current line -- sliding the window toward whichever
 * end has less to show near the start or end of a program (same as any
 * ordinary scrolling list) rather than wasting rows on blank space.
 *
 * Line text comes from prgmline2buf() (core_display.cc, exposed via
 * core_display.h for exactly this) -- the same formatting the real
 * renderer uses for LBL/GTO/numbers/strings/.END./etc, so every
 * instruction type is handled correctly for free, including a live
 * ALPHA entry (mode_alpha_entry): prgmline2buf() renders that itself
 * whenever highlight is set, reading the entered_string global directly.
 * The two things it doesn't cover -- a number actively being typed, and
 * an argument prompt still waiting on its digits/name -- are handled by
 * display_prgm_number_entry() above and the existing display_incomplete()
 * respectively, same as the real core's own display_prgm_line() callers
 * handle them one level up. */
static void display_prgm_listing() {
    debug_checkpoint(CKPT_DISPLAY_PRGM_LISTING, 0);
    if (message_active()) {
        /* Rare (e.g. "PROGRAM LOCKED" from a locked program): no room set
         * aside for this in the listing layout, so it just takes over one
         * central row, unlabeled -- same "message replaces everything"
         * convention display_stack() uses for the Y row. */
        draw_message_row(0, prgm_row_y[PRGM_WINDOW_ROWS / 2]);
        return;
    }

    int4 curline = pc2line(pc);

    int cur_cmd = CMD_NONE;
    if (curline != 0) {
        int4 tmppc = pc;
        arg_struct discard_arg;
        const char *discard_num;
        get_next_command(&tmppc, &cur_cmd, &discard_arg, 0, &discard_num);
    }
    bool curline_is_end = curline != 0 && cur_cmd == CMD_END;

    /* How far the window could extend below curline, capped at
     * PRGM_WINDOW_ROWS-1 (the most we could ever show) -- stopping as
     * soon as .END. turns up so it isn't recounted forever (line2pc()
     * clamps any line number past the program's end to .END.'s own
     * line). */
    int max_after = 0;
    if (!curline_is_end) {
        for (int k = 1; k < PRGM_WINDOW_ROWS; k++) {
            debug_checkpoint(CKPT_DISPLAY_PRGM_LISTING_MAX_AFTER, (uint32_t) k);
            int4 tmppc = line2pc(curline + k);
            int tcmd;
            arg_struct targ;
            const char *tnum;
            get_next_command(&tmppc, &tcmd, &targ, 0, &tnum);
            max_after = k;
            if (tcmd == CMD_END)
                break;
        }
    }
    int max_before = curline < PRGM_WINDOW_ROWS - 1 ? (int) curline : PRGM_WINDOW_ROWS - 1;

    /* Centered (2 above, 2 below) when there's enough program on both
     * sides; otherwise give the side that ran out fewer rows and hand
     * the rest to the other side. */
    int before = max_before < 2 ? max_before : 2;
    int after = PRGM_WINDOW_ROWS - 1 - before;
    if (after > max_after)
        after = max_after;
    before = PRGM_WINDOW_ROWS - 1 - after;
    if (before > max_before)
        before = max_before;

    int4 tmpline = curline - before;
    bool ended = false;
    for (int i = 0; i < PRGM_WINDOW_ROWS; i++, tmpline++) {
        debug_checkpoint(CKPT_DISPLAY_PRGM_LISTING, 0x10000u | ((uint32_t) i << 8) | (uint32_t) (tmpline & 0xff));
        uint8_t y = prgm_row_y[i];
        if (ended)
            continue;

        bool is_current = tmpline == curline;
        if (is_current && mode_command_entry) {
            display_incomplete(y);
            continue;
        }
        if (is_current && mode_number_entry) {
            display_prgm_number_entry(y, tmpline);
            continue;
        }

        int cmd = CMD_NONE;
        arg_struct arg;
        const char *orig_num = NULL;
        if (tmpline != 0) {
            int4 tmppc = line2pc(tmpline);
            get_next_command(&tmppc, &cmd, &arg, 0, &orig_num);
        }
        char buf[LINE_CHARS + 1];
        int len = prgmline2buf(buf, LINE_CHARS, tmpline, is_current ? 2 : 0, cmd, &arg, orig_num);
        if (len > LINE_CHARS)
            len = LINE_CHARS;
        /* By length -- a divide step contains byte 0 (the divide sign). */
        display_draw_chars(&font_large, buf, len, STACK_COL_START, y, 0x00);
        if (cmd == CMD_END)
            ended = true;
    }
}

/* show_x is set only by the SHOW path (free42_key_down(), on Shift+.) --
 * it's mutually exclusive with key_hint/a message/number-entry/
 * command-entry by construction (SHOW is only reachable from plain normal
 * mode, see the gating there). When set, the Y row is left to
 * display_show_x() below (it may or may not use it -- see there) instead
 * of showing Y's own value: SHOW is X only, matching the real HP-42S. */
static void display_stack(bool show_x = false) {
    debug_checkpoint(CKPT_DISPLAY_STACK, flags.f.prgm_mode ? 1u : 0u);
    display_clear();

    display_header();
    display_footer();

    if (flags.f.prgm_mode) {
        display_prgm_listing();
        debug_checkpoint(CKPT_DISPLAY_FLUSH, 1);
        display_flush();
        return;
    }

    display_alpha(ALPHA_ROW);
    display_draw_dotted_hline(0, DISPLAY_WIDTH - 1, ROW_SEP(STACK_ROW_T));

    display_reg("T:", REG_T, STACK_ROW_T);
    display_draw_dotted_hline(0, DISPLAY_WIDTH - 1, ROW_SEP(STACK_ROW_Z));

    display_reg("Z:", REG_Z, STACK_ROW_Z);
    display_draw_dotted_hline(0, DISPLAY_WIDTH - 1, ROW_SEP(STACK_ROW_Y));

    if (show_x) {
        /* left to display_show_x(), called below */
    } else if (key_hint_active && key_hint_length > 0) {
        /* By length, not as a C string: the divide key's name is the single
         * byte 0 (Free42's divide sign, font entry 0). */
        display_draw_chars(&font_large, key_hint_text, key_hint_length, STACK_COL_START, STACK_ROW_Y, 0x00);
    } else if (message_active()) {
        /* No "Y:" label here -- this isn't Y's value, it's an override
         * message (e.g. "DIVIDE BY 0"), matching how Free42's own
         * display_error() replaces the line wholesale, unlabeled. */
        draw_message_row(0, STACK_ROW_Y);
    } else {
        display_reg("Y:", REG_Y, STACK_ROW_Y);
    }
    display_draw_dotted_hline(0, DISPLAY_WIDTH - 1, ROW_SEP(STACK_ROW_X));

    if (show_x)
        display_show_x();
    else if (message_two_lines() && !key_hint_active)
        draw_message_row(1, STACK_ROW_X); /* e.g. a long AVIEW's second line, MEM's byte count */
    else if (mode_number_entry)
        display_entry(STACK_ROW_X);
    else if (mode_command_entry)
        display_incomplete(STACK_ROW_X);
    else
        display_reg("X:", REG_X, STACK_ROW_X);
    debug_checkpoint(CKPT_DISPLAY_FLUSH, 0);
    display_flush();
}

/* Where free42_sleep() puts the calculator's memory if it doesn't fit in
 * the RAM buffer (ramstate.c) -- at the drive's root, out of the STATE
 * list. */
#define OFF_STATE_FILE "/OFFSTATE.f42"

void free42_init(bool after_crash) {
    /* The memory saved at the last OFF, from RAM or, failing that, the
     * drive. It's kept after loading, so a crash later on comes back to it
     * (the memory as of this ON) instead of Memory Clear -- but not when
     * this boot itself follows a crash: reading the RAM state uses it up,
     * and the file is deleted, so a state that crashes the calculator can
     * do it once more at most. 26: see free42_save_state(). */
    bool from_ram = ramstate_valid();
    if (from_ram) {
        core_init(1, 26, RAMSTATE_PATH, 0);
        pump_running(core_powercycle());
    } else if (statefs_ready() && statefs_file_exists(OFF_STATE_FILE)) {
        core_init(1, 26, OFF_STATE_FILE, 0);
        if (after_crash) {
            statefs_remove(OFF_STATE_FILE);
            statefs_sync();
        }
        pump_running(core_powercycle());
    } else {
        core_init(0, 0, NULL, 0);
    }
    /* The formats as last saved (they follow Free42's DMY, CLK12... at
     * OFF): kept across a reboot, a reflash or Memory Clear. */
    free42_set_formats(px_settings.date_format, px_settings.clock_24h);
    watchdog_update(); /* loading a big memory, then drawing it */
    display_stack();
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

    display_stack();
}

/* Physical keyboard matrix -> HP-42S key code (1-37, see keymap.txt in
 * the upstream repo), as wired on this hardware. 0 = no function at that
 * position (R7C2-C4 aren't wired to a key). */
static const int key_matrix[KB_ROWS][KB_COLS] = {
        /*      C0            C1        C2        C3           C4 */
        /*R0*/ {  1,           2,        3,        4,           5 }, /* Sigma+  1/x sqrt   LOG   LN */
        /*R1*/ {  7,           8,        9,       10,          11 }, /*    STO RCL  Rdn  SIN  COS */
        /*R2*/ { 13,          14,       15,       16,          17 }, /*  ENTER X<>Y  CHS    E  BSP */
        /*R3*/ { 18,          19,       20,       21,          22 }, /*     Up    7    8    9    / */
        /*R4*/ { 23,          24,       25,       26,          27 }, /*   Down    4    5    6    x */
        /*R5*/ { 28,          29,       30,       31,          32 }, /*  SHIFT    1    2    3    - */
        /*R6*/ { 33,          34,       35,       36,          37 }, /* ON/EXIT   0    .  R/S    + */
        /*R7*/ {  6,          12,        0,        0,           0 }, /*    XEQ  TAN    -    -    - */
};

/* Alpha-mode overlay: while alpha_active() (the core's ALPHA text-entry
 * mode), a position with a nonzero entry here types that literal
 * character instead of its normal key_matrix (HP-42S key code) meaning
 * -- see free42_key_down(), which injects it via Free42's ASCII/Unicode
 * key-code convention (core_keydown() with key = 1024 + the character's
 * byte value, per core_main.cc's core_keydown()) rather than looking it
 * up in key_matrix at all. A 0 entry leaves that position as-is (falls
 * through to key_matrix): most of column 0, for instance, keeps its
 * normal meaning -- ENTER, BSP, the arrow keys, SHIFT, EXIT -- since
 * those are still needed while typing text, not just calculating.
 *
 * SHIFT (R5C0, unchanged from key_matrix) changes what the next key does,
 * HP-41 style (see free42_key_down()):
 *   - on the six soft-menu keys (Sigma+ 1/x sqrt LOG LN XEQ -- the A-F
 *     keys): presses that soft key, so the ALPHA menu on the bottom line
 *     works as on the DM42 / HP-42S -- groups, punctuation, symbols, and in
 *     a letter group the lowercase letter;
 *   - on a digit key: types the digit;
 *   - on the four arithmetic keys: types its symbol -- divide, multiply,
 *     '+', '-' (unshifted, '-' types '_' and '+' a space);
 *   - on any other letter key: types the lowercase letter.
 * See alpha_shifted_char(). */
static const char alpha_key_matrix[KB_ROWS][KB_COLS] = {
        /*      C0   C1   C2   C3   C4 */
        /*R0*/ { 'A', 'B', 'C', 'D', 'E' },
        /*R1*/ { 'G', 'H', 'I', 'J', 'K' },
        /*R2*/ {   0, 'M', 'N', 'O',   0 },
        /*R3*/ {   0, 'P', 'Q', 'R', 'S' },
        /*R4*/ {   0, 'T', 'U', 'V', 'W' },
        /*R5*/ {   0, 'X', 'Y', 'Z', '_' },
        /*R6*/ {   0, ':', '.', '?', ' ' },
        /*R7*/ { 'F', 'L',   0,   0,   0 },
};

/* True for the ten HP-42S key codes that type a digit -- used below to let
 * LBL's argument prompt take digits even while alpha_active() (see
 * free42_key_down()). */
static bool is_digit_key(int key) {
    switch (key) {
        case KEY_0: case KEY_1: case KEY_2: case KEY_3: case KEY_4:
        case KEY_5: case KEY_6: case KEY_7: case KEY_8: case KEY_9:
            return true;
        default:
            return false;
    }
}

/* What SHIFT + key types in ALPHA mode when it isn't just the lowercase
 * letter: digits on the digit keys, symbols on the arithmetic keys -- as a
 * Unicode code point, which core_keydown() translates to Free42's own
 * character set (divide and multiply become its characters 0 and 1).
 * -1 = nothing special. */
static char digit_char(int key);

static int alpha_shifted_char(int key) {
    if (is_digit_key(key))
        return digit_char(key);
    switch (key) {
        case KEY_DIV: return 0x00F7; /* division sign */
        case KEY_MUL: return 0x00D7; /* multiplication sign */
        case KEY_ADD: return '+';
        case KEY_SUB: return '-';
        default:      return -1;
    }
}

/* The character a digit key types; only for is_digit_key() keys. */
static char digit_char(int key) {
    switch (key) {
        case KEY_0: return '0';
        case KEY_1: return '1';
        case KEY_2: return '2';
        case KEY_3: return '3';
        case KEY_4: return '4';
        case KEY_5: return '5';
        case KEY_6: return '6';
        case KEY_7: return '7';
        case KEY_8: return '8';
        default:    return '9';
    }
}

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

    /* SHIFT+EXIT -- OFF on the real HP-42S -- opens the PX-42 settings menu
     * instead (its first item is Off). Taken before the core ever sees the
     * key, in every mode; the main loop runs the menu once the key is
     * released (free42_settings_requested()). OFF as a command -- XEQ "OFF"
     * or in a program -- still powers off directly via shell_powerdown(). */
    if (mode_shift && key_matrix[row][col] == KEY_EXIT) {
        mode_shift = false;
        settings_requested = true;
        return;
    }

    bool lbl_ambiguous = mode_command_entry && incomplete_command == CMD_LBL && !incomplete_alpha;
    if (!lbl_ambiguous)
        lbl_alpha_override = false;

    /* Shift+ENTER while the LBL prompt is ambiguous flips
     * lbl_alpha_override instead of reaching core_keydown() at all --
     * this supersedes the real core's own much narrower "shift+ENTER
     * mid-digit" case (keydown_command_entry(), core_keydown.cc line
     * ~820: lets you convert a single already-typed digit into the
     * start of an alpha name) with a plain toggle that works before or
     * after any number of keystrokes, which is what this keypad's
     * fixed digit<->letter overlay actually needs. */
    if (lbl_ambiguous && mode_shift && key_matrix[row][col] == KEY_ENTER) {
        mode_shift = false;
        lbl_alpha_override = !lbl_alpha_override;
        display_stack();
        return;
    }

    /* LBL's argument prompt starts the ALPHA soft-menu (see
     * start_incomplete_command(), core_main.cc: "cmd_id == CMD_LBL ...
     * set_menu(..., MENU_ALPHA1)") since label names are usually letters
     * -- but a numeric label (e.g. "LBL 11") is just as valid, and the
     * real core's own keydown_command_entry() (core_keydown.cc, the "LBL
     * is weird" comment) special-cases exactly this: a plain digit key
     * stays numeric right up until incomplete_alpha gets set some other
     * way (typing a letter). On real hardware that works automatically
     * because the digit keys have no letter meaning of their own; on
     * this port they *do* (alpha_key_matrix, right below), so without
     * this check every digit press here would get swallowed into a
     * letter before core_keydown() -- and thus that upstream
     * disambiguation -- ever saw it. lbl_alpha_override (above) is the
     * escape hatch for when a name needs one of those overlaid letters
     * instead. Shares lbl_alpha_hidden() with current_menu_id() (above)
     * so the ALPHA soft-key footer is never showing while these keys are
     * actually acting as digits, or hidden while they're acting as
     * letters. */
    bool lbl_wants_digit = lbl_alpha_hidden() && is_digit_key(key_matrix[row][col]);

    /* SHIFT + one of the six soft-menu keys, in ALPHA mode: the soft key
     * itself, with SHIFT still set -- passed to the core below as an
     * ordinary key. Free42 then does exactly what the DM42 does: an item
     * that is a menu opens it (SHIFT doesn't matter), and a letter item
     * types its lowercase letter. Capitals don't need the menu: every one
     * has its own key. */
    int code = key_matrix[row][col];
    bool soft_key = mode_shift && code >= 1 && code <= 6;

    char alpha_c = alpha_key_matrix[row][col];
    if (alpha_c != 0 && alpha_active() && !lbl_wants_digit && !soft_key) {
        int ch = (unsigned char) alpha_c;
        int shifted = mode_shift ? alpha_shifted_char(code) : -1;
        if (shifted >= 0)
            ch = shifted; /* HP-41: SHIFT + digit key types the digit, etc. */
        else if (mode_shift && ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
        int key = 1024 + ch;

        held_key = key;
        pump_running(core_keydown(key, &enqueued, &repeat));
        held_enqueued = enqueued;
        held_repeat = repeat;

        display_stack();
        return;
    }

    int key = key_matrix[row][col];
    if (key == 0)
        return;

    /* SHOW: Shift+. (KEY_DOT=35), same physical position as the real
     * HP-42S's SHOW key. Handled entirely here rather than forwarded to
     * core_keydown() -- the core's own show() (core_display.cc, reached via
     * keydown_normal_mode()'s shifted-key switch) draws into Free42's
     * internal single-line display buffer, which this port never reads (see
     * the comment on free42_key_timeout1() above); forwarding the keypress
     * would only add untested side effects (pending_command = CMD_LINGER1,
     * shell_request_timeout3()) for no visible benefit. Restricted to plain
     * normal mode -- number/command entry and alpha mode all give "." its
     * ordinary meaning instead (decimal point / letter), matching the real
     * calculator's own gating (show() is only reached from
     * keydown_normal_mode()). Manually clearing mode_shift replaces what
     * core_keydown() would otherwise do (core_main.cc: "shift" is
     * consumed -- reset to false -- on every keypress); skipping that here
     * would leave Shift stuck on for the next, unrelated key. */
    if (key == KEY_DOT && mode_shift && !mode_number_entry && !mode_command_entry
            && !alpha_active() && !flags.f.prgm_mode) {
        mode_shift = false;
        display_stack(true);
        return;
    }

    held_key = key;
    pump_running(core_keydown(key, &enqueued, &repeat));
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

    /* >= 0: CMD_NONE, CMD_CANCELLED and CMD_LINGER1/2 (holding MEM, SHOW)
     * are negative -- not entries of cmd_array. */
    if (pending_command >= 0 && !(cmd_array[pending_command].flags & FLAG_NO_SHOW)) {
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

    if (pending_command >= 0 && !(cmd_array[pending_command].flags & FLAG_NO_SHOW)) {
        pending_command = CMD_CANCELLED;
        memcpy(key_hint_text, "NULL", 4);
        key_hint_length = 4;
        key_hint_active = true;
        display_stack();
    }
}

void free42_key_up(void) {
    down_code = 0;
    if (held_key != 0 && !held_enqueued)
        pump_running(core_keyup());

    held_key = 0;
    held_enqueued = false;
    held_repeat = 0;
    key_hint_active = false;

    display_stack();
}

/* OFF (Shift+EXIT, or the OFF command) ends up in shell_powerdown(), which
 * only records the request: it's called from deep inside core_keyup(), so
 * the actual sleep happens later, from the main loop, via free42_sleep(). */
static bool powerdown_requested = false;

char free42_alpha_char(uint8_t row, uint8_t col) {
    if (row >= KB_ROWS || col >= KB_COLS)
        return 0;
    return alpha_key_matrix[row][col];
}

/* Same sequence as Free42's own GTK shell uses to save and switch states
 * (free42-upstream/gtk/shell_main.cc): core_save_state() writes a complete
 * .f42 file; loading tears the core down and starts it again from the
 * file. 26 is the state version the shells pass for a bare .f42 file -- the
 * file carries its own version beyond that. */
void free42_save_state(const char *path) {
    core_save_state(path);
}

void free42_load_state(const char *path) {
    held_key = 0;
    key_hint_active = false;
    core_cleanup();
    core_init(1, 26, path, 0);
    pump_running(core_powercycle());
    display_stack();
}

bool free42_auto_off_enabled(void) {
    return !shell_always_on(-1);
}

void free42_set_printer(bool on) {
    flags.f.printer_exists = on ? 1 : 0; /* flag 55, same as PRON / PROFF */
}

bool free42_printer_on(void) {
    return flags.f.printer_exists;
}

#define RUN_REDRAW_US 250000 /* screen refresh while a program runs */

bool free42_service(void) {
    static uint64_t last_draw_us;

    if (timeout3_armed && time_reached(timeout3_at)) {
        timeout3_armed = false;
        if (core_timeout3(true)) /* the PSE ended: carry on running */
            run_pending = true;
        display_stack();
    }

    if (!run_pending)
        return false;

    slice_start_us = time_us_64();
    bool enqueued;
    int repeat;
    run_pending = core_keydown(0, &enqueued, &repeat);

    uint64_t now = time_us_64();
    if (!run_pending || now - last_draw_us >= RUN_REDRAW_US) {
        last_draw_us = now;
        display_stack(); /* progress while running, and the result at the end */
    }
    return run_pending;
}

void free42_redisplay(void) {
    display_stack();
}

void free42_request_powerdown(void) {
    powerdown_requested = true;
}

bool free42_powerdown_requested(void) {
    return powerdown_requested;
}

void free42_sleep(bool quiet) {
    powerdown_requested = false;
    held_key = 0;
    held_enqueued = false;
    held_repeat = 0;
    key_hint_active = false;

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
    power_deep_off(on_row, on_col, splash_until_ms);
}
