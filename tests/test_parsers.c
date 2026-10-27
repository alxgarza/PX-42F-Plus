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
#include "fontsheet.h"
#include "pngwrite.h"
#include "irproto.h"
#include "irfile.h"
#include "ir_vectors.h"
#include "fonts/font_8x12.h"
#include "fonts/font_10x16.h"
#include "fonts/font_6x8.h"

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

/* ===================== font sheets ===================== */

static bool load_sheet(const uint8_t *png, size_t size, const display_font_t *font, int gap, uint8_t *glyphs,
                       char *error) {
    mem_t m = {png, size, 0};
    png_source_t src = {mem_read, &m, NULL};
    alarm(5);
    bool ok = font_sheet_load(&src, font->width, font->height, gap, glyphs, error, 64);
    alarm(0);
    return ok;
}

/* A font's sheet must slice into exactly the table gen_font.py made from
 * it (the built-in font); anything else must be refused, leaving the
 * glyphs alone. */
static void test_font_sheet(const char *path, const display_font_t *font, int gap) {
    static uint8_t glyphs[FONT_SHEET_GLYPHS * 32];
    size_t glyph_bytes = (size_t) FONT_SHEET_GLYPHS * font->bytes_per_char;
    char error[64];
    size_t size;
    uint8_t *png = read_file(path, &size);
    CHECK(png != NULL, "can't read %s", path);
    if (png == NULL)
        return;

    memset(glyphs, 0xA5, sizeof(glyphs));
    bool ok = load_sheet(png, size, font, gap, glyphs, error);
    CHECK(ok, "%s: %s", path, error);
    CHECK(ok && memcmp(glyphs, font->data, glyph_bytes) == 0, "%s slices differently from the built-in font", path);
    CHECK(glyphs[glyph_bytes] == 0xA5, "%s: wrote past the glyphs", path);

    /* The wrong size, and truncations: refused with the glyphs left alone
     * -- or, cut only after the last row's data (the checksum, IEND),
     * still the whole font. */
    memset(glyphs, 0xA5, sizeof(glyphs));
    CHECK(!load_sheet(png, size, font, gap + 1, glyphs, error), "%s: accepted with the wrong gap", path);
    CHECK(strstr(error, "not") != NULL, "%s: wrong size reported as \"%s\"", path, error);
    for (size_t cut = 0; cut < size; cut += 7) {
        if (load_sheet(png, cut, font, gap, glyphs, error)) {
            CHECK(memcmp(glyphs, font->data, glyph_bytes) == 0, "%s cut at %zu: accepted, but a different font", path,
                  cut);
            memset(glyphs, 0xA5, glyph_bytes);
        }
        bool untouched = true;
        for (size_t i = 0; i < sizeof(glyphs); i++)
            untouched = untouched && glyphs[i] == 0xA5;
        CHECK(untouched, "%s cut at %zu: a refused sheet changed the glyphs", path, cut);
    }
    free(png);
}

/* ===================== the PNG writer ===================== */

typedef struct {
    uint8_t *data;
    size_t size, room;
} sink_t;

static bool sink_write(void *ctx, const uint8_t *data, size_t len) {
    sink_t *s = ctx;
    if (len > s->room - s->size)
        return false;
    memcpy(s->data + s->size, data, len);
    s->size += len;
    return true;
}

/* What png_write_1bit() writes, png1bit.c must read back as the same
 * picture -- at the screen's size, tiny, not a multiple of 8 wide, and
 * tall enough to need more than one deflate block. (tests/run.sh also has
 * Python's zlib check the screen-sized one: its CRCs and Adler-32, which
 * png1bit.c doesn't look at.) */
static void test_png_write(int width, int height, const char *save_as) {
    int stride = (width + 7) / 8 + 1; /* a spare byte per row: stride isn't the row length */
    size_t bitmap = (size_t) stride * (size_t) height;
    uint8_t *in = malloc(bitmap), *back = malloc(bitmap);
    sink_t s = {malloc(bitmap + 4096), 0, bitmap + 4096};
    uint32_t seed = (uint32_t) (width * 7919 + height);
    for (size_t i = 0; i < bitmap; i++) {
        seed = seed * 1664525u + 1013904223u;
        in[i] = (uint8_t) (seed >> 24);
    }

    CHECK(png_write_1bit(in, width, height, stride, sink_write, &s), "%dx%d: write failed", width, height);
    mem_t m = {s.data, s.size, 0};
    png_source_t src = {mem_read, &m, NULL};
    char error[64] = "";
    alarm(5);
    bool ok = png_decode_1bit_exact(&src, back, width, height, stride, error, sizeof(error));
    alarm(0);
    CHECK(ok, "%dx%d: written PNG doesn't decode: %s", width, height, error);
    bool same = ok;
    for (int y = 0; same && y < height; y++)
        for (int x = 0; same && x < width; x++)
            same = ((in[y * stride + x / 8] ^ back[y * stride + x / 8]) & (0x80 >> (x % 8))) == 0;
    CHECK(same, "%dx%d: written PNG decodes to a different picture", width, height);

    /* A sink that fills up is reported, not written past. */
    sink_t small = {s.data, 0, s.size - 1};
    CHECK(!png_write_1bit(in, width, height, stride, sink_write, &small), "%dx%d: a full sink went unnoticed", width,
          height);

    if (save_as != NULL) {
        FILE *f = fopen(save_as, "wb");
        CHECK(f != NULL && fwrite(s.data, 1, s.size, f) == s.size, "can't write %s", save_as);
        if (f != NULL)
            fclose(f);
    }
    free(in);
    free(back);
    free(s.data);
}

/* The sheet the calculator writes to /FONTS/BUILTIN (fonts.c): drawn from
 * the built-in font, written as a PNG, it must load back as that font. */
static void test_font_sheet_draw(const display_font_t *font, int gap) {
    static uint8_t glyphs[FONT_SHEET_GLYPHS * 32];
    int width = FONT_SHEET_WIDTH(font->width, gap), height = FONT_SHEET_HEIGHT(font->height, gap);
    int stride = (width + 7) / 8;
    uint8_t *sheet = malloc((size_t) stride * (size_t) height);
    sink_t s = {malloc(8192), 0, 8192};
    font_sheet_draw(font, gap, sheet, stride);
    CHECK(png_write_1bit(sheet, width, height, stride, sink_write, &s), "%dx%d sheet: write failed", font->width,
          font->height);
    char error[64] = "";
    memset(glyphs, 0xA5, sizeof(glyphs));
    bool ok = load_sheet(s.data, s.size, font, gap, glyphs, error);
    CHECK(ok, "%dx%d drawn sheet: %s", font->width, font->height, error);
    CHECK(ok && memcmp(glyphs, font->data, (size_t) FONT_SHEET_GLYPHS * font->bytes_per_char) == 0,
          "%dx%d drawn sheet loads as a different font", font->width, font->height);
    free(sheet);
    free(s.data);
}

/* ===================== infrared ===================== */

/* Every protocol sends what the Flipper firmware sends for the same code
 * (tests/ir_vectors.h, made from its encoders): the first press, and two
 * transmissions more of a held key. */
static void test_ir_encode(void) {
    for (size_t v = 0; v < sizeof(ir_vectors) / sizeof(ir_vectors[0]); v++) {
        ir_code_t code = {ir_protocol_by_name(ir_vectors[v].protocol), ir_vectors[v].address, ir_vectors[v].command};
        CHECK(code.protocol != IR_PROTOCOL_COUNT, "%s: no such protocol", ir_vectors[v].protocol);
        if (code.protocol == IR_PROTOCOL_COUNT)
            continue;
        CHECK(ir_code_valid(&code), "%s: vector's code out of range", ir_vectors[v].protocol);
        uint32_t us[400];
        int n = 0;
        for (int t = 0; t < IR_VECTOR_TRANSMISSIONS; t++) {
            ir_burst_t b;
            ir_encode(&code, ir_vectors[v].toggle, t, &b);
            for (int i = 0; i < b.count; i++) {
                if (n % 2 != i % 2)
                    us[n - 1] += b.us[i]; /* a gap after a gap (Pioneer) */
                else if (n < 400)
                    us[n++] = b.us[i];
            }
        }
        CHECK(n == ir_vectors[v].count && memcmp(us, ir_vectors[v].us, sizeof(uint32_t) * (size_t) n) == 0,
              "%s %X/%X toggle %d: sends something else than Flipper", ir_vectors[v].protocol, ir_vectors[v].address,
              ir_vectors[v].command, ir_vectors[v].toggle);
    }
}

static bool load_ir(const char *text, ir_remote_t *remote, int *line, char *error) {
    /* (fmemopen() can't open an empty buffer) */
    FILE *f = text[0] ? fmemopen((void *) text, strlen(text), "r") : fopen("/dev/null", "r");
    bool ok = ir_remote_load(f, remote, line, error, 32);
    fclose(f);
    return ok;
}

static int keys_used(const ir_remote_t *remote) {
    int n = 0;
    for (int r = 0; r < IR_ROWS; r++)
        for (int c = 0; c < IR_COLS; c++)
            n += remote->keys[r][c].used;
    return n;
}

static void test_ir_file_error(const char *text, int expected_line, const char *expected_error) {
    static ir_remote_t remote;
    int line = 0;
    char error[32] = "";
    bool ok = load_ir(text, &remote, &line, error);
    CHECK(!ok && line == expected_line && strcmp(error, expected_error) == 0,
          "expected line %d \"%s\", got %s line %d \"%s\"", expected_line, expected_error, ok ? "success" : "error",
          line, error);
}

static void test_ir_file(void) {
    /* A Flipper file, with buttons and a label added, a raw code on no key,
     * and its long data line. */
    static const char good[] =
            "Filetype: IR signals file\n"
            "Version: 1\n"
            "#\n"
            "name: TV On/Off\n"
            "label: Power\n"
            "button: R0C5\n"
            "type: parsed\n"
            "protocol: NECext\n"
            "address: 02 7D 00 00\n"
            "command: 9F 60 00 00\n"
            "#\n"
            "name: Learned\n"
            "type: raw\n"
            "frequency: 38000\n"
            "duty_cycle: 0.330000\n"
            "data: 9024 4512 579 552 579 552 579 1683 579 552 579 552 579 552 579 552 579 552 579 1683 579 1683\n"
            "#\n"
            "name: Vol+\r\n"
            "button: r6c4\r\n"
            "protocol: NEC\r\n"
            "address: 07 00 00 00\r\n"
            "command: 02 00 00 00\r\n"
            "#\n"
            "name: Input Source Select Long\n"
            "button: R2C0\n"
            "protocol: Samsung32\n"
            "address: 07\n"
            "command: 01 00";
    static ir_remote_t remote;
    int line = 0;
    char error[32] = "";
    CHECK(load_ir(good, &remote, &line, error), "good .ir file: line %d: %s", line, error);
    CHECK(keys_used(&remote) == 3, "good .ir file: %d keys, expected 3", keys_used(&remote));
    const ir_key_t *power = &remote.keys[0][5], *vol = &remote.keys[6][4], *input = &remote.keys[2][0];
    CHECK(power->used && strcmp(power->name, "TV On/Off") == 0 && strcmp(power->label, "Power") == 0
                  && power->code.protocol == IR_NECEXT && power->code.address == 0x7D02
                  && power->code.command == 0x609F,
          "R0C5 read as \"%s\" \"%s\" %d %X %X", power->name, power->label, power->code.protocol,
          power->code.address, power->code.command);
    CHECK(vol->used && strcmp(vol->label, "Vol+") == 0 && vol->code.protocol == IR_NEC && vol->code.address == 7
                  && vol->code.command == 2,
          "R6C4 (CR LF lines, no label) read wrong");
    CHECK(input->used && strcmp(input->name, "Input Source Select Lon") == 0
                  && strcmp(input->label, "Input Sour") == 0 && input->code.address == 7 && input->code.command == 1,
          "R2C0 (long name, short bytes) read as \"%s\" \"%s\"", input->name, input->label);

    CHECK(load_ir("", &remote, &line, error) && keys_used(&remote) == 0, "an empty file is an empty remote");

    static const char entry[] = "name: A\nbutton: R1C1\nprotocol: NEC\naddress: 01 00 00 00\ncommand: 02 00 00 00\n";
    test_ir_file_error("name: A\nbutton: R6C0\nprotocol: NEC\naddress: 01\ncommand: 02\n", 2,
                       "R6C0 is ON: can't be used");
    test_ir_file_error("name: A\nbutton: R2C5\nprotocol: NEC\naddress: 01\ncommand: 02\n", 2, "No such key");
    test_ir_file_error("name: A\nbutton: X1\nprotocol: NEC\naddress: 01\ncommand: 02\n", 2,
                       "Bad button (R0C0 to R6C4)");
    test_ir_file_error("name: A\nbutton: R1C1x\nprotocol: NEC\naddress: 01\ncommand: 02\n", 2,
                       "Bad button (R0C0 to R6C4)");
    char twice[256];
    snprintf(twice, sizeof(twice), "%s#\n%s", entry, entry);
    test_ir_file_error(twice, 8, "Key used twice");
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: Foo\naddress: 01\ncommand: 02\n", 3, "Unknown protocol");
    test_ir_file_error("name: A\nbutton: R1C1\ntype: raw\ndata: 100 200\n", 4, "Raw codes aren't supported");
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: NEC\naddress: 01 01\ncommand: 02\n", 4,
                       "Too big for the protocol");
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: NEC\naddress: 0G\ncommand: 02\n", 4, "Bad address");
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: NEC\naddress: 01\ncommand: 02 00 00 00 00\n", 5,
                       "Bad command");
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: NEC\naddress: 01\n", 1, "No command");
    /* A file cut off mid-entry is checked like any other. */
    test_ir_file_error("name: A\nbutton: R1C1\nprotocol: NEC\naddr", 1, "No address");
}

int main(int argc, char **argv) {
    signal(SIGALRM, on_alarm);
    test_png("images/splash.png", true);
    test_png("fonts/font_10x16.png", false);
    test_png("fonts/font_12x8.png", false);
    test_font_sheet("fonts/font_12x8.png", &font_8x12, 0);
    test_font_sheet("fonts/font_10x16.png", &font_10x16, 1);
    test_font_sheet("fonts/font_6x8.png", &font_6x8, 0);
    test_png_write(250, 122, getenv("PNG_WRITE_OUT"));
    test_font_sheet_draw(&font_8x12, 0);
    test_font_sheet_draw(&font_10x16, 1);
    test_font_sheet_draw(&font_6x8, 0);
    test_png_write(1, 1, NULL);
    test_png_write(17, 5, NULL);
    test_png_write(2040, 300, NULL);
    uint8_t one = 0;
    CHECK(!png_write_1bit(&one, 2041, 1, 256, sink_write, &(sink_t){&one, 0, 0}), "too wide accepted");
    test_ir_encode();
    test_ir_file();
    test_wav();
    for (int i = 1; i < argc; i++)
        test_wav_file(argv[i]);
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
