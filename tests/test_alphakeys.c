/* The ALPHA keyboard's rules (alphakeys.c), on a computer: what each key
 * does in [ ], [a] and [A], with the ALPHA menu hidden or drawn, SHIFT or
 * not, and while typing ASTO's argument. tests/run.sh builds and runs it. */
#include <stdio.h>

#include "alphakeys.h"
#include "keymap.h"
#include "settings.h"

static int failures = 0;

/* The matrix position of an HP-42S key code. */
static void where(int code, uint8_t *row, uint8_t *col) {
    for (uint8_t r = 0; r < KB_ROWS; r++)
        for (uint8_t c = 0; c < KB_COLS; c++)
            if (key_matrix[r][c] == code) {
                *row = r;
                *col = c;
                return;
            }
    fprintf(stderr, "no key %d\n", code);
    failures++;
}

static void expect(int line, int code, alpha_state_t s, alpha_key_kind_t kind, int value, bool shift, bool to_page1) {
    uint8_t row = 0, col = 0;
    where(code, &row, &col);
    alpha_key_t k = alpha_key(row, col, &s);
    if (k.kind != kind || k.value != value || k.shift != shift || k.to_page1 != to_page1) {
        fprintf(stderr, "line %d: key %d gave kind %d value %d shift %d to_page1 %d, not %d %d %d %d\n", line, code,
                k.kind, k.value, k.shift, k.to_page1, kind, value, shift, to_page1);
        failures++;
    }
}

#define CORE(code, s, shift)           expect(__LINE__, code, s, ALPHA_KEY_CORE, code, shift, false)
#define CHAR(code, s, ch)              expect(__LINE__, code, s, ALPHA_KEY_CHAR, ch, (s).shift, false)
#define NEXT_MODE(code, s)             expect(__LINE__, code, s, ALPHA_KEY_NEXT_MODE, code, false, false)
#define PICK(code, s, shift, to_page1) expect(__LINE__, code, s, ALPHA_KEY_CORE, code, shift, to_page1)

static alpha_state_t state(uint8_t mode, bool shift, alpha_menu_t menu) {
    alpha_state_t s = {mode, shift, menu, false, 0, false};
    return s;
}

int main(void) {
    const uint8_t HP = ALPHA_MODE_HP42S, LO = ALPHA_MODE_LOWER, UP = ALPHA_MODE_UPPER;

    /* Not in ALPHA: every key is itself, in any mode. */
    CORE(HPK_UP, state(LO, false, ALPHA_MENU_NONE), false);
    CORE(HPK_RUN, state(LO, true, ALPHA_MENU_NONE), true);
    CORE(1, state(UP, false, ALPHA_MENU_NONE), false);

    /* [ ]: the HP-42S's keys; ALPHA (SHIFT ENTER) is the next mode, in
     * every mode, and SHIFT R/S is the HP-42S's again (PRGM). */
    CORE(HPK_STO, state(HP, false, ALPHA_MENU_PAGE1), false);
    CORE(HPK_UP, state(HP, true, ALPHA_MENU_PAGE1), true);
    CORE(1, state(HP, false, ALPHA_MENU_PAGE1), false);
    CORE(HPK_RUN, state(HP, true, ALPHA_MENU_PAGE1), true);
    CORE(HPK_ENTER, state(HP, false, ALPHA_MENU_PAGE1), false);
    NEXT_MODE(HPK_ENTER, state(HP, true, ALPHA_MENU_PAGE1));
    NEXT_MODE(HPK_ENTER, state(LO, true, ALPHA_MENU_GROUP));
    NEXT_MODE(HPK_ENTER, state(UP, true, ALPHA_MENU_PAGE1));
    CORE(HPK_ENTER, state(LO, true, ALPHA_MENU_NONE), true); /* outside ALPHA: ALPHA itself */

    /* [a] / [A], the first page hidden: the printed characters. */
    alpha_state_t lo = state(LO, false, ALPHA_MENU_PAGE1), up = state(UP, false, ALPHA_MENU_PAGE1);
    CHAR(1, lo, 'a');
    CHAR(1, up, 'A');
    CHAR(6, lo, 'f'); /* XEQ */
    CORE(HPK_UP, lo, false);   /* the arrows page the hidden menu */
    CORE(HPK_DOWN, up, false);
    CHAR(HPK_DOT, lo, ',');
    CHAR(HPK_RUN, lo, '?');
    CHAR(HPK_0, up, ':');
    CORE(HPK_ENTER, lo, false);
    CORE(17, lo, false); /* BSP */

    /* SHIFT: digits, symbols, ASTO / ARCL; the arrows page the shown menu. */
    alpha_state_t lo_s = state(LO, true, ALPHA_MENU_PAGE1), up_s = state(UP, true, ALPHA_MENU_PAGE1);
    CHAR(HPK_7, lo_s, '7');
    CHAR(HPK_DOT, up_s, '.');
    CHAR(HPK_DIV, lo_s, 0x00F7);
    CHAR(HPK_ADD, lo_s, '+');
    CHAR(HPK_RDN, lo_s, 0x03C0); /* pi */
    CHAR(HPK_SIN, lo_s, '(');
    CHAR(HPK_COS, lo_s, ')');
    CHAR(HPK_TAN, lo_s, '"');
    CHAR(HPK_SWAP, up_s, '<');
    CHAR(HPK_CHS, up_s, '>');
    CHAR(HPK_E, up_s, '^');
    CHAR(HPK_8, lo_s, '8'); /* its digit, not Q */
    CHAR(HPK_RUN, up_s, '?');
    CORE(HPK_STO, lo_s, false);
    CORE(HPK_RCL, up_s, false);
    CORE(HPK_UP, lo_s, false);
    CORE(HPK_DOWN, up_s, false);
    PICK(1, lo_s, true, false);  /* the shown first page: a group opens */
    PICK(3, up_s, true, false);

    /* A drawn menu: arrows page it, the top row picks without SHIFT
     * (lowercase in [a]), and a pick from a group goes back to page 1. */
    CORE(HPK_UP, state(LO, false, ALPHA_MENU_PAGE2), false);
    CORE(HPK_DOWN, state(UP, false, ALPHA_MENU_GROUP), false);
    PICK(1, state(LO, false, ALPHA_MENU_PAGE2), true, false);
    PICK(5, state(UP, false, ALPHA_MENU_PAGE2), false, false);
    PICK(2, state(LO, false, ALPHA_MENU_GROUP), true, true);
    PICK(6, state(UP, false, ALPHA_MENU_GROUP), false, true);
    PICK(4, state(UP, true, ALPHA_MENU_GROUP), true, true);
    CHAR(HPK_7, state(LO, false, ALPHA_MENU_GROUP), 'p'); /* not a soft key: its letter */

    /* ASTO / ARCL's argument: a number's first digit with SHIFT, the rest
     * plain; a name's letters plain. */
    alpha_state_t reg = state(LO, true, ALPHA_MENU_PAGE1);
    reg.reg_entry = true;
    CORE(HPK_0, reg, false);
    reg.shift = false;
    CHAR(HPK_1, reg, 'x'); /* nothing typed yet, no SHIFT: a name */
    reg.reg_length = 1;
    CORE(HPK_1, reg, false);
    reg.reg_alpha = true;
    CHAR(HPK_1, reg, 'x');

    if (failures) {
        fprintf(stderr, "%d ALPHA key checks failed\n", failures);
        return 1;
    }
    printf("ALPHA keys: all passed\n");
    return 0;
}
