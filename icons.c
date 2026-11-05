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

void icon_bell(uint8_t x, uint8_t y) {
    static const uint8_t rows[10] = {0x18, 0x3C, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0xFF, 0x00, 0x18};
    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 8; c++)
            if (rows[r] & (0x80 >> c))
                white(x + c, y + r);
}

void icon_printer(uint8_t x, uint8_t y) {
    /* The dots of annunciator 3 in Plus42's skin (skins/Plus42.gif). */
    static const uint32_t rows[7] = {0x07C000, 0x0FFC00, 0x1FFFC0, 0x1FFFFC, 0x0FDFFF, 0x0FC07F, 0x033F00};
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < ICON_PRINTER_W; c++)
            if (rows[r] & (1u << (ICON_PRINTER_W - 1 - c)))
                white(x + c, y + 2 + r);
}
