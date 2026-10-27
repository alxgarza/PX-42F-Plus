/* What the PX-42F's speaker is given, as WAV files to listen to and look at
 * on a computer: the firmware's own converter (sounds.c, included whole)
 * turns each sound into the duty stream speaker.c plays, and each duty value
 * becomes one sample -- 32kHz, 0..1024 scaled to 0..32768 (16-bit mono).
 * Unipolar, as the coil's current is: the wave rides on the level that
 * follows its loudness. Not modelled: the speaker itself (a small cone plays
 * little bass) and the 146kHz carrier, far above hearing.
 *
 *   px_sound OUT_DIR                     every built-in sound
 *   px_sound OUT_DIR [-ms N] FILE.wav...  those files, as the PX converts
 *                                        them (-ms: at most N ms, as key
 *                                        sounds are: 100 in a set, 250 alone)
 *
 * tools/px_sound/run.sh builds and runs it. */
#include "sounds.c"

#include <errno.h>
#include <sys/stat.h>

px_settings_t px_settings;

bool statefs_ready(void) { return false; } /* open_sound() falls back to the built-ins */
FILE *statefs_fopen(const char *name, const char *mode) { return NULL; }
bool statefs_file_exists(const char *path) { return false; }
void statefs_mkdir(const char *path) {}
bool statefs_sync(void) { return true; }
int statefs_list_files(const char *dir, const char *ext, char *names, size_t name_size, int max) { return 0; }
int statefs_list_dirs(const char *dir, char *names, size_t name_size, int max) { return 0; }
void speaker_play(speaker_fill_fn fill, void *ctx) {}

static void put16(FILE *f, uint32_t v) { fputc(v & 0xFF, f); fputc(v >> 8 & 0xFF, f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

/* Runs the converter to the end and writes what it gave as a WAV. */
static bool convert(stream_t *st, const char *out) {
    static uint16_t duty[SPEAKER_SAMPLE_HZ * 600 + 1]; /* 10 minutes */
    uint32_t n = 0, got;
    while (n < count_of(duty) - 1 && (got = stream_fill(st, duty + n, count_of(duty) - 1 - n)) != 0)
        n += got;
    wav_close(&st->src);
    duty[n++] = 0; /* then speaker.c turns the transistor off */
    FILE *f = fopen(out, "wb");
    if (f == NULL) {
        fprintf(stderr, "%s: %s\n", out, strerror(errno));
        return false;
    }
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + n * 2);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);                         /* PCM */
    put16(f, 1);                         /* mono */
    put32(f, SPEAKER_SAMPLE_HZ);
    put32(f, SPEAKER_SAMPLE_HZ * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, n * 2);
    for (uint32_t i = 0; i < n; i++)
        put16(f, (uint16_t) (duty[i] * 32767u / SPEAKER_DUTY_MAX));
    fclose(f);
    printf("%s  %.0f ms\n", out, n * 1000.0 / SPEAKER_SAMPLE_HZ);
    return true;
}

/* The longest a built-in plays: key sounds are cut as sounds_key_select() cuts them. */
static uint32_t max_out(const char *path) {
    if (strncmp(path, "KEY/", 4) != 0)
        return UINT32_MAX;
    return strchr(path + 4, '/') ? KEY_SET_MAX_OUT : KEY_MAX_OUT;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: px_sound OUT_DIR [-ms N] [FILE.wav...]\n");
        return 2;
    }
    const char *dir = argv[1];
    mkdir(dir, 0755);
    char out[1024];
    const char *error;
    int bad = 0;
    if (argc == 2) {
        for (unsigned i = 0; i < builtin_sound_count; i++) {
            const builtin_sound_t *b = &builtin_sounds[i];
            snprintf(out, sizeof(out), "%s/%s", dir, b->path);
            for (char *p = out + strlen(dir) + 1; *p; p++)
                if (*p == '/')
                    *p = ' ';
            if (!stream_open(&play_stream, wav_source_mem(b->wav, b->size), max_out(b->path), &error)
                || !convert(&play_stream, out)) {
                fprintf(stderr, "%s: %s\n", b->path, error);
                bad = 1;
            }
        }
        return bad;
    }
    uint32_t limit = UINT32_MAX;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-ms") == 0 && i + 1 < argc) {
            limit = (uint32_t) atoi(argv[++i]) * (SPEAKER_SAMPLE_HZ / 1000);
            continue;
        }
        FILE *f = fopen(argv[i], "rb");
        wav_source_t src;
        const char *name = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
        snprintf(out, sizeof(out), "%s/%s", dir, name);
        if (f == NULL || !wav_source_file(&src, f)) {
            fprintf(stderr, "%s: can't read it\n", argv[i]);
            bad = 1;
        } else if (!stream_open(&play_stream, src, limit, &error)) {
            fprintf(stderr, "%s: %s\n", argv[i], error);
            bad = 1;
        } else if (!convert(&play_stream, out)) {
            bad = 1;
        }
    }
    return bad;
}
