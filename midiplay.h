#ifndef MIDIPLAY_H
#define MIDIPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "dutyshape.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Playing Standard MIDI Files (.mid, formats 0 and 1, any number of
 * tracks up to MIDI_MAX_TRACKS): the file's events drive a small
 * polyphonic synthesizer, MIDI_VOICES notes at once on 16 channels, whose
 * output goes to the speaker as duty values (dutyshape.h). Standard C only,
 * so tests/test_parsers.c runs it on the computer; sounds.c is the
 * firmware's side (the file, the speaker).
 *
 * Instruments: a channel's program picks one of the synth's timbres (a
 * handful of additive waveforms with their own envelope -- piano, bell,
 * organ, plucked, bass, strings, brass, reed, flute, lead, pad) by General
 * MIDI family; channel 10 is the drum kit (kick, snare, hats, toms,
 * cymbals, clicks, made from noise and sweeping sines). Controllers: volume,
 * expression, modulation, sustain pedal, pitch bend (+-2 semitones), all
 * notes / sound off, reset. Everything else in the file is skipped.
 *
 * Damaged files are safe to play: every read is bounds-checked, an event
 * that runs past its track's end ends the track. */

#define MIDI_RATE       32000 /* = SPEAKER_SAMPLE_HZ (sounds.c checks) */
#define MIDI_MAX_TRACKS 32
#define MIDI_VOICES     24
#define MIDI_HIST       256   /* blocks of channel activity kept (a power of 2) */
#define MIDI_BLOCK      32    /* samples between sequencer steps: events land within 1ms */

typedef struct {
    const uint8_t *start, *end, *p;
    uint32_t next_tick;
    uint8_t running;
    bool done;
} midi_track_t;

typedef struct {
    uint8_t state, ch, note, timbre; /* timbre >= the melodic ones: a drum */
    bool sustained;                  /* released, but the pedal holds it */
    uint32_t phase, serial;
    float base_hz, env, amp, atk_step, decay_k, sus, rel_k;
    float a1, a2;                    /* drums: the sweeping pitch, the noise before */
} midi_voice_t;

typedef struct {
    uint8_t program, vol, expr, mod;
    bool pedal;
    float bend_ratio, gain;
} midi_channel_t;

typedef struct {
    midi_track_t trk[MIDI_MAX_TRACKS];
    int ntrk;
    uint16_t division;
    bool smpte;
    double ticks_per_sample, tick;
    uint32_t tempo_us;
    /* the length, found by playing the events without sound */
    bool dry;
    double scan_us;
    uint32_t scan_tick, length_ms;
    midi_channel_t ch[16];
    midi_voice_t v[MIDI_VOICES];
    uint32_t serial, rng, blocks, reported;
    uint32_t lit_until[16]; /* per channel: the block its box stays lit to */
    uint32_t mask_hist[MIDI_HIST]; /* channels whose box is lit in each block rendered, newest at blocks-1 */
    float lfo, limit_gain;
    float blk[MIDI_BLOCK];
    uint32_t blk_pos;
    dutyshape_t shape;
    bool seq_done, finished;
    uint32_t flushed;
    uint64_t samples;
} midi_player_t;

/* Checks the header and finds the tracks; data must stay where it is until
 * the music has ended. False, with the reason in *error, if it isn't a MIDI
 * file this can play. Builds the synth's waveforms the first time (some
 * ms): call it from the core that isn't playing. */
bool midi_open(midi_player_t *p, const uint8_t *data, uint32_t size, const char **error);

/* A speaker_fill_fn (speaker.h): the next duty values; 0 at the end. */
uint32_t midi_fill(void *ctx, uint16_t *buf, uint32_t max);

/* The song's length (to its last event), how far it has played, the
 * channels (bit n = channel n+1) lit since the last call (3/4 of each note's length from its start), and how many notes sound. */
uint32_t midi_length_ms(const midi_player_t *p);
uint32_t midi_elapsed_ms(const midi_player_t *p);
uint32_t midi_active_channels(midi_player_t *p); /* started a note since the last call */
int midi_active_voices(const midi_player_t *p);

#ifdef __cplusplus
}
#endif

#endif
