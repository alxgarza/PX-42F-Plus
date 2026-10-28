#include "sounds.h"

#include <stdio.h>
#include <stdlib.h>
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

/* A WAV file on the drive, by its full path. Its FILE gets a buffer of
 * its own, here: core 0 reads it while the sound plays (speaker.h), and
 * stdio would otherwise malloc() one there -- on the heap core 1's
 * Plus42 is using. One sound file is open at a time (play_stream). */
static bool open_file(stream_t *st, const char *full, uint32_t max_out, const char **error) {
    static char file_buf[512];
    *error = "not found";
    FILE *f = use_files_ok && statefs_ready() ? statefs_fopen(full, "rb") : NULL;
    if (f == NULL)
        return false;
    setvbuf(f, file_buf, _IOFBF, sizeof(file_buf));
    wav_source_t src;
    if (!wav_source_file(&src, f)) {
        fclose(f);
        *error = "can't read it";
        return false;
    }
    if (stream_open(st, src, max_out, error))
        return true;
    wav_close(&st->src);
    return false;
}

/* /SOUNDS/<path>, or the built-in if the file is missing or unusable. */
static bool open_sound(stream_t *st, const char *path, uint32_t max_out, const char **error) {
    char full[64];
    snprintf(full, sizeof(full), "%s/%s", SOUNDS_DIR, path);
    if (open_file(st, full, max_out, error))
        return true;
    const builtin_sound_t *b = builtin(path);
    if (b == NULL)
        return false;
    const char *builtin_error;
    return stream_open(st, wav_source_mem(b->wav, b->size), max_out, &builtin_error);
}

static stream_t play_stream; /* ~1KB: not on the stack */
static bool play_open;       /* play_stream holds a sound (its file open) */

/* Stops the sound playing, and closes the last one's file -- left open
 * by a sound that ended by itself, until the next one or this. */
void sounds_stop(void) {
    speaker_stop();
    if (play_open) {
        wav_close(&play_stream.src);
        play_open = false;
    }
}

bool sounds_playing(void) {
    return speaker_busy();
}

void sounds_wait(void) {
    speaker_wait();
}

/* play_stream, opened, to the speaker: waiting until it has played, or
 * not. */
static bool play_stream_now(bool wait) {
    play_open = true;
    speaker_start(stream_fill, &play_stream);
    if (wait)
        sounds_wait();
    return true;
}

static bool play(const char *path, bool wait) {
    sounds_stop();
    const char *error;
    return open_sound(&play_stream, path, UINT32_MAX, &error) && play_stream_now(wait);
}

void sounds_play_boot(void) {
    play("BOOT.wav", true);
}

void sounds_play_on(void) {
    play("ON.wav", false);
}

void sounds_play_off(void) {
    play("OFF.wav", true);
}

bool sounds_play_file(const char *path, bool wait) {
    sounds_stop();
    const char *error;
    return open_file(&play_stream, path, UINT32_MAX, &error) && play_stream_now(wait);
}

void sounds_play_tone(int tone, bool wait) {
    char path[20];
    if (tone >= 0 && tone <= 9)
        snprintf(path, sizeof(path), "TONES/TONE%d.wav", tone);
    else if (tone == 10)
        snprintf(path, sizeof(path), "ERROR.wav");
    else
        return;
    play(path, wait);
}

/* ===================== the key sounds, in RAM ===================== */

/* The Key Sound is a set (sounds.h): KEY/<name>/ENTRY.wav, OP.wav and
 * ENTER.wav, each at most KEY_SET_MAX_OUT -- or one sound for every key,
 * KEY/<name>.wav, at most KEY_MAX_OUT. A set's missing OP sounds like its
 * ENTRY, a missing ENTER like its OP. They share key_duty, one after the
 * other; each comes with its level's fall after it (KEY_TAIL). */
#define KEY_SET_MAX_OUT (SPEAKER_SAMPLE_HZ / 10) /* 100ms */
#define KEY_TAIL        (LOOKAHEAD + TAIL_MAX)

static uint16_t key_duty[KEY_SOUND_KINDS * (KEY_SET_MAX_OUT + KEY_TAIL)];
_Static_assert(KEY_MAX_OUT + KEY_TAIL <= KEY_SOUND_KINDS * (KEY_SET_MAX_OUT + KEY_TAIL),
               "a single key sound fits the buffer");
static uint32_t key_start[KEY_SOUND_KINDS], key_len[KEY_SOUND_KINDS]; /* key_len 0 = Off */

typedef struct {
    uint32_t pos, end;
} key_play_t;

static uint32_t key_fill(void *ctx, uint16_t *buf, uint32_t max) {
    key_play_t *k = (key_play_t *) ctx;
    uint32_t n = k->end - k->pos < max ? k->end - k->pos : max;
    memcpy(buf, key_duty + k->pos, n * sizeof(uint16_t));
    k->pos += n;
    return n;
}

void sounds_key_play(key_sound_t kind) {
    static key_play_t k; /* core 0 reads it as it plays */
    if (key_len[kind] == 0)
        return;
    sounds_stop();
    k = (key_play_t) {key_start[kind], key_start[kind] + key_len[kind]};
    speaker_start(key_fill, &k);
}

/* Converts sound path (at most max_out) into key_duty, from *used on, as
 * kind. */
static bool load_key(const char *path, uint32_t max_out, uint32_t *used, key_sound_t kind, const char **why) {
    if (!open_sound(&play_stream, path, max_out, why))
        return false;
    uint32_t n = *used, got;
    while (n < count_of(key_duty)
           && (got = stream_fill(&play_stream, key_duty + n,
                                 1024 < count_of(key_duty) - n ? 1024 : count_of(key_duty) - n)) != 0) {
        n += got;
        watchdog_update(); /* converting 250ms of sound takes a while */
    }
    wav_close(&play_stream.src);
    key_start[kind] = *used;
    key_len[kind] = n - *used;
    *used = n;
    return true;
}

/* The same sound as kind `from`. */
static void key_alias(key_sound_t kind, key_sound_t from) {
    key_start[kind] = key_start[from];
    key_len[kind] = key_len[from];
}

bool sounds_key_select(const char *name, char *error, size_t error_size) {
    sounds_stop(); /* key_duty and play_stream are about to change */
    memset(key_len, 0, sizeof(key_len));
    if (name[0] == 0)
        return true; /* Off */
    char path[24 + SOUND_NAME_MAX];
    uint32_t used = 0;
    const char *set_why, *why;
    snprintf(path, sizeof(path), "KEY/%s/ENTRY.wav", name);
    if (load_key(path, KEY_SET_MAX_OUT, &used, KEY_SOUND_ENTRY, &set_why)) {
        snprintf(path, sizeof(path), "KEY/%s/OP.wav", name);
        if (!load_key(path, KEY_SET_MAX_OUT, &used, KEY_SOUND_OP, &why))
            key_alias(KEY_SOUND_OP, KEY_SOUND_ENTRY);
        snprintf(path, sizeof(path), "KEY/%s/ENTER.wav", name);
        if (!load_key(path, KEY_SET_MAX_OUT, &used, KEY_SOUND_ENTER, &why))
            key_alias(KEY_SOUND_ENTER, KEY_SOUND_OP);
        return true;
    }
    snprintf(path, sizeof(path), "KEY/%s.wav", name);
    if (!load_key(path, KEY_MAX_OUT, &used, KEY_SOUND_ENTRY, &why)) {
        /* A set whose ENTRY.wav is there but unusable says why. */
        snprintf(error, error_size, "%s", strcmp(set_why, "not found") != 0 ? set_why : why);
        return false;
    }
    key_alias(KEY_SOUND_OP, KEY_SOUND_ENTRY);
    key_alias(KEY_SOUND_ENTER, KEY_SOUND_ENTRY);
    return true;
}

/* A built-in's Key Sound name: "KEY/<name>.wav", or "KEY/<name>/ENTRY.wav"
 * for a set (its other sounds aren't choices of their own). */
static int key_name_of(const char *path, char *name) {
    if (strncasecmp(path, "KEY/", 4) != 0)
        return 0;
    path += 4;
    const char *slash = strchr(path, '/');
    size_t len = slash != NULL ? (size_t) (slash - path) : strlen(path) - 4;
    if (slash != NULL ? strcasecmp(slash, "/ENTRY.wav") != 0 : strlen(path) <= 4)
        return 0;
    if (len == 0 || len > SOUND_NAME_MAX)
        return 0;
    memcpy(name, path, len);
    name[len] = 0;
    return 1;
}

int sounds_key_list(char names[][SOUND_NAME_MAX + 1], int max) {
    int count = 0;
    for (unsigned i = 0; i < builtin_sound_count && count < max; i++)
        count += key_name_of(builtin_sounds[i].path, names[count]);

    /* Then the files in /SOUNDS/KEY, and its folders with an ENTRY.wav. */
    static char found[2 * 32][SOUND_NAME_MAX + 1];
    int files = 0, dirs = 0;
    if (use_files_ok) {
        files = statefs_list_files(KEY_DIR, ".wav", (char *) found, SOUND_NAME_MAX + 1, 32);
        dirs = statefs_list_dirs(KEY_DIR, (char *) found[files], SOUND_NAME_MAX + 1, 32);
    }
    for (int f = 0; f < files + dirs && count < max; f++) {
        if (f >= files) {
            char entry[64];
            snprintf(entry, sizeof(entry), "%s/%s/ENTRY.wav", KEY_DIR, found[f]);
            if (!statefs_file_exists(entry))
                continue;
        }
        bool dup = false;
        for (int b = 0; b < count; b++)
            dup |= strcasecmp(found[f], names[b]) == 0;
        if (!dup)
            strcpy(names[count++], found[f]);
    }
    return count;
}

/* ===================== the folder ===================== */

/* /SOUNDS/BUILTIN.TXT records, for each built-in written to the drive, a
 * checksum of what was written: "<8 hex digits> <path>" lines. A file that
 * still matches it was never replaced, so when the built-in itself changes
 * (a new firmware) the file is brought up to date; one that doesn't is the
 * user's own and stays. A file from before the record counts as written
 * only if it matches today's built-in. */
#define RECORD_PATH SOUNDS_DIR "/BUILTIN.TXT"
#define MAX_RECORDS 40

static uint32_t recorded[MAX_RECORDS];
static bool has_record[MAX_RECORDS];

static uint32_t fnv1a(uint32_t h, const uint8_t *p, size_t n) {
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
#define FNV_START 0x811C9DC5u

static int builtin_index(const char *path) {
    for (unsigned i = 0; i < builtin_sound_count && i < MAX_RECORDS; i++)
        if (strcasecmp(builtin_sounds[i].path, path) == 0)
            return (int) i;
    return -1;
}

static void read_record(void) {
    memset(has_record, 0, sizeof(has_record));
    FILE *f = statefs_fopen(RECORD_PATH, "rb");
    if (f == NULL)
        return;
    char line[80];
    while (fgets(line, sizeof(line), f) != NULL) {
        char *end;
        uint32_t h = (uint32_t) strtoul(line, &end, 16);
        if (end != line + 8 || *end != ' ')
            continue;
        end[1 + strcspn(end + 1, "\r\n")] = 0;
        int i = builtin_index(end + 1);
        if (i >= 0) {
            recorded[i] = h;
            has_record[i] = true;
        }
    }
    fclose(f);
}

static void write_record(void) {
    FILE *f = statefs_fopen(RECORD_PATH, "wb");
    if (f == NULL)
        return;
    for (unsigned i = 0; i < builtin_sound_count && i < MAX_RECORDS; i++)
        if (has_record[i])
            fprintf(f, "%08lx %s\n", (unsigned long) recorded[i], builtin_sounds[i].path);
    fclose(f);
}

/* The checksum of a file on the drive; false if it can't be read. */
static bool file_checksum(const char *path, uint32_t *h) {
    FILE *f = statefs_fopen(path, "rb");
    if (f == NULL)
        return false;
    static uint8_t buf[512];
    size_t n;
    *h = FNV_START;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        *h = fnv1a(*h, buf, n);
        watchdog_update();
    }
    fclose(f);
    return true;
}

static bool write_builtin(unsigned i, char *full) {
    char *slash = strrchr(full, '/'); /* a set's folder, KEY/<name> */
    *slash = 0;
    statefs_mkdir(full);
    *slash = '/';
    FILE *f = statefs_fopen(full, "wb");
    if (f == NULL)
        return false;
    bool ok = fwrite(builtin_sounds[i].wav, 1, builtin_sounds[i].size, f) == builtin_sounds[i].size;
    fclose(f);
    return ok;
}

/* Makes the folders, writes each built-in whose file is missing -- every
 * one on the first boot -- and updates the ones still as written. */
static void write_missing(void) {
    statefs_mkdir(SOUNDS_DIR);
    statefs_mkdir(KEY_DIR);
    statefs_mkdir(SOUNDS_DIR "/TONES");
    read_record();
    bool changed = false;
    for (unsigned i = 0; i < builtin_sound_count && i < MAX_RECORDS; i++) {
        char full[64];
        snprintf(full, sizeof(full), "%s/%s", SOUNDS_DIR, builtin_sounds[i].path);
        watchdog_update();
        uint32_t h = fnv1a(FNV_START, builtin_sounds[i].wav, builtin_sounds[i].size);
        if (has_record[i] && recorded[i] == h && statefs_file_exists(full))
            continue; /* the usual case: nothing new */
        uint32_t on_drive;
        bool exists = statefs_file_exists(full) && file_checksum(full, &on_drive);
        bool ours = !exists || on_drive == h || (has_record[i] && on_drive == recorded[i]);
        if (!ours)
            continue; /* the user's own */
        if ((!exists || on_drive != h) && !write_builtin(i, full))
            continue;
        recorded[i] = h;
        has_record[i] = true;
        changed = true;
        watchdog_update();
    }
    if (changed)
        write_record();
    statefs_sync();
}

void sounds_init(bool use_files) {
    sounds_stop(); /* the files may be about to be written */
    use_files_ok = use_files;
    if (use_files && statefs_ready())
        write_missing();
    char error[32];
    if (!sounds_key_select(px_settings.key_sound, error, sizeof(error)))
        printf("Key sound \"%s\": %s\n", px_settings.key_sound, error);
}
