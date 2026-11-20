#ifndef APPFILE_H
#define APPFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "px_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Checking a .bin from the drive before it runs (app.c) -- kept free of
 * hardware for tests/run.sh: a file someone dropped on the drive must
 * never be jumped into unless it is what it says. */

/* NULL if a file of file_size bytes with this header can run, or why not
 * (at most 28 characters, for a line of the menu). */
const char *app_header_check(const px_app_header_t *h, uint32_t file_size);

/* Where an app with this header runs, and how much RAM it has there: an
 * app built for API 9 on gets PX_APP_BASE and PX_APP_RAM; an older one
 * the 128 KB at the top it was linked for. */
#define APP_OLD_BASE 0x20060000u
#define APP_OLD_RAM  0x20000u
uint32_t app_base(const px_app_header_t *h);
uint32_t app_ram(const px_app_header_t *h);

/* The icon of a .bin whose first n bytes are head (at least
 * sizeof(px_app_header_t) + sizeof(px_app_icon_t) for one to be found):
 * into bits, true; false if it has none. */
bool app_icon_from(const uint8_t *head, size_t n, uint8_t bits[sizeof(((px_app_icon_t *) 0)->bits)]);

/* zlib's CRC-32, continued: crc = 0 to start. */
uint32_t app_crc32(uint32_t crc, const void *data, size_t n);

#ifdef __cplusplus
}
#endif

#endif
