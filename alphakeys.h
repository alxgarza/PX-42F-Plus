#ifndef ALPHAKEYS_H
#define ALPHAKEYS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What a key does while the core's ALPHA menu is up, in the keyboard's
 * three modes (px_settings.alpha_mode, as on the DM42): [ ] every key is
 * the HP-42S's, letters come from the ALPHA menu; [a] and [A] each key
 * types the character printed on it (keymap.h's alpha_key_matrix), lower
 * or upper case. No core calls here, so tests/test_alphakeys.c runs it on
 * a computer; free42_app.cc describes the core's state and does what it
 * says.
 *
 * In every mode, ALPHA (SHIFT then ENTER): the next mode -- not out of
 * ALPHA, as on the HP-42S; EXIT leaves it.
 *
 * In [a] and [A]:
 *   - the ALPHA menu's first page (the groups of letters) is hidden
 *     (alpha_menu_hidden()); SHIFT shows it. The arrows page the menu,
 *     SHIFT or not. The second page and a group's menu are drawn: the top
 *     row picks without SHIFT, and a pick from a group goes back to the
 *     hidden first page;
 *   - a picked letter is lowercase in [a] (SHIFT set for the core), a
 *     capital in [A] -- lowercase with SHIFT;
 *   - R/S types '?', with SHIFT too (to run a program, leave ALPHA);
 *   - SHIFT then a digit key: the digit; the four arithmetic keys: their
 *     symbols; the dot key: '.' (alone it types ',', as on the HP-41C);
 *     I J K L M N O: pi ( ) " < > ^; STO and RCL: ASTO and ARCL (the core's
 *     STO and RCL in ALPHA); any other letter key: the same letter;
 *   - after ASTO or ARCL, a register number: its first digit with SHIFT
 *     (alone the keys are letters, for a name), the rest without. */

typedef enum {
    ALPHA_MENU_NONE,  /* not in ALPHA */
    ALPHA_MENU_PAGE1, /* the ALPHA menu's first page */
    ALPHA_MENU_PAGE2, /* its second page */
    ALPHA_MENU_GROUP, /* a group's menu: ABCDE, PUNC, MATH... */
} alpha_menu_t;

typedef struct {
    uint8_t mode;      /* alpha_mode_t (settings.h) */
    bool shift;        /* SHIFT is set */
    alpha_menu_t menu; /* the front menu */
    bool reg_entry;    /* typing ASTO's or ARCL's argument */
    int reg_length;    /* how much of it is typed */
    bool reg_alpha;    /* it's a name, not a register number */
} alpha_state_t;

typedef enum {
    ALPHA_KEY_CORE,      /* the HP-42S key code to the core (0: none) */
    ALPHA_KEY_CHAR,      /* a character to type (the core's 1024 + it) */
    ALPHA_KEY_NEXT_MODE, /* the next keyboard mode */
} alpha_key_kind_t;

typedef struct {
    alpha_key_kind_t kind;
    int value;     /* the key code, or the character as Unicode */
    bool shift;    /* SHIFT, for the core, with it */
    bool to_page1; /* after the core has it: back to the ALPHA menu's first page */
} alpha_key_t;

/* The key at (row, col), pressed in state s. */
alpha_key_t alpha_key(uint8_t row, uint8_t col, const alpha_state_t *s);

/* The ALPHA menu isn't drawn (and the top row types letters). */
bool alpha_menu_hidden(const alpha_state_t *s);

/* '0'-'9' for the HP-42S key code of a digit key, else 0. */
char hp_key_digit(int code);

#ifdef __cplusplus
}
#endif

#endif
