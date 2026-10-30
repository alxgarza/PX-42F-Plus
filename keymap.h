#ifndef KEYMAP_H
#define KEYMAP_H

#include "keyboard.h"

/* The keyboard, in one place for the firmware (free42_app.cc, alphakeys.c)
 * and the manual's screenshot tool (tools/menu_shots/menu_shots.c). */

/* The HP-42S key codes alphakeys.c names: the core's KEY_* (core_globals.h,
 * which only C++ can include -- free42_app.cc checks they agree). */
#define HPK_STO   7
#define HPK_RCL   8
#define HPK_RDN   9
#define HPK_SIN  10
#define HPK_COS  11
#define HPK_TAN  12
#define HPK_SWAP 14
#define HPK_CHS  15
#define HPK_E    16
#define HPK_UP   18
#define HPK_7    19
#define HPK_8    20
#define HPK_9    21
#define HPK_DIV  22
#define HPK_DOWN 23
#define HPK_4    24
#define HPK_5    25
#define HPK_6    26
#define HPK_MUL  27
#define HPK_1    29
#define HPK_2    30
#define HPK_3    31
#define HPK_SUB  32
#define HPK_EXIT 33
#define HPK_0    34
#define HPK_DOT  35
#define HPK_RUN  36
#define HPK_ADD  37

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

/* The characters printed on the keys: what they type in ALPHA in the
 * DM42's [a] and [A] modes (alphakeys.h says when). 0 keeps the key's
 * meaning: most of column 0 -- ENTER, BSP, SHIFT, EXIT -- is needed while
 * typing too. The arrows type '=' and '$' only while the ALPHA menu is
 * hidden. Also the letters the settings menu types (free42_alpha_char()). */
static const char alpha_key_matrix[KB_ROWS][KB_COLS] = {
        /*      C0   C1   C2   C3   C4 */
        /*R0*/ { 'A', 'B', 'C', 'D', 'E' },
        /*R1*/ { 'G', 'H', 'I', 'J', 'K' },
        /*R2*/ {   0, 'M', 'N', 'O',   0 },
        /*R3*/ { '=', 'P', 'Q', 'R', 'S' },
        /*R4*/ { '$', 'T', 'U', 'V', 'W' },
        /*R5*/ {   0, 'X', 'Y', 'Z', '_' },
        /*R6*/ {   0, ':', ',', '?', ' ' },
        /*R7*/ { 'F', 'L',   0,   0,   0 },
};

#endif
