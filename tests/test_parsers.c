/* Host tests for the two parsers that read files users drop on the drive:
 * png1bit.c (splash images) and wav.c (sounds). Built and run on the
 * computer, with AddressSanitizer, by tests/run.sh -- not on the device.
 *
 * What matters most is that damaged input fails cleanly: a crash or a hang
 * here is a calculator that resets or freezes when the file is on its
 * drive. Every case runs under an alarm, so a hang fails the test. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "png1bit.h"
#include "wav.h"
#include "images/image.h"

static int failures;

#define CHECK(cond, ...)                                       \
    do {                                                       \
        if (!(cond)) {                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);        \
            printf(__VA_ARGS__);                               \
            printf("\n");                                      \
            failures++;                                        \
        }                                                      \
    } while (0)

static void on_alarm(int sig) {
    (void) sig;
    static const char msg[] = "FAIL: a case hung (alarm)\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(1);
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t) ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size ? *size : 1);
    if (fread(data, 1, *size, f) != *size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

/* ===================== PNG ===================== */

#define W 250
#define H 122
#define STRIDE ((W + 7) / 8)

typedef struct {
    const uint8_t *data;
    size_t size, pos;
} mem_t;

static int mem_read(void *ctx, uint8_t *buf, int len) {
    mem_t *m = ctx;
    size_t n = (size_t) len < m->size - m->pos ? (size_t) len : m->size - m->pos;
    memcpy(buf, m->data + m->pos, n);
    m->pos += n;
    return (int) n;
}

static bool decode(const uint8_t *data, size_t size, uint8_t *bits, char *error) {
    mem_t m = {data, size, 0};
    png_source_t src = {mem_read, &m, NULL};
    alarm(5);
    bool ok = png_decode_1bit(&src, bits, W, H, STRIDE, error, 64);
    alarm(0);
    return ok;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static void test_png(const char *path, bool compare_with_builtin) {
    size_t size;
    uint8_t *png = read_file(path, &size);
    CHECK(png != NULL, "can't read %s", path);
    if (png == NULL)
        return;
    static uint8_t bits[STRIDE * H];
    char error[64];

    /* The real file decodes -- and splash.png exactly as gen_image.py did. */
    bool ok = decode(png, size, bits, error);
    CHECK(ok, "%s: %s", path, error);
    if (ok && compare_with_builtin) {
        CHECK(splash_image.width == W && splash_image.height == H, "built-in splash size");
        CHECK(memcmp(bits, splash_image.data, sizeof(bits)) == 0, "%s decodes differently from images/splash_image.c",
              path);
    }

    /* Where the picture is complete: the last IDAT's contents, but for the
     * zlib stream's closing Adler-32 checksum (4 bytes), which the decoder
     * doesn't check. Past that, a damaged tail may be accepted. */
    size_t data_end = 0;
    for (size_t pos = 8; pos + 12 <= size;) {
        uint32_t len = be32(png + pos);
        if (len > size - pos - 12)
            break;
        if (memcmp(png + pos + 4, "IDAT", 4) == 0)
            data_end = pos + 8 + len;
        pos += 12 + len;
    }
    CHECK(data_end > 4, "%s: no IDAT", path);
    data_end -= 4;

    /* Cut short anywhere before that: must fail, cleanly. */
    for (size_t cut = 0; cut < data_end; cut++)
        CHECK(!decode(png, cut, bits, error), "%s cut to %zu bytes was accepted", path, cut);
    for (size_t cut = data_end; cut < size; cut++)
        decode(png, cut, bits, error); /* either way, no crash or hang */

    /* Each chunk's length field made huge: no crash, no hang, no reading
     * past the end (ASan). (With the real data still following, it may
     * even decode -- it stops when the picture is complete.) */
    uint8_t *bad = malloc(size);
    for (size_t pos = 8; pos + 12 <= size;) {
        uint32_t len = be32(png + pos);
        for (int v = 0; v < 3; v++) {
            static const uint32_t lens[] = {0xFFFFFFFFu, 0x7FFFFFFFu, 0x10000u};
            memcpy(bad, png, size);
            bad[pos] = (uint8_t) (lens[v] >> 24);
            bad[pos + 1] = (uint8_t) (lens[v] >> 16);
            bad[pos + 2] = (uint8_t) (lens[v] >> 8);
            bad[pos + 3] = (uint8_t) lens[v];
            decode(bad, size, bits, error);
        }
        if (len > size - pos - 12)
            break;
        pos += 12 + len;
    }

    /* Random damage: any result is fine, as long as it neither crashes
     * (ASan) nor hangs (the alarm). */
    srand(42);
    for (int i = 0; i < 2000; i++) {
        memcpy(bad, png, size);
        int hits = 1 + rand() % 4;
        for (int h = 0; h < hits; h++)
            bad[8 + rand() % (size - 8)] ^= (uint8_t) (1 + rand() % 255);
        decode(bad, size, bits, error);
    }
    free(bad);
    free(png);
}

/* ===================== WAV ===================== */

/* A WAV built in memory: header fields can be set to lie. */
static size_t make_wav(uint8_t *out, uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits,
                       uint32_t data_size_field, const uint8_t *data, size_t data_len, bool extra_chunk,
                       uint32_t extra_size_field) {
    size_t n = 0;
#define PUT(p, len) (memcpy(out + n, (p), (len)), n += (len))
#define PUT16(v) (out[n] = (uint8_t) (v), out[n + 1] = (uint8_t) ((v) >> 8), n += 2)
#define PUT32(v) (PUT16((v) & 0xFFFF), PUT16((uint32_t) (v) >> 16))
    PUT("RIFF", 4);
    PUT32(0);
    PUT("WAVE", 4);
    if (extra_chunk) { /* e.g. Audacity's LIST chunk before the data */
        PUT("LIST", 4);
        PUT32(extra_size_field);
        PUT("INFOabc", 7); /* 7 bytes: odd, so a pad byte follows */
        out[n++] = 0;
    }
    PUT("fmt ", 4);
    PUT32(16);
    PUT16(format);
    PUT16(channels);
    PUT32(rate);
    PUT32(rate * channels * bits / 8);
    PUT16(channels * bits / 8);
    PUT16(bits);
    PUT("data", 4);
    PUT32(data_size_field);
    PUT(data, data_len);
    return n;
#undef PUT
#undef PUT16
#undef PUT32
}

static bool open_mem(const uint8_t *wav, size_t size, wav_t *w, const char **error) {
    wav_source_t s = wav_source_mem(wav, (uint32_t) size);
    alarm(5);
    bool ok = wav_open(&s, w, error);
    alarm(0);
    return ok;
}

static void test_wav(void) {
    static uint8_t wav[4096], pcm[1000];
    for (int i = 0; i < 1000; i++)
        pcm[i] = (uint8_t) (i * 37);
    wav_t w;
    const char *error;
    size_t n;

    /* A good one: 500 frames of 16-bit mono. */
    n = make_wav(wav, 1, 1, 16000, 16, 1000, pcm, 1000, false, 0);
    CHECK(open_mem(wav, n, &w, &error) && w.frames == 500 && w.rate == 16000, "good 16-bit WAV");

    /* The data size left at 0xFFFFFFFF (a recording never finalized): only
     * what the file holds. This was the calculator-freezing bug. */
    n = make_wav(wav, 1, 1, 16000, 16, 0xFFFFFFFFu, pcm, 1000, false, 0);
    CHECK(open_mem(wav, n, &w, &error) && w.frames == 500, "data size 0xFFFFFFFF: frames %u", w.frames);

    /* Cut short in the middle of the data: only what's there. */
    n = make_wav(wav, 1, 1, 16000, 16, 1000, pcm, 1000, false, 0);
    CHECK(open_mem(wav, n - 401, &w, &error) && w.frames == 299, "cut in the data: frames %u", w.frames);

    /* The same, through a real FILE (the drive's path). */
    FILE *f = tmpfile();
    fwrite(wav, 1, n - 400, f);
    wav_source_t s;
    CHECK(wav_source_file(&s, f), "wav_source_file");
    alarm(5);
    CHECK(wav_open(&s, &w, &error) && w.frames == 300, "FILE cut in the data: frames %u", w.frames);
    alarm(0);
    wav_close(&s);

    /* Cut anywhere in the header: fails, never hangs or reads past. */
    n = make_wav(wav, 1, 1, 16000, 16, 1000, pcm, 1000, true, 7);
    CHECK(open_mem(wav, n, &w, &error) && w.frames == 500, "LIST chunk (odd size, padded) skipped");
    for (size_t cut = 0; cut < 60; cut++)
        CHECK(!open_mem(wav, cut, &w, &error) || w.frames * w.frame_bytes <= cut, "header cut to %zu", cut);

    /* A chunk claiming nearly 4GB: rejected (it used to wrap the position
     * round and loop on the same chunk until the watchdog reset). */
    n = make_wav(wav, 1, 1, 16000, 16, 1000, pcm, 1000, true, 0xFFFFFFF0u);
    CHECK(!open_mem(wav, n, &w, &error) && strcmp(error, "file cut short") == 0, "huge chunk size: %s",
          error);
    n = make_wav(wav, 1, 1, 16000, 16, 1000, pcm, 1000, true, 0xFFFFFFFFu);
    CHECK(!open_mem(wav, n, &w, &error), "chunk size 0xFFFFFFFF accepted");

    /* Formats. */
    n = make_wav(wav, 2, 1, 16000, 4, 1000, pcm, 1000, false, 0);
    CHECK(!open_mem(wav, n, &w, &error) && strstr(error, "PCM"), "ADPCM rejected: %s", error);
    n = make_wav(wav, 1, 1, 16000, 12, 1000, pcm, 1000, false, 0);
    CHECK(!open_mem(wav, n, &w, &error), "12-bit rejected");
    n = make_wav(wav, 1, 0, 16000, 16, 1000, pcm, 1000, false, 0);
    CHECK(!open_mem(wav, n, &w, &error), "0 channels rejected");
    n = make_wav(wav, 1, 1, 100, 16, 1000, pcm, 1000, false, 0);
    CHECK(!open_mem(wav, n, &w, &error), "100Hz rejected");
    memcpy(wav, "RIFX", 4);
    CHECK(!open_mem(wav, n, &w, &error), "not RIFF rejected");

    /* Sample values. */
    w.format = 1;
    w.bits = 8;
    uint8_t b8[] = {0, 128, 255};
    CHECK(wav_sample(&w, b8) == -1.0f && wav_sample(&w, b8 + 1) == 0.0f, "8-bit is unsigned");
    w.bits = 16;
    uint8_t b16[] = {0x00, 0x80, 0xFF, 0x7F};
    CHECK(wav_sample(&w, b16) == -1.0f && wav_sample(&w, b16 + 2) > 0.999f, "16-bit range");
    w.bits = 24;
    uint8_t b24[] = {0x00, 0x00, 0x80};
    CHECK(wav_sample(&w, b24) == -1.0f, "24-bit");
    w.format = 3;
    w.bits = 32;
    float half = 0.5f, nan = 0.0f / 0.0f;
    CHECK(wav_sample(&w, (uint8_t *) &half) == 0.5f && wav_sample(&w, (uint8_t *) &nan) == 0.0f, "float, NaN");

    /* Random damage to a good file. */
    n = make_wav(wav, 1, 2, 44100, 16, 1000, pcm, 1000, true, 7);
    static uint8_t bad[4096];
    srand(7);
    for (int i = 0; i < 20000; i++) {
        memcpy(bad, wav, n);
        int hits = 1 + rand() % 4;
        for (int h = 0; h < hits; h++)
            bad[rand() % 60] ^= (uint8_t) (1 + rand() % 255);
        if (open_mem(bad, n, &w, &error))
            CHECK(w.data_pos + (uint64_t) w.frames * w.frame_bytes <= n, "damaged WAV reads past its end");
    }
}

/* Every .wav given on the command line (the built-in sounds) opens, and
 * its samples are all in range. */
static void test_wav_file(const char *path) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "can't open %s", path);
    if (f == NULL)
        return;
    wav_source_t s;
    wav_t w;
    const char *error = "";
    CHECK(wav_source_file(&s, f) && wav_open(&s, &w, &error), "%s: %s", path, error);
    uint8_t frame[64];
    for (uint32_t i = 0; i < w.frames && wav_read(&s, frame, w.frame_bytes) == w.frame_bytes; i++) {
        float x = wav_sample(&w, frame);
        if (x < -1.0f || x > 1.0f) {
            CHECK(0, "%s: sample %u out of range", path, i);
            break;
        }
    }
    wav_close(&s);
}

int main(int argc, char **argv) {
    signal(SIGALRM, on_alarm);
    test_png("images/splash.png", true);
    test_png("fonts/font_10x16.png", false);
    test_png("fonts/font_12x8.png", false);
    test_wav();
    for (int i = 1; i < argc; i++)
        test_wav_file(argv[i]);
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
