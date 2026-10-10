#include "settings.h"

#include <stddef.h>
#include <string.h>

#include "pico/flash.h"
#include "hardware/flash.h"
#include "watchdog_cfg.h"
#include "crash_report.h"
#include "debug_checkpoint.h"
#include "rtc.h"

/* The 4KB sector just below the 4MB mark -- a fixed address (it was the
 * last sector when the build assumed a 4MB Pico 2 W), so the saved settings
 * stay put now that the board header declares the PX-42F's real 16MB. */
#define SETTINGS_FLASH_OFFSET (4u * 1024u * 1024u - FLASH_SECTOR_SIZE)

#define SETTINGS_MAGIC   0x32345850u /* "PX42" */
#define SETTINGS_VERSION 6           /* bump when px_settings_t changes */

typedef struct {
    uint32_t magic;
    uint32_t version;
    px_settings_t settings;
    uint32_t checksum;
} settings_record_t;

/* Version 5: before Date & Time. */
typedef struct {
    uint16_t sleep_minutes;
    uint16_t splash_minutes;
    char splash_image[SPLASH_NAME_MAX + 1];
    uint8_t printer;
    char key_sound[KEY_SOUND_NAME_MAX + 1];
    bool boot_sound;
    bool off_sound;
} px_settings_v5_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    px_settings_v5_t settings;
    uint32_t checksum;
} settings_record_v5_t;

_Static_assert(sizeof(settings_record_t) <= FLASH_PAGE_SIZE, "settings record must fit one flash page");

px_settings_t px_settings;

static const px_settings_t defaults = {
    .key_sound = "",
    .boot_sound = true,
    .off_sound = true,
    .sleep_minutes = 10,  /* the HP-42S's own auto-off time */
    .splash_minutes = 10,
    .splash_image = "",
    .printer = 0, /* PRINTER_OFF */
    .date_format = RTC_YMD,
    .clock_24h = true,
};

/* FNV-1a over everything before the checksum field. */
static uint32_t checksum_bytes(const void *record, size_t checksum_offset) {
    const uint8_t *p = (const uint8_t *) record;
    uint32_t sum = 0x811C9DC5u;
    for (size_t i = 0; i < checksum_offset; i++)
        sum = (sum ^ p[i]) * 16777619u;
    return sum;
}

static uint32_t checksum(const settings_record_t *r) {
    return checksum_bytes(r, offsetof(settings_record_t, checksum));
}

static const settings_record_t *stored_record(void) {
    return (const settings_record_t *) (XIP_BASE + SETTINGS_FLASH_OFFSET);
}

/* The current layout, or the one before (version 5); anything older
 * gets the defaults. */
void settings_load(void) {
    const settings_record_t *r = stored_record();
    const settings_record_v5_t *r5 = (const settings_record_v5_t *) r;
    px_settings = defaults;
    if (r->magic == SETTINGS_MAGIC && r->version == SETTINGS_VERSION && r->checksum == checksum(r)) {
        px_settings = r->settings;
    } else if (r5->magic == SETTINGS_MAGIC && r5->version == 5
               && r5->checksum == checksum_bytes(r5, offsetof(settings_record_v5_t, checksum))) {
        px_settings.sleep_minutes = r5->settings.sleep_minutes;
        px_settings.splash_minutes = r5->settings.splash_minutes;
        memcpy(px_settings.splash_image, r5->settings.splash_image, sizeof(px_settings.splash_image));
        px_settings.printer = r5->settings.printer;
        memcpy(px_settings.key_sound, r5->settings.key_sound, sizeof(px_settings.key_sound));
        px_settings.boot_sound = r5->settings.boot_sound;
        px_settings.off_sound = r5->settings.off_sound;
    }
    px_settings.splash_image[SPLASH_NAME_MAX] = 0;
    px_settings.key_sound[KEY_SOUND_NAME_MAX] = 0;
    if (px_settings.date_format > RTC_WMD)
        px_settings.date_format = RTC_YMD;
}

static bool pending = false;

void settings_save_later(void) {
    pending = true;
}

void settings_save_pending(void) {
    if (pending)
        settings_save();
}

static void write_record(void *param) {
    flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_FLASH_OFFSET, (const uint8_t *) param, FLASH_PAGE_SIZE);
}

void settings_save(void) {
    /* Programming works in whole 256-byte pages; the rest stays erased. */
    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    settings_record_t *r = (settings_record_t *) page;
    memset(r, 0, sizeof(*r)); /* zero any struct padding, so the checksum is stable */
    r->magic = SETTINGS_MAGIC;
    r->version = SETTINGS_VERSION;
    r->settings = px_settings;
    r->checksum = checksum(r);

    pending = false;
    if (memcmp(r, stored_record(), sizeof(*r)) == 0)
        return; /* unchanged -- don't wear the flash */

    debug_checkpoint(CKPT_FLASH_WRITE, SETTINGS_FLASH_OFFSET);
    watchdog_pause();
    int rc = flash_safe_execute(write_record, page, 1000);
    watchdog_resume();
    debug_checkpoint(CKPT_FLASH_DONE, (uint32_t) rc);
    if (rc != PICO_OK)
        crash_note_flash_error(rc, SETTINGS_FLASH_OFFSET);
}
