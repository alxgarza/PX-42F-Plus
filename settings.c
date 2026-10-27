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
#define SETTINGS_VERSION 10          /* bump when px_settings_t changes */

typedef struct {
    uint32_t magic;
    uint32_t version;
    px_settings_t settings;
    uint32_t checksum;
} settings_record_t;

/* Version 6: before the on sound. */
typedef struct {
    uint16_t sleep_minutes;
    uint16_t splash_minutes;
    char splash_image[SPLASH_NAME_MAX + 1];
    uint8_t printer;
    char key_sound[KEY_SOUND_NAME_MAX + 1];
    bool boot_sound;
    bool off_sound;
    uint8_t date_format;
    bool clock_24h;
    int16_t time_trim;
    int16_t cal_drift;
    uint16_t cal_span_h;
    uint64_t cal_ref_ms;
} px_settings_v6_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    px_settings_v6_t settings;
    uint32_t checksum;
} settings_record_v6_t;

_Static_assert(sizeof(settings_record_t) <= FLASH_PAGE_SIZE, "settings record must fit one flash page");

px_settings_t px_settings;

static const px_settings_t defaults = {
    .key_sound = "",
    .boot_sound = true,
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

/* Version 7 was today's layout without the fonts, which were added at the
 * end: its settings are the first V7_SIZE bytes, its checksum right after.
 * Versions 8 and 9, likewise, without the mini font / the alpha mode --
 * their settings ended in chars, so the struct's padding to 8 (it holds a
 * uint64_t) came before the checksum. */
#define V7_SIZE        offsetof(px_settings_t, small_font)
#define V7_CHECKSUM_AT (offsetof(settings_record_t, settings) + V7_SIZE)
#define V8_SIZE        offsetof(px_settings_t, mini_font)
#define V8_CHECKSUM_AT (offsetof(settings_record_t, settings) + ((V8_SIZE + 7) & ~(size_t) 7))
#define V9_SIZE        offsetof(px_settings_t, alpha_mode)
#define V9_CHECKSUM_AT (offsetof(settings_record_t, settings) + ((V9_SIZE + 7) & ~(size_t) 7))

/* The current layout, or one of the four before -- versions 6 and 7 too:
 * settings are only written when one changes, so a calculator can still
 * hold them. Anything older gets the defaults. */
void settings_load(void) {
    const settings_record_t *r = stored_record();
    const settings_record_v6_t *r6 = (const settings_record_v6_t *) r;
    uint32_t v7_checksum, v8_checksum, v9_checksum;
    memcpy(&v7_checksum, (const uint8_t *) r + V7_CHECKSUM_AT, sizeof(v7_checksum));
    memcpy(&v8_checksum, (const uint8_t *) r + V8_CHECKSUM_AT, sizeof(v8_checksum));
    memcpy(&v9_checksum, (const uint8_t *) r + V9_CHECKSUM_AT, sizeof(v9_checksum));
    px_settings = defaults;
    if (r->magic == SETTINGS_MAGIC && r->version == SETTINGS_VERSION && r->checksum == checksum(r)) {
        px_settings = r->settings;
    } else if (r->magic == SETTINGS_MAGIC && r->version == 9 && v9_checksum == checksum_bytes(r, V9_CHECKSUM_AT)) {
        memcpy(&px_settings, &r->settings, V9_SIZE);
    } else if (r->magic == SETTINGS_MAGIC && r->version == 8 && v8_checksum == checksum_bytes(r, V8_CHECKSUM_AT)) {
        memcpy(&px_settings, &r->settings, V8_SIZE);
    } else if (r->magic == SETTINGS_MAGIC && r->version == 7 && v7_checksum == checksum_bytes(r, V7_CHECKSUM_AT)) {
        memcpy(&px_settings, &r->settings, V7_SIZE);
    } else if (r6->magic == SETTINGS_MAGIC && r6->version == 6
               && r6->checksum == checksum_bytes(r6, offsetof(settings_record_v6_t, checksum))) {
        const px_settings_v6_t *o = &r6->settings;
        px_settings.sleep_minutes = o->sleep_minutes;
        px_settings.splash_minutes = o->splash_minutes;
        memcpy(px_settings.splash_image, o->splash_image, sizeof(px_settings.splash_image));
        px_settings.printer = o->printer;
        memcpy(px_settings.key_sound, o->key_sound, sizeof(px_settings.key_sound));
        px_settings.boot_sound = o->boot_sound;
        px_settings.off_sound = o->off_sound;
        px_settings.date_format = o->date_format;
        px_settings.clock_24h = o->clock_24h;
        px_settings.time_trim = o->time_trim;
        px_settings.cal_drift = o->cal_drift;
        px_settings.cal_span_h = o->cal_span_h;
        px_settings.cal_ref_ms = o->cal_ref_ms;
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
