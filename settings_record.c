#include "settings_record.h"

#include <stddef.h>
#include <string.h>

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

/* Versions 7 to 13: today's layout cut short before the field each one
 * lacked. Version 7's checksum came right after its settings; from 8 on
 * they ended in chars, so the struct's padding to 8 (it holds a uint64_t)
 * came first. */
static const struct {
    uint32_t version;
    size_t size;
} older[] = {
    { 13, offsetof(px_settings_t, print_delay_ms) },
    { 12, offsetof(px_settings_t, menu_style) },
    { 11, offsetof(px_settings_t, error_sound) },
    { 10, offsetof(px_settings_t, screen_print) },
    {  9, offsetof(px_settings_t, alpha_mode) },
    {  8, offsetof(px_settings_t, mini_font) },
    {  7, offsetof(px_settings_t, small_font) },
};

static size_t older_checksum_at(int i) {
    size_t size = older[i].size;
    if (older[i].version >= 8)
        size = (size + 7) & ~(size_t) 7;
    return offsetof(settings_record_t, settings) + size;
}

/* The current layout, or one of the eight before -- versions 6 and 7 too:
 * settings are only written when one changes, so a calculator can still
 * hold them. Anything older gets the defaults. */
bool settings_record_load(const settings_record_t *r, px_settings_t *out) {
    if (r->magic != SETTINGS_MAGIC)
        return false;
    if (r->version == SETTINGS_VERSION && r->checksum == checksum(r)) {
        *out = r->settings;
        return true;
    }
    for (size_t i = 0; i < sizeof(older) / sizeof(older[0]); i++) {
        if (r->version != older[i].version)
            continue;
        size_t at = older_checksum_at((int) i);
        uint32_t sum;
        memcpy(&sum, (const uint8_t *) r + at, sizeof(sum));
        if (sum != checksum_bytes(r, at))
            return false;
        memcpy(out, &r->settings, older[i].size);
        return true;
    }
    const settings_record_v6_t *r6 = (const settings_record_v6_t *) r;
    if (r6->version != 6 || r6->checksum != checksum_bytes(r6, offsetof(settings_record_v6_t, checksum)))
        return false;
    const px_settings_v6_t *o = &r6->settings;
    out->sleep_minutes = o->sleep_minutes;
    out->splash_minutes = o->splash_minutes;
    memcpy(out->splash_image, o->splash_image, sizeof(out->splash_image));
    out->printer = o->printer;
    memcpy(out->key_sound, o->key_sound, sizeof(out->key_sound));
    out->off_sound = o->off_sound;
    out->date_format = o->date_format;
    out->clock_24h = o->clock_24h;
    out->time_trim = o->time_trim;
    out->cal_drift = o->cal_drift;
    out->cal_span_h = o->cal_span_h;
    out->cal_ref_ms = o->cal_ref_ms;
    return true;
}

void settings_record_make(settings_record_t *r, const px_settings_t *s) {
    memset(r, 0, sizeof(*r));
    r->magic = SETTINGS_MAGIC;
    r->version = SETTINGS_VERSION;
    r->settings = *s;
    r->checksum = checksum(r);
}
