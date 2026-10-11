#include "fonts.h"

#include <stdio.h>
#include <string.h>

#include "hardware/watchdog.h"
#include "font_8x12.h"
#include "font_10x16.h"
#include "fontsheet.h"
#include "settings.h"
#include "statefs.h"

/* A font from the drive lives here; the built-in ones stay in flash. */
static uint8_t small_glyphs[FONT_SHEET_GLYPHS * 12];
static uint8_t large_glyphs[FONT_SHEET_GLYPHS * 20];

display_font_t font_small, font_large;

static const struct {
    display_font_t *font;
    const display_font_t *builtin;
    uint8_t *glyphs;
    int gap; /* pixels between the sheet's cells (fontsheet.h) */
    const char *dir;
    char *setting;
} kinds[FONT_KINDS] = {
        [FONT_SMALL] = {&font_small, &font_8x12, small_glyphs, 0, FONTS_DIR "/SMALL", px_settings.small_font},
        [FONT_LARGE] = {&font_large, &font_10x16, large_glyphs, 1, FONTS_DIR "/LARGE", px_settings.large_font},
};

_Static_assert(sizeof(small_glyphs) == FONT_SHEET_GLYPHS * ((8 * 12 + 7) / 8), "small font: 8 x 12");
_Static_assert(sizeof(large_glyphs) == FONT_SHEET_GLYPHS * ((10 * 16 + 7) / 8), "large font: 10 x 16");

static int file_read(void *ctx, uint8_t *buf, int len) {
    size_t n = fread(buf, 1, (size_t) len, (FILE *) ctx);
    return n == 0 && ferror((FILE *) ctx) ? -1 : (int) n;
}

static void feed_watchdog(void) {
    watchdog_update();
}

bool fonts_select(font_kind_t kind, const char *name, char *error, size_t error_size) {
    const display_font_t *builtin = kinds[kind].builtin;
    if (name[0] == 0) {
        *kinds[kind].font = *builtin;
        return true;
    }
    char path[64];
    snprintf(path, sizeof(path), "%s/%s.png", kinds[kind].dir, name);
    FILE *f = statefs_fopen(path, "rb");
    if (f == NULL) {
        snprintf(error, error_size, "can't open the file");
        return false;
    }
    png_source_t src = {file_read, f, feed_watchdog};
    bool ok = font_sheet_load(&src, builtin->width, builtin->height, kinds[kind].gap, kinds[kind].glyphs, error,
                              error_size);
    fclose(f);
    if (ok) {
        *kinds[kind].font = *builtin; /* the same size and spacing: only the glyphs differ */
        kinds[kind].font->data = kinds[kind].glyphs;
    }
    return ok;
}

int fonts_list(font_kind_t kind, char names[][FONT_NAME_MAX + 1], int max) {
    return statefs_list_files(kinds[kind].dir, ".png", (char *) names, FONT_NAME_MAX + 1, max);
}

void fonts_init(void) {
    if (statefs_ready()) { /* the folders to put font sheets in */
        statefs_mkdir(FONTS_DIR);
        for (int kind = 0; kind < FONT_KINDS; kind++)
            statefs_mkdir(kinds[kind].dir);
    }
    for (int kind = 0; kind < FONT_KINDS; kind++) {
        char error[40];
        if (!fonts_select((font_kind_t) kind, kinds[kind].setting, error, sizeof(error))) {
            printf("Font \"%s\": %s -- using the built-in font.\n", kinds[kind].setting, error);
            fonts_select((font_kind_t) kind, "", error, sizeof(error));
        }
    }
}
