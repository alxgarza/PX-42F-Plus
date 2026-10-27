#include "icons.h"

#include "display.h"

static void white(int x, int y) {
    display_set_pixel((uint8_t) x, (uint8_t) y, false);
}

void icon_battery(uint8_t x, uint8_t y, int level) {
    for (int c = 0; c <= 16; c++) { /* the outline */
        white(x + c, y);
        white(x + c, y + 9);
    }
    for (int r = 1; r <= 8; r++) {
        white(x, y + r);
        white(x + 1, y + r);
        white(x + 15, y + r);
        white(x + 16, y + r);
    }
    for (int r = 3; r <= 6; r++) /* the terminal */
        white(x + 17, y + r);
    for (int bar = 0; bar < level && bar < 4; bar++)
        for (int r = 2; r <= 7; r++) {
            white(x + 3 + 3 * bar, y + r);
            white(x + 4 + 3 * bar, y + r);
        }
}
