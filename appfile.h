#ifndef APPFILE_H
#define APPFILE_H

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

/* zlib's CRC-32, continued: crc = 0 to start. */
uint32_t app_crc32(uint32_t crc, const void *data, size_t n);

#ifdef __cplusplus
}
#endif

#endif
