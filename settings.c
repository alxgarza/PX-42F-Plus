#include "settings.h"

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

#define SETTINGS_MAGIC   0x32345850u /* "PX42" */
#define SETTINGS_VERSION 11          /* bump when px_settings_t changes */

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

/* Version 7 was today's layout without the fonts, which were added at the
 * end: its settings are the first V7_SIZE bytes, its checksum right after.
 * Versions 8, 9 and 10, likewise, without the mini font / the alpha mode /
 * the screen print --
 * their settings ended in chars, so the struct's padding to 8 (it holds a
 * uint64_t) came before the checksum. */
#define V7_SIZE        offsetof(px_settings_t, small_font)
#define V7_CHECKSUM_AT (offsetof(settings_record_t, settings) + V7_SIZE)
#define V8_SIZE        offsetof(px_settings_t, mini_font)
#define V8_CHECKSUM_AT (offsetof(settings_record_t, settings) + ((V8_SIZE + 7) & ~(size_t) 7))
#define V9_SIZE        offsetof(px_settings_t, alpha_mode)
#define V9_CHECKSUM_AT (offsetof(settings_record_t, settings) + ((V9_SIZE + 7) & ~(size_t) 7))
#define V10_SIZE        offsetof(px_settings_t, screen_print)
#define V10_CHECKSUM_AT (offsetof(settings_record_t, settings) + ((V10_SIZE + 7) & ~(size_t) 7))

/* The current layout, or one of the five before -- versions 6 and 7 too:
 * settings are only written when one changes, so a calculator can still
 * hold them. Anything older gets the defaults. */
static bool load_record(const settings_record_t *r) {
    const settings_record_v6_t *r6 = (const settings_record_v6_t *) r;
    uint32_t v7_checksum, v8_checksum, v9_checksum, v10_checksum;
    memcpy(&v7_checksum, (const uint8_t *) r + V7_CHECKSUM_AT, sizeof(v7_checksum));
    memcpy(&v8_checksum, (const uint8_t *) r + V8_CHECKSUM_AT, sizeof(v8_checksum));
    memcpy(&v9_checksum, (const uint8_t *) r + V9_CHECKSUM_AT, sizeof(v9_checksum));
    memcpy(&v10_checksum, (const uint8_t *) r + V10_CHECKSUM_AT, sizeof(v10_checksum));
    if (r->magic == SETTINGS_MAGIC && r->version == SETTINGS_VERSION && r->checksum == checksum(r)) {
        px_settings = r->settings;
    } else if (r->magic == SETTINGS_MAGIC && r->version == 10 && v10_checksum == checksum_bytes(r, V10_CHECKSUM_AT)) {
        memcpy(&px_settings, &r->settings, V10_SIZE);
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
        px_settings.off_sound = o->off_sound;
        px_settings.date_format = o->date_format;
        px_settings.clock_24h = o->clock_24h;
        px_settings.time_trim = o->time_trim;
        px_settings.cal_drift = o->cal_drift;
        px_settings.cal_span_h = o->cal_span_h;
        px_settings.cal_ref_ms = o->cal_ref_ms;
    } else {
        return false;
    }
    return true;
}

/* The newest record that reads, else the other one, else the defaults. */
void settings_load(void) {
    int newer = sequence(1) > sequence(0) ? 1 : 0;
    px_settings = defaults;
    current = newer;
    if (!load_record(stored_record())) {
        current = 1 - newer;
        if (!load_record(stored_record())) {
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
    memset(r, 0, sizeof(*r)); /* zero any struct padding, so the checksum is stable */
    r->magic = SETTINGS_MAGIC;
    r->version = SETTINGS_VERSION;
    r->settings = px_settings;
    r->checksum = checksum(r);

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
