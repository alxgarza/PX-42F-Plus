#include "settings.h"
#include "settings_record.h"

#include <stddef.h>
#include <string.h>

#include "pico/flash.h"
#include "hardware/flash.h"
#include "watchdog_cfg.h"
#include "crash_report.h"
#include "debug_checkpoint.h"
#include "rtc.h"
#include "printer.h"

/* Two 4KB sectors just below the 4MB mark, written in turn: a save erases
 * and writes the one that doesn't hold the newest record, so a power cut
 * in the middle (a few ms) leaves the other one -- one erased sector would
 * have lost every setting. The first is the one older firmware used alone
 * (the last sector when the build assumed a 4MB Pico 2 W). flashdisk.c's
 * IMAGE_LIMIT keeps the program below both. */
#define SETTINGS_SECTORS 2
static const uint32_t sector_offset[SETTINGS_SECTORS] = {
    4u * 1024u * 1024u - FLASH_SECTOR_SIZE,
    4u * 1024u * 1024u - 2 * FLASH_SECTOR_SIZE,
};

/* Which record is newer: a count at the end of the record's page, and its
 * complement next to it -- unwritten (both 0xFF..., older firmware's
 * record, or an erased sector) counts as 0. */
#define SEQ_AT     (FLASH_PAGE_SIZE - 4)
#define SEQ_INV_AT (FLASH_PAGE_SIZE - 8)


_Static_assert(sizeof(settings_record_t) <= SEQ_INV_AT, "settings record and its count must fit one flash page");

px_settings_t px_settings;

static const px_settings_t defaults = {
    .key_sound = "",
    .on_sound = true,
    .off_sound = true,
    .sleep_minutes = 10,  /* the HP-42S's own auto-off time */
    .splash_minutes = 10,
    .splash_image = "",
    .printer = 0, /* PRINTER_OFF */
    .date_format = RTC_YMD,
    .clock_24h = true,
    .small_font = "",
    .large_font = "",
    .mini_font = "",
    .alpha_mode = ALPHA_MODE_HP42S, /* the DM42's first time too */
    .screen_print = SCREEN_PRINT_UPRIGHT,
    .error_sound = true,
    .print_delay_ms = PRINT_DELAY_DEFAULT_MS,
    .splash_clock = true, /* as it always was */
};


static const uint8_t *sector_page(int i) {
    return (const uint8_t *) (XIP_BASE + sector_offset[i]);
}

static uint32_t sequence(int i) {
    uint32_t seq, inv;
    memcpy(&seq, sector_page(i) + SEQ_AT, sizeof(seq));
    memcpy(&inv, sector_page(i) + SEQ_INV_AT, sizeof(inv));
    return inv == ~seq ? seq : 0;
}

/* The sector the settings were last loaded from or saved to. */
static int current = 0;

static const settings_record_t *stored_record(void) {
    return (const settings_record_t *) sector_page(current);
}

/* The newest record that reads, else the other one, else the defaults. */
void settings_load(void) {
    int newer = sequence(1) > sequence(0) ? 1 : 0;
    px_settings = defaults;
    current = newer;
    if (!settings_record_load(stored_record(), &px_settings)) {
        current = 1 - newer;
        if (!settings_record_load(stored_record(), &px_settings)) {
            px_settings = defaults;
            current = newer;
        }
    }
    px_settings.splash_image[SPLASH_NAME_MAX] = 0;
    px_settings.key_sound[KEY_SOUND_NAME_MAX] = 0;
    px_settings.small_font[FONT_SETTING_MAX] = 0;
    px_settings.large_font[FONT_SETTING_MAX] = 0;
    px_settings.mini_font[FONT_SETTING_MAX] = 0;
    if (px_settings.date_format > RTC_WMD)
        px_settings.date_format = RTC_YMD;
    if (px_settings.alpha_mode >= ALPHA_MODES)
        px_settings.alpha_mode = ALPHA_MODE_HP42S;
    if (px_settings.screen_print >= SCREEN_PRINTS)
        px_settings.screen_print = SCREEN_PRINT_UPRIGHT;
    if (px_settings.menu_style >= MENU_STYLES)
        px_settings.menu_style = MENU_STYLE_ICONS;
    if (px_settings.print_delay_ms > PRINT_DELAY_MAX_MS)
        px_settings.print_delay_ms = PRINT_DELAY_DEFAULT_MS;
}

static bool pending = false;

void settings_save_later(void) {
    pending = true;
}

void settings_save_pending(void) {
    if (pending)
        settings_save();
}

static uint32_t write_offset;

static void write_record(void *param) {
    flash_range_erase(write_offset, FLASH_SECTOR_SIZE);
    flash_range_program(write_offset, (const uint8_t *) param, FLASH_PAGE_SIZE);
}

void settings_save(void) {
    /* Programming works in whole 256-byte pages; the rest stays erased. */
    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    settings_record_t *r = (settings_record_t *) page;
    settings_record_make(r, &px_settings);

    pending = false;
    if (memcmp(r, stored_record(), sizeof(*r)) == 0)
        return; /* unchanged -- don't wear the flash */

    uint32_t seq = sequence(current) + 1;
    uint32_t inv = ~seq;
    memcpy(page + SEQ_AT, &seq, sizeof(seq));
    memcpy(page + SEQ_INV_AT, &inv, sizeof(inv));
    int target = 1 - current;
    write_offset = sector_offset[target];
    debug_checkpoint(CKPT_FLASH_WRITE, write_offset);
    watchdog_pause();
    int rc = flash_safe_execute(write_record, page, 1000);
    watchdog_resume();
    debug_checkpoint(CKPT_FLASH_DONE, (uint32_t) rc);
    if (rc != PICO_OK)
        crash_note_flash_error(rc, write_offset);
    else
        current = target;
}
