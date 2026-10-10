#include "wav.h"

#include <string.h>

bool wav_source_file(wav_source_t *s, FILE *f) {
    memset(s, 0, sizeof(*s));
    s->f = f;
    if (fseek(f, 0, SEEK_END) != 0)
        return false;
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0)
        return false;
    s->size = (uint32_t) size;
    return true;
}

wav_source_t wav_source_mem(const uint8_t *data, uint32_t size) {
    wav_source_t s = {NULL, data, size, 0};
    return s;
}

uint32_t wav_read(wav_source_t *s, void *buf, uint32_t n) {
    if (n > s->size - s->pos)
        n = s->size - s->pos;
    if (s->f != NULL)
        n = (uint32_t) fread(buf, 1, n, s->f);
    else
        memcpy(buf, s->mem + s->pos, n);
    s->pos += n;
    return n;
}

bool wav_seek(wav_source_t *s, uint32_t pos) {
    if (pos > s->size)
        return false;
    if (s->f != NULL && fseek(s->f, (long) pos, SEEK_SET) != 0)
        return false;
    s->pos = pos;
    return true;
}

void wav_close(wav_source_t *s) {
    if (s->f != NULL)
        fclose(s->f);
    s->f = NULL;
}

static uint32_t le32(const uint8_t *p) {
    return p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t) (p[0] | p[1] << 8);
}

bool wav_open(wav_source_t *s, wav_t *w, const char **error) {
    uint8_t h[40];
    if (wav_read(s, h, 12) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) {
        *error = "not a WAV file";
        return false;
    }
    bool have_fmt = false;
    while (true) {
        if (wav_read(s, h, 8) != 8) {
            *error = have_fmt ? "no sound data" : "not a WAV file";
            return false;
        }
        uint32_t size = le32(h + 4);
        uint32_t start = s->pos; /* of the chunk's contents */
        uint32_t left = s->size - start;
        if (memcmp(h, "fmt ", 4) == 0 && size >= 16) {
            uint32_t n = size < sizeof(h) ? size : sizeof(h);
            if (size > left || wav_read(s, h, n) != n) {
                *error = "file cut short";
                return false;
            }
            w->format = le16(h);
            w->channels = le16(h + 2);
            w->rate = le32(h + 4);
            w->frame_bytes = le16(h + 12);
            w->bits = le16(h + 14);
            if (w->format == 0xFFFE && n >= 26) /* WAVE_FORMAT_EXTENSIBLE: the real one is in the GUID */
                w->format = le16(h + 24);
            have_fmt = true;
        } else if (memcmp(h, "data", 4) == 0) {
            if (!have_fmt) {
                *error = "not a WAV file";
                return false;
            }
            if (w->format != 1 && w->format != 3) {
                *error = "compressed WAV: export as PCM";
                return false;
            }
            bool bits_ok = w->format == 1 ? (w->bits == 8 || w->bits == 16 || w->bits == 24 || w->bits == 32)
                                          : w->bits == 32;
            if (!bits_ok || w->channels < 1 || w->channels > 8 || w->frame_bytes != w->channels * w->bits / 8
                    || w->rate < 4000 || w->rate > 192000) {
                *error = "unsupported WAV format";
                return false;
            }
            /* What the file really holds: a copy cut short, or a recording
             * whose size was never filled in (0xFFFFFFFF), would otherwise
             * play hours of silence. */
            w->data_pos = start;
            w->frames = (size < left ? size : left) / w->frame_bytes;
            return true;
        }
        /* Some other chunk: skip it (chunks are word-aligned). One claiming
         * more than the file holds is damage -- and would wrap the position
         * round to read the same chunk forever. */
        if (size > left || !wav_seek(s, start + size + (size & 1))) {
            *error = "file cut short";
            return false;
        }
    }
}

float wav_sample(const wav_t *w, const uint8_t *p) {
    switch (w->bits) {
        case 8:
            return ((int) p[0] - 128) / 128.0f; /* 8-bit WAV is unsigned */
        case 16:
            return (int16_t) le16(p) / 32768.0f;
        case 24:
            return (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) / 2147483648.0f;
        default:
            if (w->format == 3) {
                float f;
                memcpy(&f, p, 4);
                return f == f ? f : 0.0f; /* NaN -> 0 */
            }
            return (int32_t) le32(p) / 2147483648.0f;
    }
}
