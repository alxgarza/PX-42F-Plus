/* The settings record (settings_record.c): today's layout, and the older
 * ones a calculator may still hold -- settings are only written when one
 * changes, so an update has to read whatever the last firmware wrote. A
 * wrong row in older[] resets everyone's settings at the update. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "settings_record.h"

static int failures;

#define CHECK(cond, ...)                 \
    do {                                 \
        if (!(cond)) {                   \
            printf("FAIL: " __VA_ARGS__); \
            printf("\n");                \
            failures++;                  \
        }                                \
    } while (0)

/* Each older version: the field it didn't have yet. */
static const struct {
    uint32_t version;
    size_t cut;
} history[] = {
    {13, offsetof(px_settings_t, print_delay_ms)},
    {12, offsetof(px_settings_t, menu_style)},
    {11, offsetof(px_settings_t, error_sound)},
    {10, offsetof(px_settings_t, screen_print)},
    {9, offsetof(px_settings_t, alpha_mode)},
    {8, offsetof(px_settings_t, mini_font)},
    {7, offsetof(px_settings_t, small_font)},
};

static uint32_t fnv(const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *) p;
    uint32_t sum = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++)
        sum = (sum ^ b[i]) * 16777619u;
    return sum;
}

/* Settings with every byte set, so a field read from the wrong place shows. */
static void filled(px_settings_t *s, uint8_t seed) {
    uint8_t *b = (uint8_t *) s;
    for (size_t i = 0; i < sizeof(*s); i++)
        b[i] = (uint8_t) (seed + i * 7);
}

/* A record as older firmware wrote it: today's layout cut short at cut, its
 * checksum right after -- from version 8 on, after the padding to 8 (the
 * struct holds a uint64_t). */
static void old_record(settings_record_t *r, uint32_t version, size_t cut, const px_settings_t *s) {
    memset(r, 0, sizeof(*r));
    r->magic = SETTINGS_MAGIC;
    r->version = version;
    memcpy(&r->settings, s, cut);
    size_t at = offsetof(settings_record_t, settings) + (version >= 8 ? (cut + 7) & ~(size_t) 7 : cut);
    uint32_t sum = fnv(r, at);
    memcpy((uint8_t *) r + at, &sum, sizeof(sum));
}

int main(void) {
    CHECK(SETTINGS_VERSION == 14,
          "SETTINGS_VERSION is %d: add the version before it to older[] (settings_record.c) and to history[] here",
          SETTINGS_VERSION);

    px_settings_t s, defaults, out;
    filled(&s, 1);
    filled(&defaults, 99);
    settings_record_t r;

    /* Today's layout: everything back. */
    settings_record_make(&r, &s);
    out = defaults;
    CHECK(settings_record_load(&r, &out) && memcmp(&out, &s, sizeof(s)) == 0, "today's record reads back");

    /* Damaged, or not a record: refused, the defaults left. */
    settings_record_t bad = r;
    ((uint8_t *) &bad.settings)[5] ^= 1;
    out = defaults;
    CHECK(!settings_record_load(&bad, &out) && memcmp(&out, &defaults, sizeof(out)) == 0, "a changed byte refused");
    bad = r;
    bad.magic = 0xFFFFFFFFu; /* an erased sector */
    CHECK(!settings_record_load(&bad, &out), "an erased sector refused");
    bad = r;
    bad.version = SETTINGS_VERSION + 1;
    CHECK(!settings_record_load(&bad, &out), "a newer firmware's record refused");
    bad.version = 5;
    CHECK(!settings_record_load(&bad, &out), "version 5 refused: too old");

    /* Each older layout: what it had, and the defaults for the rest. */
    for (size_t i = 0; i < sizeof(history) / sizeof(history[0]); i++) {
        uint32_t v = history[i].version;
        size_t cut = history[i].cut;
        old_record(&r, v, cut, &s);
        out = defaults;
        bool ok = settings_record_load(&r, &out);
        CHECK(ok, "version %u doesn't load", (unsigned) v);
        CHECK(!ok || memcmp(&out, &s, cut) == 0, "version %u: its settings not read back", (unsigned) v);
        CHECK(!ok || memcmp((uint8_t *) &out + cut, (uint8_t *) &defaults + cut, sizeof(out) - cut) == 0,
              "version %u: the newer settings aren't the defaults", (unsigned) v);
    }
    /* The version just before today's must be one of them. */
    CHECK(history[0].version == SETTINGS_VERSION - 1, "the version before today's isn't tested");

    printf(failures ? "Settings: %d FAILED\n" : "Settings: all passed\n", failures);
    return failures != 0;
}
