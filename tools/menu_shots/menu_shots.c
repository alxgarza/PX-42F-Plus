/* Screenshots of the settings menu, made on a computer: the firmware's own
 * settings_menu.c, display_draw.c, splash.c and fonts, driven by a script
 * of key presses, with the rest of the calculator (the clock, the battery,
 * the drive's contents...) standing in with fixed, plausible values. Each
 * "shot:<name>" in the script writes the screen as last sent to the panel
 * to <out_dir>/<name>.png. See run.sh.
 *
 *   menu_shots <out_dir> <script words...>
 *
 * Script words: ENTER EXIT BSP UP DOWN SHIFT, a digit 0-9, or shot:<name>. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "battery.h"
#include "cpuclock.h"
#include "crash_report.h"
#include "display_fb.h"
#include "flashdisk.h"
#include "hardware/watchdog.h"
#include "font_10x16.h"
#include "font_6x8.h"
#include "font_8x12.h"
#include "fonts.h"
#include "free42_app.h"
#include "idle.h"
#include "keyboard.h"
#include "keyhold.h"
#include "pngwrite.h"
#include "power.h"
#include "app.h"
#include "rtc.h"
#include "settings.h"
#include "settings_menu.h"
#include "sounds.h"
#include "statefs.h"
#include "usb_msc.h"

uint64_t host_now_us = 1;
watchdog_hw_t host_watchdog;

static uint8_t framebuffer[DISPLAY_HEIGHT][DISPLAY_STRIDE];
uint8_t (*const display_fb)[DISPLAY_STRIDE] = framebuffer;

/* ===================== the panel and the script ===================== */

static uint8_t shown[DISPLAY_HEIGHT][DISPLAY_STRIDE]; /* as last flushed */
static const char *out_dir;
static char **script;
static int script_len, script_pos;

void display_flush(void) {
    memcpy(shown, framebuffer, sizeof(shown));
}

void display_set_slow_refresh(bool slow) {
}

static bool write_file(void *ctx, const uint8_t *data, size_t len) {
    return fwrite(data, 1, len, (FILE *) ctx) == len;
}

static void shot(const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", out_dir, name);
    FILE *f = fopen(path, "wb");
    if (f == NULL || !png_write_1bit(&shown[0][0], DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE, write_file, f)) {
        fprintf(stderr, "can't write %s\n", path);
        exit(1);
    }
    fclose(f);
    printf("%s\n", path);
}

/* The keyboard, as free42_app.cc's key_matrix and alpha_key_matrix. */
static const int key_matrix[KB_ROWS][KB_COLS] = {
        {1, 2, 3, 4, 5},      {7, 8, 9, 10, 11},    {13, 14, 15, 16, 17}, {18, 19, 20, 21, 22},
        {23, 24, 25, 26, 27}, {28, 29, 30, 31, 32}, {33, 34, 35, 36, 37}, {6, 12, 0, 0, 0},
};
static const char alpha_key_matrix[KB_ROWS][KB_COLS] = {
        {'A', 'B', 'C', 'D', 'E'}, {'G', 'H', 'I', 'J', 'K'}, {0, 'M', 'N', 'O', 0},   {0, 'P', 'Q', 'R', 'S'},
        {0, 'T', 'U', 'V', 'W'},   {0, 'X', 'Y', 'Z', '_'},   {0, ':', '.', '?', ' '}, {'F', 'L', 0, 0, 0},
};

int free42_key_code(uint8_t row, uint8_t col) {
    return row < KB_ROWS && col < KB_COLS ? key_matrix[row][col] : 0;
}

char free42_alpha_char(uint8_t row, uint8_t col) {
    return row < KB_ROWS && col < KB_COLS ? alpha_key_matrix[row][col] : 0;
}

static int code_of(const char *word) {
    static const struct {
        const char *name;
        int code;
    } names[] = {{"ENTER", 13}, {"BSP", 17}, {"UP", 18}, {"DOWN", 23}, {"SHIFT", 28}, {"EXIT", 33},
                 {"0", 34},     {"1", 29},   {"2", 30},  {"3", 31},    {"4", 24},     {"5", 25},
                 {"6", 26},     {"7", 19},   {"8", 20},  {"9", 21}};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcasecmp(word, names[i].name) == 0)
            return names[i].code;
    fprintf(stderr, "unknown key %s\n", word);
    exit(1);
}

/* The next key of the script, taking the shots before it; the end of the
 * script ends the program. */
bool keyboard_scan(uint8_t *row, uint8_t *col) {
    host_now_us += 1000;
    while (script_pos < script_len && strncmp(script[script_pos], "shot:", 5) == 0)
        shot(script[script_pos++] + 5);
    if (script_pos == script_len)
        exit(0);
    int code = code_of(script[script_pos++]);
    for (uint8_t r = 0; r < KB_ROWS; r++)
        for (uint8_t c = 0; c < KB_COLS; c++)
            if (key_matrix[r][c] == code) {
                *row = r;
                *col = c;
                return true;
            }
    return false;
}

bool keyboard_is_held(uint8_t row, uint8_t col) {
    return false;
}

void key_hold_begin(uint8_t row, uint8_t col) {
}

key_hold_t key_hold_poll(void) {
    return KEY_HOLD_NONE;
}

/* ===================== the calculator, standing in ===================== */

/* Wednesday, October 21 2026, 11:42:05. */
void rtc_get_datetime(rtc_datetime_t *dt) {
    *dt = (rtc_datetime_t) {2026, 10, 21, 11, 42, 5, 0, 3};
}

void rtc_get_hms(int *hour, int *min, int *sec) {
    *hour = 11;
    *min = 42;
    *sec = 5;
}

bool rtc_set_date(int year, int month, int day) {
    return true;
}

void rtc_set_hms(int hour, int min, int sec) {
}

bool rtc_set_measured(void) {
    return false;
}

void rtc_apply_trim(void) {
}

int battery_read_mv(void) {
    return 4150;
}

int battery_level(void) {
    return 4;
}

void cpuclock_set_fast(bool fast) {
}

uint32_t crash_core1_stack_peak(void) {
    return 7424;
}

void crash_note_flash_error(int rc, uint32_t flash_offset) {
}

bool idle_wait(absolute_time_t until) {
    return false;
}

bool idle_usb_host_active(void) {
    return false;
}

absolute_time_t idle_earliest(absolute_time_t until, absolute_time_t other) {
    return until < other ? until : other;
}

uint32_t flashdisk_block_count(void) {
    return 24576;
}

uint32_t flashdisk_error_count(void) {
    return 0;
}

bool usb_msc_ejected(void) {
    return false;
}

px_settings_t px_settings;

void settings_save(void) {
}

void settings_save_pending(void) {
}

bool free42_save_to_drive(void) {
    return true;
}

/* The real one never comes back: the boot ROM takes over. */
void power_update_mode(void) {
    exit(0);
}

void settings_save_later(void) {
}

void free42_set_formats(uint8_t date_format, bool clock_24h) {
}

void free42_set_printer(bool on) {
}

void free42_save_state(const char *path) {
}

void free42_load_state(const char *path) {
}

int free42_list_programs(char names[][PROGRAM_LABEL_MAX + 1], int max) {
    static const char *const programs[] = {"\"QUAD\"", "\"LOAN\" \"PMT\"", "\"PRIMES\"", ".END."};
    int n = 0;
    for (; n < 4 && n < max; n++)
        snprintf(names[n], PROGRAM_LABEL_MAX + 1, "%s", programs[n]);
    return n;
}

bool free42_export_programs(int index, const char *path) {
    return true;
}

int free42_import_programs(const char *path) {
    return 2;
}

/* The drive: a few files of each kind. */
bool statefs_ready(void) {
    return true;
}

static int list(const char *const *items, int n, char *names, size_t name_size, int max) {
    int count = 0;
    for (int i = 0; i < n && count < max; i++)
        snprintf(names + (size_t) count++ * name_size, name_size, "%s", items[i]);
    return count;
}

int statefs_list_files(const char *dir, const char *ext, char *names, size_t name_size, int max) {
    static const char *const splash[] = {"20261020-174015", "Mountains"};
    static const char *const large[] = {"Rounded"};
    static const char *const games[] = {"SNAKE"};
    static const char *const apps[] = {"IR"};
    if (strcmp(dir, STATEFS_SPLASH_DIR) == 0)
        return list(splash, 2, names, name_size, max);
    if (strcmp(dir, FONTS_DIR "/LARGE") == 0)
        return list(large, 1, names, name_size, max);
    if (strcmp(dir, GAMES_DIR) == 0)
        return list(games, 1, names, name_size, max);
    if (strcmp(dir, APPS_DIR) == 0)
        return list(apps, 1, names, name_size, max);
    static const char *const raw[] = {"ALL", "Calendar", "QUAD", "Rocket-Science"};
    if (strcmp(dir, PROGRAMS_DIR) == 0)
        return list(raw, 4, names, name_size, max);
    return 0;
}

int statefs_list_dirs(const char *dir, char *names, size_t name_size, int max) {
    return 0;
}

int statefs_list_states(char names[][STATEFS_NAME_MAX + 1], int max) {
    static const char *const states[] = {"20261020-174205", "Finance", "Physics"};
    return list(states, 3, (char *) names, STATEFS_NAME_MAX + 1, max);
}

bool statefs_state_exists(const char *name) {
    return false;
}

void statefs_state_path(const char *name, char *path, size_t size) {
    snprintf(path, size, "/STATE/%s.p42", name);
}

bool statefs_delete_state(const char *name) {
    return true;
}

bool statefs_rename_state(const char *old_name, const char *new_name) {
    return true;
}

FILE *statefs_fopen(const char *name, const char *mode) {
    return NULL;
}

bool statefs_sync(void) {
    return true;
}

bool statefs_file_exists(const char *path) {
    return false;
}

void statefs_mkdir(const char *path) {
}

bool statefs_format(void) {
    return true;
}

int statefs_remove(const char *name) {
    return 0;
}

void statefs_usb_begin(void) {
}

void statefs_usb_poll(void) {
}

void statefs_usb_end(void) {
}

int fonts_list(font_kind_t kind, char names[][FONT_NAME_MAX + 1], int max) {
    return kind == FONT_LARGE ? statefs_list_files(FONTS_DIR "/LARGE", ".png", (char *) names, FONT_NAME_MAX + 1, max) : 0;
}

display_font_t font_small, font_large, font_mini;

bool fonts_select(font_kind_t kind, const char *name, char *error, size_t error_size) {
    return true;
}

void fonts_init(void) {
    font_small = font_8x12;
    font_large = font_10x16;
    font_mini = font_6x8;
}

int sounds_key_list(char names[][SOUND_NAME_MAX + 1], int max) {
    static const char *const sounds[] = {"Typewriter", "Tick", "Tock", "Glass", "Pop", "Beep", "Free42"};
    return list(sounds, 7, (char *) names, SOUND_NAME_MAX + 1, max);
}

bool sounds_key_select(const char *name, char *error, size_t error_size) {
    return true;
}

void sounds_key_play(key_sound_t kind) {
}

void sounds_play_boot(void) {
}

void sounds_play_on(void) {
}

void sounds_play_off(void) {
}

void sounds_play_tone(int tone, bool wait) {
}

void sounds_stop(void) {
}

void sounds_wait(void) {
}

void sounds_init(bool use_files) {
}

void app_folders(void) {
}

const char *app_run(const char *dir, const char *name, bool *power_off) {
    *power_off = false;
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <out_dir> <script words...>\n", argv[0]);
        return 2;
    }
    out_dir = argv[1];
    script = argv + 2;
    script_len = argc - 2;

    px_settings = (px_settings_t) {
            .sleep_minutes = 10,
            .splash_minutes = 10,
            .key_sound = "Typewriter",
            .on_sound = true,
            .off_sound = true,
            .date_format = RTC_WMD,
            .clock_24h = false,
    };
    fonts_init();
    settings_menu_run();
    return 0;
}
