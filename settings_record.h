#ifndef SETTINGS_RECORD_H
#define SETTINGS_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The settings as stored in flash (settings.c writes and reads the sectors):
 * the record's layout, and reading the layouts older firmware wrote. Kept
 * free of hardware for tests/run.sh (tests/test_settings.c). */

#define SETTINGS_MAGIC   0x32345850u /* "PX42" */
#define SETTINGS_VERSION 15          /* bump when px_settings_t changes, and add the old one to older[] */

typedef struct {
    uint32_t magic;
    uint32_t version;
    px_settings_t settings;
    uint32_t checksum;
} settings_record_t;

/* A record of s, today's layout: zeroed first (the padding too, so the
 * checksum is stable), then filled and checksummed. */
void settings_record_make(settings_record_t *r, const px_settings_t *s);

/* Reads r into out, which holds the defaults: a record from older firmware
 * fills only what it has. False (out untouched) if r isn't a good record of
 * today's layout or one of the older ones. */
bool settings_record_load(const settings_record_t *r, px_settings_t *out);

#ifdef __cplusplus
}
#endif

#endif
