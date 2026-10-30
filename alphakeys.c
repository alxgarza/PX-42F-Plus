#include "alphakeys.h"

#include "keymap.h"
#include "settings.h"

char hp_key_digit(int code) {
    switch (code) {
        case HPK_0: return '0';
        case HPK_1: return '1';
        case HPK_2: return '2';
        case HPK_3: return '3';
        case HPK_4: return '4';
        case HPK_5: return '5';
        case HPK_6: return '6';
        case HPK_7: return '7';
        case HPK_8: return '8';
        case HPK_9: return '9';
        default:    return 0;
    }
}

/* What SHIFT + a key types when it isn't the key's own letter, as a
 * Unicode code point, which core_keydown() translates to the core's own
 * character set (divide and multiply become its characters 0 and 1).
 * 0 = the letter. */
static int shifted_char(int code) {
    if (hp_key_digit(code))
        return hp_key_digit(code);
    switch (code) {
        case HPK_DIV:  return 0x00F7; /* division sign */
        case HPK_MUL:  return 0x00D7; /* multiplication sign */
        case HPK_ADD:  return '+';
        case HPK_SUB:  return '-';
        case HPK_DOT:  return '.';
        /* The characters only the ALPHA menu has, the pairs side by side:
         * ( ) on SIN COS, < > on X<>Y CHS, ^ on E (the exponent), pi on R-down. */
        case HPK_RDN:  return 0x03C0; /* pi */
        case HPK_SIN:  return '(';
        case HPK_COS:  return ')';
        case HPK_TAN:  return '"';
        case HPK_SWAP: return '<';
        case HPK_CHS:  return '>';
        case HPK_E:    return '^';
        default:       return 0;
    }
}

bool alpha_menu_hidden(const alpha_state_t *s) {
    return s->mode != ALPHA_MODE_HP42S && s->menu == ALPHA_MENU_PAGE1 && !s->shift;
}

alpha_key_t alpha_key(uint8_t row, uint8_t col, const alpha_state_t *s) {
    int code = row < KB_ROWS && col < KB_COLS ? key_matrix[row][col] : 0;
    alpha_key_t k = {ALPHA_KEY_CORE, code, s->shift, false};
    if (s->menu == ALPHA_MENU_NONE)
        return k;
    if (s->shift && code == HPK_RUN) {
        k.kind = ALPHA_KEY_NEXT_MODE;
        k.shift = false;
        return k;
    }
    if (s->mode == ALPHA_MODE_HP42S)
        return k;

    /* Keys that reach the core as themselves, without SHIFT: a register
     * number's digits, ASTO / ARCL, an arrow paging a drawn menu (or the
     * one SHIFT shows). */
    if ((hp_key_digit(code) && s->reg_entry && (s->reg_length == 0 ? s->shift : !s->reg_alpha))
            || (s->shift && (code == HPK_STO || code == HPK_RCL))
            || ((code == HPK_UP || code == HPK_DOWN) && !alpha_menu_hidden(s))) {
        k.shift = false;
        return k;
    }

    /* A soft key with the menu drawn: the core picks from it. */
    if (code >= 1 && code <= 6 && !alpha_menu_hidden(s)) {
        k.shift = s->shift || s->mode == ALPHA_MODE_LOWER;
        k.to_page1 = s->menu == ALPHA_MENU_GROUP;
        return k;
    }

    int ch = row < KB_ROWS && col < KB_COLS ? (unsigned char) alpha_key_matrix[row][col] : 0;
    if (s->shift && shifted_char(code))
        ch = shifted_char(code);
    if (ch == 0)
        return k;
    if (s->mode == ALPHA_MODE_LOWER && ch >= 'A' && ch <= 'Z')
        ch += 'a' - 'A';
    k.kind = ALPHA_KEY_CHAR;
    k.value = ch;
    return k;
}
