#ifndef SPEAKER_H
#define SPEAKER_H

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

/* Duty values go out at this rate (paced by a timer interrupt), each
 * 0..SPEAKER_DUTY_MAX (= always on: ~200mA through the 16 ohm coil). */
#define SPEAKER_SAMPLE_HZ 32000
#define SPEAKER_DUTY_MAX  1024

/* Drives the gate low (transistor off). Call once, as early as possible
 * at startup. */
void speaker_init(void);

/* Fills buf with up to max duty values; returns how many it wrote, 0 when
 * the sound is over. May take its time (read a file): a buffer ahead of
 * the output covers ~60ms. */
typedef uint32_t (*speaker_fill_fn)(void *ctx, uint16_t *buf, uint32_t max);

/* Plays a sound to the end, blocking (Free42's shell_beeper() is expected
 * to be synchronous); feeds the watchdog. Core 1. */
void speaker_play(speaker_fill_fn fill, void *ctx);

/* time_us_64() when the last sound ended (0 if none yet): the speaker's
 * current pulls the battery voltage down for a while (battery.c). */
uint64_t speaker_last_tone_us(void);

#ifdef __cplusplus
}
#endif

#endif
