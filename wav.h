#ifndef WAV_H
#define WAV_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reading WAV files -- the format and where the samples are -- from a
 * FILE or from memory (the built-in sounds). Standard C only, so it builds
 * on a computer too, for tests/test_parsers.c. The rest -- normalizing,
 * converting, playing -- is sounds.c's. */

typedef struct {
    FILE *f;            /* a file, or ... */
    const uint8_t *mem; /* ... bytes in memory */
    uint32_t size;      /* the real size, either way -- never the header's word for it */
    uint32_t pos;
} wav_source_t;

/* A source on an open file (measures its size); on memory. */
bool wav_source_file(wav_source_t *s, FILE *f);
wav_source_t wav_source_mem(const uint8_t *data, uint32_t size);

uint32_t wav_read(wav_source_t *s, void *buf, uint32_t n);
bool wav_seek(wav_source_t *s, uint32_t pos);
void wav_close(wav_source_t *s); /* closes the file, if it's one */

typedef struct {
    uint16_t format; /* 1 = integer PCM, 3 = float */
    uint16_t channels, bits, frame_bytes;
    uint32_t rate;
    uint32_t data_pos, frames; /* frames: only as many as the file really holds */
} wav_t;

/* Finds the format and the sample data, and leaves the source at the
 * data. PCM 8/16/24/32-bit or 32-bit float, 1-8 channels, 4-192kHz.
 * False, with the reason in *error, for anything else or a damaged file. */
bool wav_open(wav_source_t *s, wav_t *w, const char **error);

/* One sample of the frame at p, as -1..+1. */
float wav_sample(const wav_t *w, const uint8_t *p);

#ifdef __cplusplus
}
#endif

#endif
