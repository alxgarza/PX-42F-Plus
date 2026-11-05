/* The status bar's indicators as PNGs, for the manual: drawn white on black
 * by the firmware's own code (display_draw.c, icons.c, font_8x12) the way
 * free42_app.cc's draw_status_bar() draws them, one image each.
 *
 *   status_icons <out_dir> */
#include <stdio.h>
#include <string.h>

#include "display_fb.h"
#include "font_8x12.h"
#include "icons.h"
#include "pngwrite.h"

static uint8_t framebuffer[DISPLAY_HEIGHT][DISPLAY_STRIDE];
uint8_t (*const display_fb)[DISPLAY_STRIDE] = framebuffer;

#define BAR_H 12

static bool write_file(void *ctx, const uint8_t *data, size_t len) {
    return fwrite(data, 1, len, (FILE *) ctx) == len;
}

/* The top-left width x BAR_H of the framebuffer, as <name>.png. */
static int save(const char *dir, const char *name, int width) {
    uint8_t bits[BAR_H][DISPLAY_STRIDE];
    memcpy(bits, framebuffer, sizeof(bits));
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    FILE *f = fopen(path, "wb");
    bool ok = f != NULL && png_write_1bit(&bits[0][0], width, BAR_H, DISPLAY_STRIDE, write_file, f);
    if (f != NULL)
        fclose(f);
    if (!ok)
        fprintf(stderr, "can't write %s\n", path);
    return ok ? 0 : 1;
}

static void bar(int width) {
    display_clear();
    display_fill_rect(0, 0, (uint8_t) (width - 1), BAR_H - 1, true);
}

/* Characters of font_8x12, white, from x = 2; mirrored: left to right, as
 * the shift mark is drawn. */
static int text(const char *dir, const char *name, const char *s, bool mirrored) {
    int width = 4 + 8 * (int) strlen(s);
    bar(width);
    display_draw_string(&font_8x12, s, 2, 0, 0xFF);
    if (mirrored)
        for (int y = 0; y < BAR_H; y++)
            for (int x = 0; x < 4; x++) {
                bool a = display_get_pixel(2 + x, y), b = display_get_pixel(9 - x, y);
                display_set_pixel((uint8_t) (2 + x), (uint8_t) y, b);
                display_set_pixel((uint8_t) (9 - x), (uint8_t) y, a);
            }
    return save(dir, name, width);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <out_dir>\n", argv[0]);
        return 2;
    }
    const char *dir = argv[1];
    int errors = 0;
    errors += text(dir, "deg", "\023", false);  /* ° */
    errors += text(dir, "rad", "\007", false);  /* π */
    errors += text(dir, "grad", "G", false);
    errors += text(dir, "shift", "\r", true);   /* ↵, pointing right */
    errors += text(dir, "updn", "\136\016", false); /* ↑↓ */
    errors += text(dir, "run", "\006", false);  /* ▶ */
    errors += text(dir, "alpha_hp42s", "[ ]", false); /* ALPHA's keyboard modes */
    errors += text(dir, "alpha_lower", "[a]", false);
    errors += text(dir, "alpha_upper", "[A]", false);
    bar(10);
    icon_bell(1, 1);
    errors += save(dir, "bell", 10);
    bar(ICON_PRINTER_W + 2);
    icon_printer(1, 1);
    errors += save(dir, "printer", ICON_PRINTER_W + 2);
    for (int level = 0; level <= 4; level++) {
        char name[16];
        snprintf(name, sizeof(name), "battery%d", level);
        bar(22);
        icon_battery(2, 1, level);
        errors += save(dir, name, 22);
    }
    return errors;
}
