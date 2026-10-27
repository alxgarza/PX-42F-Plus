#include "fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/watchdog.h"
#include "font_8x12.h"
#include "font_10x16.h"
#include "font_6x8.h"
#include "fontsheet.h"
#include "pngwrite.h"
#include "settings.h"
#include "statefs.h"

/* A font from the drive lives here; the built-in ones stay in flash. */
static uint8_t small_glyphs[FONT_SHEET_GLYPHS * 12];
static uint8_t large_glyphs[FONT_SHEET_GLYPHS * 20];
static uint8_t mini_glyphs[FONT_SHEET_GLYPHS * 6];

display_font_t font_small, font_large, font_mini;

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
        [FONT_MINI] = {&font_mini, &font_6x8, mini_glyphs, 0, FONTS_DIR "/MINI", px_settings.mini_font},
};

_Static_assert(sizeof(small_glyphs) == FONT_SHEET_GLYPHS * ((8 * 12 + 7) / 8), "small font: 8 x 12");
_Static_assert(sizeof(large_glyphs) == FONT_SHEET_GLYPHS * ((10 * 16 + 7) / 8), "large font: 10 x 16");
_Static_assert(sizeof(mini_glyphs) == FONT_SHEET_GLYPHS * ((6 * 8 + 7) / 8), "mini font: 6 x 8");

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

static bool file_write(void *ctx, const uint8_t *data, size_t len) {
    return fwrite(data, 1, len, (FILE *) ctx) == len;
}

/* png_write_1bit()'s output checked against a file instead of written. */
static bool file_compare(void *ctx, const uint8_t *data, size_t len) {
    uint8_t buf[64];
    while (len > 0) {
        size_t n = len < sizeof(buf) ? len : sizeof(buf);
        if (fread(buf, 1, n, (FILE *) ctx) != n || memcmp(buf, data, n) != 0)
            return false;
        data += n;
        len -= n;
    }
    return true;
}

/* /FONTS/BUILTIN/LARGE.png etc.: the built-in font as a sheet, to copy to a
 * computer and start a new font from. Read-only, and outside LARGE/SMALL/
 * MINI so Settings -> Fonts doesn't list the built-in font twice. Written
 * only when it isn't there or isn't the built-in font any more (a new
 * firmware's font, or a computer changed it): most boots only read it. */
static void write_builtin_sheet(int kind) {
    const display_font_t *font = kinds[kind].builtin;
    int gap = kinds[kind].gap;
    int width = FONT_SHEET_WIDTH(font->width, gap), height = FONT_SHEET_HEIGHT(font->height, gap);
    int stride = (width + 7) / 8;
    uint8_t *sheet = (uint8_t *) malloc((size_t) stride * (size_t) height);
    if (sheet == NULL)
        return;
    font_sheet_draw(font, gap, sheet, stride);

    char path[48];
    snprintf(path, sizeof(path), "%s/%s.png", FONTS_BUILTIN_DIR, kinds[kind].dir + sizeof(FONTS_DIR));
    FILE *f = statefs_fopen(path, "rb");
    bool same = f != NULL && png_write_1bit(sheet, width, height, stride, file_compare, f) && fgetc(f) == EOF;
    if (f != NULL)
        fclose(f);
    if (!same) {
        statefs_set_readonly(path, false);
        f = statefs_fopen(path, "wb");
        bool ok = f != NULL && png_write_1bit(sheet, width, height, stride, file_write, f);
        if (f != NULL)
            ok = fclose(f) == 0 && ok;
        if (ok)
            statefs_set_readonly(path, true);
        else
            statefs_remove(path); /* half a file */
        statefs_sync();
    }
    free(sheet);
}

void fonts_init(void) {
    if (statefs_ready()) { /* the folders to put font sheets in */
        statefs_mkdir(FONTS_DIR);
        statefs_mkdir(FONTS_BUILTIN_DIR);
        for (int kind = 0; kind < FONT_KINDS; kind++) {
            statefs_mkdir(kinds[kind].dir);
            write_builtin_sheet(kind);
        }
    }
    for (int kind = 0; kind < FONT_KINDS; kind++) {
        char error[40];
        if (!fonts_select((font_kind_t) kind, kinds[kind].setting, error, sizeof(error))) {
            printf("Font \"%s\": %s -- using the built-in font.\n", kinds[kind].setting, error);
            fonts_select((font_kind_t) kind, "", error, sizeof(error));
        }
    }
}
