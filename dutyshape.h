#ifndef DUTYSHAPE_H
#define DUTYSHAPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turning a waveform into the speaker's duty values (speaker.h): the
 * waveform x, normalized to +-1, rides on a level e that follows its
 * loudness: duty = (e + x) / 2. With e >= |x| the duty never goes negative,
 * and e is 0 in silence -- no current through the coil when nothing plays.
 * e looks DUTY_LOOKAHEAD samples ahead (the output is delayed that much),
 * so it has risen before a sound starts instead of stepping up with it, and
 * falls back slowly -- a fast-moving e would be heard as distortion.
 * Standard C, so the host tests build it. */
#define DUTY_LOOKAHEAD 64     /* 2ms */
#define DUTY_ATTACK_K  0.075f /* 1 - exp(-1 / (0.4ms * 32kHz)) */
#define DUTY_RELEASE_K 0.99844f /* exp(-1 / (20ms * 32kHz)) */

typedef struct {
    float delay[DUTY_LOOKAHEAD];
    uint32_t pos;
    float env;
} dutyshape_t;

/* Takes the next sample (zeroed struct to start); returns the duty value
 * for the one DUTY_LOOKAHEAD samples back, 0..SPEAKER_DUTY_MAX. */
int dutyshape_next(dutyshape_t *s, float x);

#ifdef __cplusplus
}
#endif

#endif
