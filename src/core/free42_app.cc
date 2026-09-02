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

/* LCD row assignment for the 4-level stack (block-rows, see display.h). */
#define STACK_ROW_T 1
#define STACK_ROW_Z 3
#define STACK_ROW_Y 5
#define STACK_ROW_X 7

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

/* font_24x10's 11px advance per character (10px glyph + 1px gap) gives
 * (250 - STACK_COL_START) / 11 = ~22 characters per line, matching
 * Free42's own native 22-character display width. */
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
static void display_line(const char *label, const char *value, uint8_t block_row) {
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
     * font_24x10 covers the full 0-127 range, byte 24 draws directly as
     * font entry 24 -- whatever glyph lives there (a dedicated exponent
     * "E", distinct from a plain letter 'E') -- no translation needed. */

    if (total <= LINE_CHARS) {
        display_draw_string(full, STACK_COL_START, block_row, 0x00);
    } else {
        char buf[LINE_CHARS + 1];
        int tail_len = LINE_CHARS - 1;
        buf[0] = 26;
        memcpy(buf + 1, full + (total - tail_len), tail_len);
        buf[LINE_CHARS] = 0;
        display_draw_string(buf, STACK_COL_START, block_row, 0x00);
    }
}

static void display_reg(const char *label, int index, uint8_t block_row) {
    char buf[48];
    /* Default 12 mantissa digits (same as Free42's own vartype2string()
     * default), worst case ~19 chars with sign/decimal/exponent --
     * display_line() truncates from the front if it ever doesn't fit. */
    int len = vartype2string(stack[index], buf, sizeof(buf) - 1);
    buf[len] = 0;
    display_line(label, buf, block_row);
}

/* While typing a number, Free42's own core never updates X (stack[sp])
 * with each keystroke -- it stays at whatever value was there before
 * entry started. The live digits being typed live in a separate buffer,
 * cmdline/cmdline_length (see keydown_number_entry()'s "draw_number:"
 * path in core_keydown.cc, which builds its own string from cmdline
 * instead of calling display_x()/vartype2string()). We do the same here:
 * show the raw entry buffer, not the stale X register, whenever
 * mode_number_entry is set. */
static void display_entry(uint8_t block_row) {
    char buf[64];
    int n = 0;
    for (int i = 0; i < cmdline_length && n < (int) sizeof(buf) - 2; i++)
        buf[n++] = cmdline[i]; /* display_line() translates the exponent marker (24) */
    buf[n++] = '_'; /* entry cursor, matching the real HP-42S/Free42 display */
    buf[n] = 0;
    display_line("X:", buf, block_row);
}

static void display_stack() {
    display_clear();
    display_reg("T:", REG_T, STACK_ROW_T);
    display_reg("Z:", REG_Z, STACK_ROW_Z);
    if (key_hint_active && key_hint_length > 0) {
        char buf[sizeof(key_hint_text)];
        memcpy(buf, key_hint_text, key_hint_length);
        buf[key_hint_length] = 0;
        display_draw_string(buf, STACK_COL_START, STACK_ROW_Y, 0x00);
    } else if (flags.f.message && status_message_length > 0) {
        /* No "Y:" label here -- this isn't Y's value, it's an override
         * message (e.g. "DIVIDE BY 0"), matching how Free42's own
         * display_error() replaces the line wholesale, unlabeled. */
        char buf[sizeof(status_message)];
        memcpy(buf, status_message, status_message_length);
        buf[status_message_length] = 0;
        display_draw_string(buf, STACK_COL_START, STACK_ROW_Y, 0x00);
    } else {
        display_reg("Y:", REG_Y, STACK_ROW_Y);
    }
    if (mode_number_entry)
        display_entry(STACK_ROW_X);
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
 * that position yet (row 7 and R0C0 aren't wired to a function yet). */
static const int key_matrix[KB_ROWS][KB_COLS] = {
        /*      C0            C1        C2        C3           C4 */
        /*R0*/ {  0,          34,       35,       36,          37 }, /*      0    .   R/S    + */
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
        memcpy(key_hint_text, cmd_array[pending_command].name, key_hint_length);
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
