#include "sounds.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "settings.h"
#include "speaker.h"
#include "statefs.h"
#include "wav.h"

#define KEY_DIR     SOUNDS_DIR "/KEY"
#define KEY_MAX_OUT (SPEAKER_SAMPLE_HZ / 4) /* 250ms: a key sound holds up its keypress */

/* Converting a sound to duty values (see speaker.h): the waveform x,
 * normalized to +-1, rides on a level e that follows its loudness:
 * duty = (e + x) / 2. With e >= |x| the duty never goes negative, and e is
 * 0 in silence -- no current through the coil when nothing plays. e looks
 * LOOKAHEAD samples ahead (the output is delayed that much), so it has
 * risen before a sound starts instead of stepping up with it, and falls
 * back slowly -- a fast-moving e would be heard as distortion. */
#define LOOKAHEAD   64              /* 2ms */
#define ATTACK_K    0.075f          /* 1 - exp(-1 / (0.4ms * 32kHz)) */
#define RELEASE_K   0.99844f        /* exp(-1 / (20ms * 32kHz)) */
#define FADE_IN     16              /* 0.5ms, against a file starting mid-wave */
#define FADE_OUT    64              /* 2ms, against one ending mid-wave */
#define TAIL_MAX    1600            /* the level's fall after the end, at most 50ms */

/* ===================== the converter ===================== */

typedef struct {
    wav_source_t src;
    wav_t w;
    float gain;              /* 1 / the peak: normalized */
    uint32_t frames_left;    /* still to read */
    uint8_t raw[512];
    uint32_t raw_len, raw_pos;
    /* resampling to SPEAKER_SAMPLE_HZ, linear */
    float x0, x1, frac, step;
    uint32_t out_total, out_n; /* output samples of the sound itself */
    /* the level e, looking ahead */
    float delay[LOOKAHEAD];
    uint32_t dpos;
    float env;
    uint32_t flushed;        /* samples since the sound's end */
} stream_t;

/* The next input frame, mixed to mono; 0 past the end. */
static float next_frame(stream_t *st) {
    if (st->frames_left == 0)
        return 0.0f;
    if (st->raw_pos + st->w.frame_bytes > st->raw_len) {
        uint32_t want = (uint32_t) sizeof(st->raw) / st->w.frame_bytes * st->w.frame_bytes;
        st->raw_len = wav_read(&st->src, st->raw, want);
        st->raw_pos = 0;
        if (st->raw_len < st->w.frame_bytes) {
            st->frames_left = 0; /* shorter than its header said */
            return 0.0f;
        }
    }
    const uint8_t *p = st->raw + st->raw_pos;
    st->raw_pos += st->w.frame_bytes;
    st->frames_left--;
    float sum = 0.0f;
    for (uint c = 0; c < st->w.channels; c++)
        sum += wav_sample(&st->w, p + c * (st->w.bits / 8));
    return sum / (float) st->w.channels;
}

/* Opens a sound and gets it ready: scans it once for its peak (to
 * normalize), then rewinds. max_frames caps how much of it is used. */
static bool stream_open(stream_t *st, wav_source_t src, uint32_t max_out, const char **error) {
    memset(st, 0, sizeof(*st));
    st->src = src;
    if (!wav_open(&st->src, &st->w, error))
        return false;
    uint64_t out_total = (uint64_t) st->w.frames * SPEAKER_SAMPLE_HZ / st->w.rate;
    st->out_total = out_total < max_out ? (uint32_t) out_total : max_out;
    uint32_t frames_used = (uint32_t) ((uint64_t) st->out_total * st->w.rate / SPEAKER_SAMPLE_HZ) + 2;
    if (frames_used > st->w.frames)
        frames_used = st->w.frames;

    float peak = 0.0f;
    st->frames_left = frames_used;
    for (uint32_t i = 0; i < frames_used; i++) {
        float x = next_frame(st);
        if (x < 0)
            x = -x;
        if (x > peak)
            peak = x;
        if ((i & 0x3FFF) == 0)
            watchdog_update();
    }
    if (peak < 1e-4f) {
        *error = "silent";
        return false;
    }
    st->gain = 1.0f / peak;
    if (!wav_seek(&st->src, st->w.data_pos)) {
        *error = "can't read it";
        return false;
    }
    st->frames_left = frames_used;
    st->raw_len = st->raw_pos = 0;
    st->step = (float) st->w.rate / SPEAKER_SAMPLE_HZ;
    st->x0 = 0.0f;
    st->x1 = next_frame(st);
    st->frac = 1.0f; /* the first output is the first frame */
    return true;
}

static uint32_t stream_fill(void *ctx, uint16_t *buf, uint32_t max) {
    stream_t *st = (stream_t *) ctx;
    uint32_t n = 0;
    while (n < max) {
        /* The newest sample into the look-ahead line. */
        float x = 0.0f;
        if (st->out_n < st->out_total) {
            while (st->frac >= 1.0f) {
                st->x0 = st->x1;
                st->x1 = next_frame(st);
                st->frac -= 1.0f;
            }
            x = (st->x0 + (st->x1 - st->x0) * st->frac) * st->gain;
            st->frac += st->step;
            uint32_t i = st->out_n++;
            if (i < FADE_IN)
                x *= (float) i / FADE_IN;
            if (st->out_total - i <= FADE_OUT)
                x *= (float) (st->out_total - i) / FADE_OUT;
        } else if (++st->flushed > LOOKAHEAD + TAIL_MAX) {
            break;
        }
        float out = st->delay[st->dpos];
        st->delay[st->dpos] = x;
        st->dpos = (st->dpos + 1) % LOOKAHEAD;

        /* The level: the loudest of what's coming, smoothed. */
        float target = 0.0f;
        for (uint i = 0; i < LOOKAHEAD; i++) {
            float a = st->delay[i] < 0 ? -st->delay[i] : st->delay[i];
            if (a > target)
                target = a;
        }
        if (target > st->env)
            st->env += (target - st->env) * ATTACK_K;
        else
            st->env = st->env * RELEASE_K > target ? st->env * RELEASE_K : target;
        float e = st->env;
        float a = out < 0 ? -out : out;
        if (e < a)
            e = a;
        if (e > 1.0f)
            e = 1.0f;

        int duty = (int) ((e + out) * 0.5f * SPEAKER_DUTY_MAX + 0.5f);
        if (st->out_n >= st->out_total && st->flushed > LOOKAHEAD && duty == 0)
            break; /* faded all the way out */
        buf[n++] = (uint16_t) (duty < 0 ? 0 : duty > SPEAKER_DUTY_MAX ? SPEAKER_DUTY_MAX : duty);
    }
    return n;
}

/* ===================== finding sounds ===================== */

static const builtin_sound_t *builtin(const char *path) {
    for (unsigned i = 0; i < builtin_sound_count; i++)
        if (strcasecmp(builtin_sounds[i].path, path) == 0)
            return &builtin_sounds[i];
    return NULL;
}

static bool use_files_ok = true;

/* /SOUNDS/<path>, or the built-in if the file is missing or unusable. */
static bool open_sound(stream_t *st, const char *path, uint32_t max_out, const char **error) {
    char full[64];
    snprintf(full, sizeof(full), "%s/%s", SOUNDS_DIR, path);
    *error = "not found";
    FILE *f = use_files_ok && statefs_ready() ? statefs_fopen(full, "rb") : NULL;
    if (f != NULL) {
        wav_source_t src;
        if (!wav_source_file(&src, f)) {
            fclose(f);
            *error = "can't read it";
        } else if (stream_open(st, src, max_out, error)) {
            return true;
        } else {
            wav_close(&st->src);
        }
    }
    const builtin_sound_t *b = builtin(path);
    if (b == NULL)
        return false;
    const char *builtin_error;
    return stream_open(st, wav_source_mem(b->wav, b->size), max_out, &builtin_error);
}

static stream_t play_stream; /* ~1KB: not on the stack */

static void play(const char *path) {
    const char *error;
    if (open_sound(&play_stream, path, UINT32_MAX, &error)) {
        speaker_play(stream_fill, &play_stream);
        wav_close(&play_stream.src);
    }
}

void sounds_play_boot(void) {
    play("BOOT.wav");
}

void sounds_play_on(void) {
    play("ON.wav");
}

void sounds_play_off(void) {
    play("OFF.wav");
}

void sounds_play_tone(int tone) {
    char path[20];
    if (tone >= 0 && tone <= 9)
        snprintf(path, sizeof(path), "TONES/TONE%d.wav", tone);
    else if (tone == 10)
        snprintf(path, sizeof(path), "ERROR.wav");
    else
        return;
    play(path);
}

/* ===================== the key sound, in RAM ===================== */

static uint16_t key_duty[KEY_MAX_OUT + LOOKAHEAD + TAIL_MAX];
static uint32_t key_len; /* 0 = Off */

typedef struct {
    uint32_t pos;
} key_play_t;

static uint32_t key_fill(void *ctx, uint16_t *buf, uint32_t max) {
    key_play_t *k = (key_play_t *) ctx;
    uint32_t n = key_len - k->pos < max ? key_len - k->pos : max;
    memcpy(buf, key_duty + k->pos, n * sizeof(uint16_t));
    k->pos += n;
    return n;
}

void sounds_key_play(void) {
    if (key_len == 0)
        return;
    key_play_t k = {0};
    speaker_play(key_fill, &k);
}

bool sounds_key_select(const char *name, char *error, size_t error_size) {
    key_len = 0;
    if (name[0] == 0)
        return true; /* Off */
    char path[16 + SOUND_NAME_MAX];
    snprintf(path, sizeof(path), "KEY/%s.wav", name);
    const char *why;
    if (!open_sound(&play_stream, path, KEY_MAX_OUT, &why)) {
        snprintf(error, error_size, "%s", why);
        return false;
    }
    uint32_t n = 0, got;
    while ((got = stream_fill(&play_stream, key_duty + n, 1024 < (uint32_t) count_of(key_duty) - n
                                                                  ? 1024 : (uint32_t) count_of(key_duty) - n)) != 0) {
        n += got;
        watchdog_update(); /* converting 250ms of sound takes a while */
        if (n == count_of(key_duty))
            break;
    }
    wav_close(&play_stream.src);
    key_len = n;
    return true;
}

static int key_name_of(const char *path, char *name) {
    /* "KEY/<name>.wav" -> name */
    if (strncasecmp(path, "KEY/", 4) != 0)
        return 0;
    size_t len = strlen(path + 4);
    if (len <= 4 || len - 4 > SOUND_NAME_MAX)
        return 0;
    memcpy(name, path + 4, len - 4);
    name[len - 4] = 0;
    return 1;
}

int sounds_key_list(char names[][SOUND_NAME_MAX + 1], int max) {
    int count = 0;
    for (unsigned i = 0; i < builtin_sound_count && count < max; i++)
        count += key_name_of(builtin_sounds[i].path, names[count]);
    int builtins = count;

    static char files[32][SOUND_NAME_MAX + 1];
    int found = use_files_ok ? statefs_list_files(KEY_DIR, ".wav", (char *) files, SOUND_NAME_MAX + 1, 32) : 0;
    for (int f = 0; f < found && count < max; f++) {
        bool dup = false;
        for (int b = 0; b < builtins; b++)
            dup |= strcasecmp(files[f], names[b]) == 0;
        if (!dup)
            strcpy(names[count++], files[f]);
    }
    return count;
}

/* ===================== the folder ===================== */

/* Makes the folders, and writes each built-in whose file is missing --
 * every one on the first boot. */
static void write_missing(void) {
    statefs_mkdir(SOUNDS_DIR);
    statefs_mkdir(KEY_DIR);
    statefs_mkdir(SOUNDS_DIR "/TONES");
    for (unsigned i = 0; i < builtin_sound_count; i++) {
        char full[64];
        snprintf(full, sizeof(full), "%s/%s", SOUNDS_DIR, builtin_sounds[i].path);
        watchdog_update();
        if (statefs_file_exists(full))
            continue;
        FILE *f = statefs_fopen(full, "wb");
        if (f == NULL)
            continue;
        fwrite(builtin_sounds[i].wav, 1, builtin_sounds[i].size, f);
        fclose(f);
        watchdog_update();
    }
    statefs_sync();
}

void sounds_init(bool use_files) {
    use_files_ok = use_files;
    if (use_files && statefs_ready())
        write_missing();
    char error[32];
    if (!sounds_key_select(px_settings.key_sound, error, sizeof(error)))
        printf("Key sound \"%s\": %s\n", px_settings.key_sound, error);
}
