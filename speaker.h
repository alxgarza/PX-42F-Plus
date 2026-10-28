#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The speaker sits between 3.3V and the drain of an N-channel MOSFET,
 * whose gate is GP15: high = transistor on (current through the speaker),
 * low = off. The pin is kept low whenever no sound is playing. GP15 is PWM
 * slice 7, channel B.
 *
 * Sounds are played as a stream of PWM duty values (sounds.c makes them
 * from WAV files): the MOSFET switches at sys_clk / 1024 -- 146.5kHz at
 * 150MHz, far above hearing -- and its duty follows the waveform; the
 * coil's inductance and the cone average the pulses. */
#define SPEAKER_PIN 15

/* Duty values go out at this rate (paced by a timer, by DMA), each
 * 0..SPEAKER_DUTY_MAX (= always on: ~200mA through the 16 ohm coil). */
#define SPEAKER_SAMPLE_HZ 32000
#define SPEAKER_DUTY_MAX  1024

/* Drives the gate low (transistor off). Call once, as early as possible
 * at startup. */
void speaker_init(void);

/* Fills buf with up to max duty values; returns how many it wrote, 0 when
 * the sound is over. May take its time (read a file): each 32ms block of
 * output is filled while the one before it plays. Called on core 0: what
 * it uses belongs to the sound until it's over (speaker_busy()). */
typedef uint32_t (*speaker_fill_fn)(void *ctx, uint16_t *buf, uint32_t max);

/* Core 0 plays the sounds (speaker_core0_poll(), from its idle loop), so
 * core 1 carries on meanwhile. These are core 1's. */

/* Starts a sound and returns at once. One already playing is stopped
 * first. Runs core 1 at full speed: the sample clock and the carrier come
 * from the system clock, and idle.c leaves it there while a sound plays. */
void speaker_start(speaker_fill_fn fill, void *ctx);

/* A sound is playing (or about to). */
bool speaker_busy(void);

/* Waits for the sound playing to end, feeding the watchdog. */
void speaker_wait(void);

/* Plays a sound to the end: speaker_start(), then speaker_wait(). */
void speaker_play(speaker_fill_fn fill, void *ctx);

/* Stops the sound playing (the speaker off within a few microseconds of
 * core 0 seeing it). Nothing to do if none is. */
void speaker_stop(void);

/* Core 0's idle loop calls this after every wakeup: it starts, fills and
 * stops what core 1 asked for. True while it wants to be called again
 * without sleeping first (the last few ms of a sound). */
bool speaker_core0_poll(void);

/* time_us_64() when the last sound ended (0 if none yet), or now while one
 * plays: the speaker's current pulls the battery voltage down, during a
 * sound and for a while after (battery.c). */
uint64_t speaker_last_tone_us(void);

#ifdef __cplusplus
}
#endif

#endif
