#include "midiplay.h"

#include <math.h>
#include <string.h>

/* ===================== the timbres ===================== */

enum {
    T_PIANO, T_BELL, T_ORGAN, T_PLUCK, T_BASS, T_STRINGS, T_BRASS, T_REED, T_FLUTE, T_LEAD, T_PAD,
    T_COUNT
};

#define HARMONICS 8
typedef struct {
    float harm[HARMONICS]; /* amplitudes of harmonics 1..8 */
    float attack_ms, decay_ms, sustain, release_ms;
    float vibrato; /* depth as a fraction of the pitch, 5.5Hz */
} timbre_t;

/* decay_ms is the time constant of the fall from the attack's peak to the
 * sustain level: a piano (sustain 0) fades while the key is held, an organ
 * (sustain 1) doesn't. */
static const timbre_t timbres[T_COUNT] = {
    [T_PIANO]   = {{1, .55f, .35f, .18f, .10f, .06f, .03f, .015f}, 2, 1100, 0.0f, 150, 0},
    [T_BELL]    = {{1, .20f, 0, .60f, .10f, .30f, 0, .10f}, 1, 1500, 0.0f, 300, 0},
    [T_ORGAN]   = {{1, .70f, .50f, 0, .40f, 0, 0, .20f}, 8, 50, 1.0f, 40, 0},
    [T_PLUCK]   = {{1, .70f, .50f, .30f, .20f, .10f, .05f, .02f}, 1, 450, 0.0f, 60, 0},
    [T_BASS]    = {{1, .45f, .30f, .15f, .08f, 0, 0, 0}, 3, 300, 0.35f, 80, 0},
    [T_STRINGS] = {{1, .50f, .33f, .25f, .20f, .16f, .14f, .12f}, 90, 200, 0.8f, 160, 0.004f},
    [T_BRASS]   = {{1, .90f, .70f, .50f, .35f, .25f, .15f, .08f}, 40, 120, 0.75f, 70, 0.002f},
    [T_REED]    = {{1, 0, .75f, 0, .50f, 0, .25f, 0}, 25, 100, 0.85f, 60, 0.002f},
    [T_FLUTE]   = {{1, .15f, .05f, 0, 0, 0, 0, 0}, 50, 100, 0.9f, 80, 0.003f},
    [T_LEAD]    = {{1, 0, .33f, 0, .20f, 0, .14f, 0}, 3, 80, 0.9f, 50, 0.003f},
    [T_PAD]     = {{1, .40f, .25f, .10f, .05f, 0, 0, 0}, 250, 300, 0.85f, 400, 0.002f},
};

/* One cycle of each, 256 points, peak 1: [0] the full set of harmonics,
 * [1] only two, for notes above TABLE_HI_HZ (the others would fold back
 * over the Nyquist frequency as noise). The last, sine_tab, is for drums. */
#define TABLE_N     256
#define TABLE_HI_HZ 1800.0f
static int16_t tables[T_COUNT][2][TABLE_N];
static int16_t sine_tab[TABLE_N];
static bool tables_ready;

static void make_tables(void) {
    for (int t = 0; t < T_COUNT; t++)
        for (int k = 0; k < 2; k++) {
            float tab[TABLE_N], peak = 1e-6f;
            for (int i = 0; i < TABLE_N; i++) {
                float s = 0;
                for (int h = 0; h < (k ? 2 : HARMONICS); h++)
                    s += timbres[t].harm[h] * sinf(6.2831853f * (float) ((h + 1) * i) / TABLE_N);
                tab[i] = s;
                if (fabsf(s) > peak)
                    peak = fabsf(s);
            }
            for (int i = 0; i < TABLE_N; i++)
                tables[t][k][i] = (int16_t) (tab[i] / peak * 32000.0f);
        }
    for (int i = 0; i < TABLE_N; i++)
        sine_tab[i] = (int16_t) (sinf(6.2831853f * (float) i / TABLE_N) * 32000.0f);
    tables_ready = true;
}

/* The table at a 32-bit phase, interpolated, as -1..+1. */
static inline float table_at(const int16_t *tab, uint32_t phase) {
    uint32_t i = phase >> 24, frac = (phase >> 8) & 0xFFFF;
    int32_t a = tab[i], b = tab[(i + 1) & (TABLE_N - 1)];
    return (float) (a + (((b - a) * (int32_t) frac) >> 16)) * (1.0f / 32768.0f);
}

/* General MIDI program -> timbre, by family with a few exceptions. */
static int timbre_of(int program) {
    switch (program >> 3) {
        case 0: return program >= 6 ? T_PLUCK : T_PIANO; /* harpsichord, clavinet */
        case 1: return T_BELL;
        case 2: return T_ORGAN;
        case 3: return program >= 29 ? T_LEAD : T_PLUCK; /* overdriven, distortion guitars */
        case 4: return T_BASS;
        case 5: return program == 45 || program == 46 ? T_PLUCK : program == 47 ? T_BASS : T_STRINGS;
        case 6: return program >= 52 && program <= 54 ? T_FLUTE : T_STRINGS;
        case 7: return T_BRASS;
        case 8: return T_REED;
        case 9: return T_FLUTE;
        case 10: return T_LEAD;
        case 11: case 12: return T_PAD;
        case 13: return T_PLUCK;
        case 14: return T_BELL;
        default: return T_PAD;
    }
}

/* ===================== the drum kit ===================== */

enum { D_KICK, D_SNARE, D_HAT, D_TOM, D_CLICK, D_KINDS };
typedef struct {
    uint8_t kind;
    float hz;     /* the tone: a kick's end, a tom's, a click's */
    float tau_ms; /* the fall to 1/e */
} drum_t;

static drum_t drum_of(int note) {
    switch (note) {
        case 35: case 36: return (drum_t) {D_KICK, 50, 120};
        case 38: case 40: return (drum_t) {D_SNARE, 190, 110};
        case 41: return (drum_t) {D_TOM, 80, 220};
        case 43: return (drum_t) {D_TOM, 100, 200};
        case 45: return (drum_t) {D_TOM, 125, 190};
        case 47: return (drum_t) {D_TOM, 150, 180};
        case 48: case 50: return (drum_t) {D_TOM, 180, 170};
        case 42: case 44: case 54: case 70: return (drum_t) {D_HAT, 0, 30};
        case 46: return (drum_t) {D_HAT, 0, 180};
        case 49: case 52: case 55: case 57: return (drum_t) {D_HAT, 0, 700};
        case 51: case 53: case 59: return (drum_t) {D_HAT, 0, 400};
        case 56: return (drum_t) {D_CLICK, 1700, 70};
        case 37: case 39: return (drum_t) {D_CLICK, 1100, 35};
        default: return (drum_t) {D_CLICK, 800, 45};
    }
}

/* ===================== voices ===================== */

enum { V_OFF, V_ATTACK, V_DECAY, V_RELEASE };
#define DRUM_CHANNEL 9
#define ENV_OFF      0.0008f

static inline float ms_k(float ms) { /* per-sample factor of a fall with this time constant */
    return expf(-1.0f / (ms * (MIDI_RATE / 1000.0f)));
}

static midi_voice_t *alloc_voice(midi_player_t *p) {
    midi_voice_t *best = NULL;
    for (int i = 0; i < MIDI_VOICES; i++) {
        midi_voice_t *v = &p->v[i];
        if (v->state == V_OFF)
            return v;
        /* Steal: the quietest one already being released, else the oldest. */
        bool better;
        if (best == NULL)
            better = true;
        else if ((v->state == V_RELEASE) != (best->state == V_RELEASE))
            better = v->state == V_RELEASE;
        else if (v->state == V_RELEASE)
            better = v->env < best->env;
        else
            better = v->serial < best->serial;
        if (better)
            best = v;
    }
    return best;
}

static void release_voice(midi_voice_t *v) {
    v->state = V_RELEASE;
    v->sustained = false;
}

static void note_on(midi_player_t *p, int ch, int note, int vel) {
    float vf = (float) vel / 127.0f;
    float amp = 0.34f * (0.5f * vf + 0.5f * vf * vf);
    if (ch == DRUM_CHANNEL) {
        drum_t d = drum_of(note);
        if (d.kind == D_HAT && d.tau_ms < 100) /* a closed hat chokes an open one */
            for (int i = 0; i < MIDI_VOICES; i++)
                if (p->v[i].state != V_OFF && p->v[i].ch == DRUM_CHANNEL && p->v[i].timbre == T_COUNT + D_HAT
                    && p->v[i].decay_k > ms_k(100))
                    p->v[i].decay_k = ms_k(8);
        midi_voice_t *v = alloc_voice(p);
        memset(v, 0, sizeof(*v));
        v->state = V_DECAY;
        v->ch = (uint8_t) ch;
        v->note = (uint8_t) note;
        v->timbre = (uint8_t) (T_COUNT + d.kind);
        v->serial = ++p->serial;
        v->env = 1.0f;
        v->amp = amp * 1.6f;
        v->base_hz = d.hz;
        v->a1 = d.kind == D_TOM ? d.hz * 1.5f : d.kind == D_KICK ? 140.0f : 0.0f;
        v->decay_k = ms_k(d.tau_ms);
        return;
    }
    const timbre_t *tm = &timbres[timbre_of(p->ch[ch].program)];
    midi_voice_t *v = alloc_voice(p);
    memset(v, 0, sizeof(*v));
    v->state = V_ATTACK;
    v->ch = (uint8_t) ch;
    v->note = (uint8_t) note;
    v->timbre = (uint8_t) (tm - timbres);
    v->serial = ++p->serial;
    v->amp = amp;
    v->base_hz = 440.0f * exp2f((float) (note - 69) / 12.0f);
    v->atk_step = 1.0f / (tm->attack_ms * (MIDI_RATE / 1000.0f));
    v->decay_k = 1.0f - ms_k(tm->decay_ms);
    v->sus = tm->sustain;
    v->rel_k = ms_k(tm->release_ms);
}

static void note_off(midi_player_t *p, int ch, int note) {
    for (int i = 0; i < MIDI_VOICES; i++) {
        midi_voice_t *v = &p->v[i];
        if ((v->state == V_ATTACK || v->state == V_DECAY) && v->ch == ch && v->note == note
            && v->timbre < T_COUNT && !v->sustained) {
            if (p->ch[ch].pedal)
                v->sustained = true;
            else
                release_voice(v);
        }
    }
}

static void all_notes_off(midi_player_t *p, int ch, bool hard) {
    for (int i = 0; i < MIDI_VOICES; i++) {
        midi_voice_t *v = &p->v[i];
        if (v->state == V_OFF || (ch >= 0 && v->ch != ch))
            continue;
        if (hard)
            v->state = V_OFF;
        else if (v->timbre < T_COUNT)
            release_voice(v);
    }
}

static void reset_channel(midi_channel_t *c) {
    c->vol = 100;
    c->expr = 127;
    c->mod = 0;
    c->pedal = false;
    c->bend_ratio = 1.0f;
}

static void controller(midi_player_t *p, int ch, int cc, int val) {
    midi_channel_t *c = &p->ch[ch];
    switch (cc) {
        case 1: c->mod = (uint8_t) val; break;
        case 7: c->vol = (uint8_t) val; break;
        case 11: c->expr = (uint8_t) val; break;
        case 64:
            c->pedal = val >= 64;
            if (!c->pedal)
                for (int i = 0; i < MIDI_VOICES; i++)
                    if (p->v[i].state != V_OFF && p->v[i].ch == ch && p->v[i].sustained)
                        release_voice(&p->v[i]);
            break;
        case 120: all_notes_off(p, ch, true); break;
        case 121: reset_channel(c); break;
        case 123: all_notes_off(p, ch, false); break;
    }
}

static void channel_message(midi_player_t *p, int status, int d1, int d2) {
    int ch = status & 15;
    switch (status & 0xF0) {
        case 0x80: note_off(p, ch, d1); break;
        case 0x90:
            if (d2 > 0)
                note_on(p, ch, d1, d2);
            else
                note_off(p, ch, d1);
            break;
        case 0xB0: controller(p, ch, d1, d2); break;
        case 0xC0: p->ch[ch].program = (uint8_t) d1; break;
        case 0xE0: /* +-2 semitones */
            p->ch[ch].bend_ratio = exp2f((float) (((d2 << 7) | d1) - 8192) / 8192.0f * 2.0f / 12.0f);
            break;
    }
}

/* ===================== the file ===================== */

static void set_tempo(midi_player_t *p, uint32_t us) {
    p->tempo_us = us;
    if (!p->smpte)
        p->ticks_per_sample = (double) p->division * 1e6 / ((double) us * MIDI_RATE);
}

static double us_per_tick(const midi_player_t *p) {
    return p->smpte ? p->ticks_per_sample > 0 ? 1e6 / (p->ticks_per_sample * MIDI_RATE) : 0
                    : (double) p->tempo_us / p->division;
}

/* A variable-length number at t->p; false if the track ends first. */
static bool read_varlen(midi_track_t *t, uint32_t *value) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (t->p >= t->end)
            return false;
        uint8_t b = *t->p++;
        v = v << 7 | (b & 0x7F);
        if (!(b & 0x80)) {
            *value = v;
            return true;
        }
    }
    return false;
}

/* The delta time before the next event. */
static void read_delta(midi_track_t *t) {
    uint32_t delta;
    if (!read_varlen(t, &delta)) {
        t->done = true;
        return;
    }
    t->next_tick = t->next_tick + delta < t->next_tick ? UINT32_MAX : t->next_tick + delta;
}

/* How many blocks the note just started on ch should light its box: LIT_PERCENT
 * of its length, found by reading ahead in the track (a copy of it) for the
 * note off, or the same note again, at the tempo now. At least one block, so
 * even a drum hit shows; a note whose end isn't found within 4 s counts as
 * 4 s. */
#define LIT_PERCENT 75
static uint32_t lit_blocks(const midi_player_t *p, const midi_track_t *t, int ch, int note) {
    double ticks_per_block = p->ticks_per_sample * MIDI_BLOCK;
    if (ticks_per_block <= 0)
        return 1;
    double max_ticks = 4.0 * MIDI_RATE / MIDI_BLOCK * ticks_per_block;
    midi_track_t s = *t;
    uint32_t start = s.next_tick; /* the note's tick: the delta after it isn't read yet */
    read_delta(&s);
    double ticks = max_ticks;
    for (int n = 0; n < 4000 && !s.done && s.p < s.end; n++) {
        if ((double) (s.next_tick - start) >= max_ticks)
            break;
        int status = *s.p;
        if (status & 0x80)
            s.p++;
        else if (s.running)
            status = s.running;
        else
            break;
        uint32_t len = 0;
        if (status >= 0xF0) {
            s.running = 0;
            if (status == 0xFF) {
                if (s.p >= s.end)
                    break;
                int type = *s.p++;
                if (!read_varlen(&s, &len))
                    break;
                if (type == 0x2F) { /* the track ends: so does the note */
                    ticks = s.next_tick - start;
                    break;
                }
            } else if (status == 0xF0 || status == 0xF7) {
                if (!read_varlen(&s, &len))
                    break;
            } else {
                len = status == 0xF2 ? 2 : status == 0xF1 || status == 0xF3 ? 1 : 0;
            }
            if (len > (uint32_t) (s.end - s.p))
                break;
            s.p += len;
        } else {
            s.running = (uint8_t) status;
            int bytes = status >= 0xC0 && status < 0xE0 ? 1 : 2;
            if (bytes > s.end - s.p)
                break;
            int d1 = s.p[0] & 0x7F;
            s.p += bytes;
            if ((status & 15) == ch && d1 == note
                && ((status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90))) {
                ticks = s.next_tick - start; /* the off, or the note struck again */
                break;
            }
        }
        read_delta(&s);
    }
    uint32_t blocks = (uint32_t) (ticks / ticks_per_block * LIT_PERCENT / 100);
    return blocks ? blocks : 1;
}

/* Does the event at t->p, then reads the delta after it. */
static void do_event(midi_player_t *p, midi_track_t *t) {
    if (p->dry) { /* the time at this event, at the tempo so far */
        p->scan_us += (double) (t->next_tick - p->scan_tick) * us_per_tick(p);
        p->scan_tick = t->next_tick;
    }
    if (t->p >= t->end) {
        t->done = true;
        return;
    }
    int status = *t->p;
    if (status & 0x80) {
        t->p++;
    } else if (t->running) {
        status = t->running;
    } else {
        t->done = true; /* data without a status */
        return;
    }
    if (status >= 0xF0) {
        t->running = 0;
        uint32_t len = 0;
        if (status == 0xFF) {
            if (t->p >= t->end) {
                t->done = true;
                return;
            }
            int type = *t->p++;
            if (!read_varlen(t, &len) || len > (uint32_t) (t->end - t->p)) {
                t->done = true;
                return;
            }
            if (type == 0x2F) {
                t->done = true;
                return;
            }
            if (type == 0x51 && len == 3) {
                uint32_t us = (uint32_t) t->p[0] << 16 | (uint32_t) t->p[1] << 8 | t->p[2];
                if (us > 0)
                    set_tempo(p, us);
            }
        } else if (status == 0xF0 || status == 0xF7) {
            if (!read_varlen(t, &len) || len > (uint32_t) (t->end - t->p)) {
                t->done = true;
                return;
            }
        } else {
            len = status == 0xF2 ? 2 : status == 0xF1 || status == 0xF3 ? 1 : 0;
            if (len > (uint32_t) (t->end - t->p)) {
                t->done = true;
                return;
            }
        }
        t->p += len;
    } else {
        t->running = (uint8_t) status;
        int n = status >= 0xC0 && status < 0xE0 ? 1 : 2;
        if (n > t->end - t->p) {
            t->done = true;
            return;
        }
        int d1 = t->p[0] & 0x7F, d2 = n == 2 ? t->p[1] & 0x7F : 0;
        t->p += n;
        if (!p->dry) {
            if ((status & 0xF0) == 0x90 && d2 > 0) {
                uint32_t until = p->blocks + lit_blocks(p, t, status & 15, d1);
                if (until > p->lit_until[status & 15])
                    p->lit_until[status & 15] = until;
            }
            channel_message(p, status, d1, d2);
        }
    }
    read_delta(t);
}

/* Does every event due by tick `limit`, in order across the tracks. */
static void run_events(midi_player_t *p, double limit) {
    while (true) {
        midi_track_t *best = NULL;
        for (int i = 0; i < p->ntrk; i++)
            if (!p->trk[i].done && (best == NULL || p->trk[i].next_tick < best->next_tick))
                best = &p->trk[i];
        if (best == NULL) {
            p->seq_done = true;
            return;
        }
        if ((double) best->next_tick > limit)
            return;
        do_event(p, best);
    }
}

static void rewind_tracks(midi_player_t *p) {
    for (int i = 0; i < p->ntrk; i++) {
        midi_track_t *t = &p->trk[i];
        t->p = t->start;
        t->next_tick = 0;
        t->running = 0;
        t->done = false;
        read_delta(t);
    }
    p->seq_done = false;
    p->tick = 0;
    set_tempo(p, 500000);
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static uint32_t be16(const uint8_t *p) {
    return (uint32_t) p[0] << 8 | p[1];
}

bool midi_open(midi_player_t *p, const uint8_t *data, uint32_t size, const char **error) {
    if (!tables_ready)
        make_tables();
    memset(p, 0, sizeof(*p));
    if (size < 14 || memcmp(data, "MThd", 4) != 0) {
        *error = "not a MIDI file";
        return false;
    }
    uint32_t hlen = be32(data + 4);
    if (hlen < 6 || hlen > size - 8) {
        *error = "damaged header";
        return false;
    }
    p->division = (uint16_t) be16(data + 12);
    if (p->division & 0x8000) { /* SMPTE: frames a second, ticks a frame */
        int fps = 256 - (int) (p->division >> 8), sub = p->division & 0xFF;
        if (fps < 24 || fps > 30 || sub == 0) {
            *error = "bad time division";
            return false;
        }
        p->smpte = true;
        p->ticks_per_sample = (fps == 29 ? 29.97 : (double) fps) * sub / MIDI_RATE;
    } else if (p->division == 0) {
        *error = "bad time division";
        return false;
    }
    uint32_t pos = 8 + hlen;
    while (pos + 8 <= size && p->ntrk < MIDI_MAX_TRACKS) {
        uint32_t len = be32(data + pos + 4), body = pos + 8;
        if (len > size - body)
            len = size - body; /* a track cut short: what is there plays */
        if (memcmp(data + pos, "MTrk", 4) == 0) {
            p->trk[p->ntrk].start = data + body;
            p->trk[p->ntrk].end = data + body + len;
            p->ntrk++;
        }
        pos = body + len;
    }
    if (p->ntrk == 0) {
        *error = "no music in it";
        return false;
    }

    p->dry = true;
    rewind_tracks(p);
    run_events(p, 1e300);
    p->dry = false;
    p->length_ms = (uint32_t) (p->scan_us / 1000.0 < 4e9 ? p->scan_us / 1000.0 : 4e9);

    rewind_tracks(p);
    p->rng = 2463534242u;
    p->limit_gain = 1.0f;
    p->blocks = 0;
    p->reported = 0;
    memset(p->lit_until, 0, sizeof(p->lit_until));
    memset(p->mask_hist, 0, sizeof(p->mask_hist));
    p->blk_pos = MIDI_BLOCK;
    for (int i = 0; i < 16; i++)
        reset_channel(&p->ch[i]);
    return true;
}

/* ===================== the synth ===================== */

static inline float noise(midi_player_t *p) { /* xorshift32 */
    p->rng ^= p->rng << 13;
    p->rng ^= p->rng >> 17;
    p->rng ^= p->rng << 5;
    return (float) (int32_t) p->rng * (1.0f / 2147483648.0f);
}

static inline uint32_t hz_to_inc(float hz) {
    return (uint32_t) (hz * (4294967296.0f / MIDI_RATE));
}

static void render_melodic(midi_player_t *p, midi_voice_t *v, float lfo, float *mix) {
    const timbre_t *tm = &timbres[v->timbre];
    const midi_channel_t *c = &p->ch[v->ch];
    const int16_t *tab = tables[v->timbre][v->base_hz > TABLE_HI_HZ];
    float hz = v->base_hz * c->bend_ratio * (1.0f + (tm->vibrato + (float) c->mod * 0.0001f) * lfo);
    uint32_t inc = hz_to_inc(hz > 15000.0f ? 15000.0f : hz);
    float amp = v->amp * c->gain, env = v->env;
    uint32_t phase = v->phase;
    int state = v->state;
    for (int i = 0; i < MIDI_BLOCK; i++) {
        if (state == V_ATTACK) {
            env += v->atk_step;
            if (env >= 1.0f) {
                env = 1.0f;
                state = V_DECAY;
            }
        } else if (state == V_DECAY) {
            env += (v->sus - env) * v->decay_k;
        } else {
            env *= v->rel_k;
        }
        if (env < ENV_OFF && state != V_ATTACK) {
            state = V_OFF;
            break;
        }
        mix[i] += table_at(tab, phase) * env * amp;
        phase += inc;
    }
    v->env = env;
    v->phase = phase;
    v->state = (uint8_t) state;
}

static void render_drum(midi_player_t *p, midi_voice_t *v, float *mix) {
    float amp = v->amp * p->ch[v->ch].gain, env = v->env;
    uint32_t phase = v->phase;
    for (int i = 0; i < MIDI_BLOCK; i++) {
        env *= v->decay_k;
        if (env < ENV_OFF) {
            v->state = V_OFF;
            break;
        }
        float s;
        switch (v->timbre - T_COUNT) {
            case D_KICK:
                v->a1 = v->base_hz + (v->a1 - v->base_hz) * 0.9995f;
                phase += hz_to_inc(v->a1);
                s = table_at(sine_tab, phase) * 0.9f;
                break;
            case D_SNARE: {
                phase += hz_to_inc(v->base_hz);
                s = noise(p) * 0.55f * env + table_at(sine_tab, phase) * 0.5f * env * env;
                break;
            }
            case D_TOM:
                v->a1 = v->base_hz + (v->a1 - v->base_hz) * 0.9993f;
                phase += hz_to_inc(v->a1);
                s = table_at(sine_tab, phase);
                break;
            case D_HAT: {
                float n = noise(p);
                s = (n - v->a2) * 0.5f; /* highpassed: the difference from the last */
                v->a2 = n;
                break;
            }
            default: { /* D_CLICK */
                float n = noise(p);
                phase += hz_to_inc(v->base_hz);
                s = table_at(sine_tab, phase) * 0.6f + (n - v->a2) * 0.2f;
                v->a2 = n;
                break;
            }
        }
        mix[i] += s * env * amp;
    }
    v->env = env;
    v->phase = phase;
}

/* The sum of the voices is raised by MASTER -- one note alone is nearly full
 * scale, as the WAV sounds are (the speaker is small) -- and a limiter takes
 * the gain back down at once when a chord or a drum hit would pass LIMIT,
 * and lets it up over about 250ms, so many notes at once get no louder
 * instead of distorting. */
#define MASTER      4.0f
#define LIMIT       0.95f
#define LIMIT_UP_K  0.000125f /* 1 / (250ms * 32kHz) */

static inline float soft_clip(float x) { /* smooth up to +-1, then flat */
    if (x >= 1.0f)
        return 1.0f;
    if (x <= -1.0f)
        return -1.0f;
    return x * (1.5f - 0.5f * x * x);
}

/* The next MIDI_BLOCK samples into p->blk: first the events due, then the
 * voices. */
static void render_block(midi_player_t *p) {
    if (!p->seq_done) {
        run_events(p, p->tick);
        p->tick += p->ticks_per_sample * MIDI_BLOCK;
        if (p->seq_done) { /* the song is over: let the notes go */
            all_notes_off(p, -1, false);
            for (int i = 0; i < 16; i++)
                p->ch[i].pedal = false;
        }
    }
    for (int i = 0; i < 16; i++) {
        float v = (float) p->ch[i].vol / 127.0f, e = (float) p->ch[i].expr / 127.0f;
        p->ch[i].gain = v * e;
    }
    static const float lfo_step = 6.2831853f * 5.5f * MIDI_BLOCK / MIDI_RATE;
    p->lfo += lfo_step;
    if (p->lfo > 6.2831853f)
        p->lfo -= 6.2831853f;
    float lfo = sinf(p->lfo);

    float mix[MIDI_BLOCK] = {0};
    int alive = 0;
    for (int i = 0; i < MIDI_VOICES; i++) {
        midi_voice_t *v = &p->v[i];
        if (v->state == V_OFF)
            continue;
        if (v->timbre < T_COUNT)
            render_melodic(p, v, lfo, mix);
        else
            render_drum(p, v, mix);
        if (v->state != V_OFF) {
            alive++;
        }
    }
    uint32_t lit = 0;
    for (int i = 0; i < 16; i++)
        lit |= (uint32_t) (p->blocks < p->lit_until[i]) << i;
    p->mask_hist[p->blocks++ % MIDI_HIST] = lit;
    float g = p->limit_gain;
    for (int i = 0; i < MIDI_BLOCK; i++) {
        float x = mix[i] * MASTER, a = x < 0 ? -x : x;
        if (a * g > LIMIT)
            g = LIMIT / a;
        p->blk[i] = soft_clip(x * g);
        g += (1.0f - g) * LIMIT_UP_K;
    }
    p->limit_gain = g;
    if (p->seq_done && alive == 0)
        p->finished = true;
}

uint32_t midi_fill(void *ctx, uint16_t *buf, uint32_t max) {
    midi_player_t *p = (midi_player_t *) ctx;
    uint32_t n = 0;
    while (n < max) {
        if (p->blk_pos == MIDI_BLOCK) {
            render_block(p);
            p->blk_pos = 0;
        }
        int duty = dutyshape_next(&p->shape, p->blk[p->blk_pos++]);
        p->samples++;
        if (p->finished && (++p->flushed > DUTY_LOOKAHEAD && duty == 0))
            break; /* faded all the way out */
        if (p->finished && p->flushed > DUTY_LOOKAHEAD + 4000)
            break;
        buf[n++] = (uint16_t) duty;
    }
    return n;
}

uint32_t midi_length_ms(const midi_player_t *p) { return p->length_ms; }

/* The filler renders ahead of what the speaker is playing: it refills a
 * 1024-sample DMA block (32 ms) as soon as the other one starts, so the
 * rendered position leads the sound by 32-64 ms, 48 on average. Position and
 * activity are reported as of that much earlier, or the boxes light before
 * the beat is heard. (An estimate: the DMA position isn't read.) */
#define LATENCY_BLOCKS (48 * MIDI_RATE / 1000 / MIDI_BLOCK)
/* Each lit stretch is reported to the first poll after it is heard, so even
 * a hit shorter than the poll interval shows once. */

uint32_t midi_elapsed_ms(const midi_player_t *p) {
    uint64_t heard = p->samples > LATENCY_BLOCKS * MIDI_BLOCK ? p->samples - LATENCY_BLOCKS * MIDI_BLOCK : 0;
    return (uint32_t) (heard * 1000 / MIDI_RATE);
}

uint32_t midi_active_channels(midi_player_t *p) {
    uint32_t heard = p->blocks > LATENCY_BLOCKS ? p->blocks - LATENCY_BLOCKS : 0, mask = 0;
    if (heard - p->reported > MIDI_HIST / 2) /* polled too rarely: only the recent */
        p->reported = heard - MIDI_HIST / 2;
    for (; p->reported < heard; p->reported++)
        mask |= p->mask_hist[p->reported % MIDI_HIST];
    return mask;
}

int midi_active_voices(const midi_player_t *p) {
    int n = 0;
    for (int i = 0; i < MIDI_VOICES; i++)
        n += p->v[i].state != V_OFF;
    return n;
}
