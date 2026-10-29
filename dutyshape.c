#include "dutyshape.h"

#include "speaker.h"

int dutyshape_next(dutyshape_t *s, float x) {
    float out = s->delay[s->pos];
    s->delay[s->pos] = x;
    s->pos = (s->pos + 1) % DUTY_LOOKAHEAD;

    /* The level: the loudest of what's coming, smoothed. */
    float target = 0.0f;
    for (unsigned i = 0; i < DUTY_LOOKAHEAD; i++) {
        float a = s->delay[i] < 0 ? -s->delay[i] : s->delay[i];
        if (a > target)
            target = a;
    }
    if (target > s->env)
        s->env += (target - s->env) * DUTY_ATTACK_K;
    else
        s->env = s->env * DUTY_RELEASE_K > target ? s->env * DUTY_RELEASE_K : target;
    float e = s->env;
    float a = out < 0 ? -out : out;
    if (e < a)
        e = a;
    if (e > 1.0f)
        e = 1.0f;

    int duty = (int) ((e + out) * 0.5f * SPEAKER_DUTY_MAX + 0.5f);
    return duty < 0 ? 0 : duty > SPEAKER_DUTY_MAX ? SPEAKER_DUTY_MAX : duty;
}
