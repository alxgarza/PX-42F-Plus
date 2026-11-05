#include <stdio.h>
#include <string.h>

#include "stopwatch.h"
#include "timemodule.h"

#define WRAP_MS 360000000 /* past 99:59:59.99 it starts again from zero */
#define IMAGE_MAGIC 0x31575350u /* "PSW1" */

typedef struct {
    uint32_t magic;
    uint32_t running;
    int64_t value_ms;   /* the time at origin_ms */
    uint64_t origin_ms;
} stopwatch_timer_t;

static stopwatch_timer_t timer = {IMAGE_MAGIC, 0, 0, 0};
static bool dirty;

void stopwatch_reset(void) {
    timer.running = 0;
    timer.value_ms = 0;
    timer.origin_ms = 0;
    dirty = false;
}

int64_t stopwatch_ms(uint64_t now_ms) {
    int64_t t = timer.value_ms;
    if (timer.running)
        t += (int64_t) (now_ms - timer.origin_ms);
    return t >= WRAP_MS ? t % WRAP_MS : t;
}

bool stopwatch_running(void) {
    return timer.running != 0;
}

void stopwatch_run(uint64_t now_ms) {
    if (timer.running)
        return;
    timer.running = 1;
    timer.origin_ms = now_ms;
    dirty = true;
}

void stopwatch_stop(uint64_t now_ms) {
    if (!timer.running)
        return;
    timer.value_ms = stopwatch_ms(now_ms);
    timer.running = 0;
    dirty = true;
}

void stopwatch_set(int64_t ms, uint64_t now_ms) {
    timer.value_ms = ms;
    timer.origin_ms = now_ms;
    dirty = true;
}

void stopwatch_clock_moved(int64_t delta_ms) {
    if (timer.running) {
        timer.origin_ms += (uint64_t) delta_ms;
        dirty = true;
    }
}

uint64_t stopwatch_timer_at(void) {
    return timer.running && timer.value_ms < 0 ? timer.origin_ms + (uint64_t) -timer.value_ms : 0;
}

void stopwatch_timer_done(uint64_t now_ms) {
    stopwatch_set(stopwatch_ms(now_ms), now_ms);
}

bool stopwatch_dirty(void) {
    return dirty;
}

void stopwatch_mark_saved(void) {
    dirty = false;
}

bool stopwatch_is_zero(void) {
    return !timer.running && timer.value_ms == 0;
}

const void *stopwatch_image(size_t *size) {
    *size = sizeof(timer);
    return &timer;
}

bool stopwatch_load(const void *data, size_t size) {
    stopwatch_timer_t t;
    if (size != sizeof(t))
        return false;
    memcpy(&t, data, sizeof(t));
    if (t.magic != IMAGE_MAGIC || t.running > 1 || t.value_ms <= -WRAP_MS || t.value_ms >= WRAP_MS)
        return false;
    timer = t;
    dirty = false;
    return true;
}

/* The keyboard. The storing and the recalling pointer are kept apart, [0] and [1],
 * and R / D are only two ways of showing one of them (the manual's page 270). */
static struct {
    int pointer[2];
    bool recall, delta, hide_pointer, three_digits;
    int typed, typed_digits;
    bool frozen, split_held;
    int64_t frozen_ms;
} kb;

void stopwatch_keyboard_open(int store, int recall, bool delta) {
    memset(&kb, 0, sizeof(kb));
    kb.pointer[0] = store;
    kb.pointer[1] = recall;
    kb.delta = delta;
    kb.three_digits = store >= 100;
}

static int64_t ms_to_digits(int64_t ms) {
    return ms < 0 ? -time_ms_to_digits((uint64_t) -ms) : time_ms_to_digits((uint64_t) ms);
}

static bool digits_to_ms(int64_t digits, int64_t *ms) {
    bool negative = digits < 0;
    if (!time_digits_to_ms(negative ? -digits : digits, ms))
        return false;
    if (negative)
        *ms = -*ms;
    return true;
}

/* Moved there, by a key or a split: the beep, and in Recall mode a register that
 * isn't there ends the keyboard. BST doesn't beep, going away from the end. */
static int pointer_moved(const stopwatch_regs_t *regs, bool beep) {
    int at = kb.pointer[kb.recall];
    int size = regs->size();
    if (at >= 100)
        kb.three_digits = true;
    if (kb.recall && at >= size)
        return STOPWATCH_LEAVE | STOPWATCH_NONEXISTENT;
    return beep && at >= size - 1 ? STOPWATCH_BEEP : 0;
}

int stopwatch_key_down(stopwatch_key_t key, uint64_t now_ms, const stopwatch_regs_t *regs) {
    int *pointer = &kb.pointer[kb.recall];
    if (key >= STOPWATCH_KEY_0 && key < STOPWATCH_KEY_OTHER) {
        kb.typed = kb.typed * 10 + (int) (key - STOPWATCH_KEY_0);
        if (++kb.typed_digits < (kb.three_digits ? 3 : 2))
            return 0;
        *pointer = kb.typed;
        kb.typed = kb.typed_digits = 0;
        return pointer_moved(regs, true);
    }
    kb.typed = kb.typed_digits = 0;
    switch (key) {
        case STOPWATCH_KEY_RUN:
            kb.recall = false;
            if (timer.running)
                stopwatch_stop(now_ms);
            else
                stopwatch_run(now_ms);
            return 0;
        case STOPWATCH_KEY_CLEAR:
            if (kb.recall)
                kb.recall = false;
            else if (!timer.running)
                stopwatch_set(0, now_ms);
            return 0;
        case STOPWATCH_KEY_SPLIT:
            kb.recall = false;
            if (!regs->put(kb.pointer[0], ms_to_digits(stopwatch_ms(now_ms))))
                return STOPWATCH_LEAVE | STOPWATCH_NONEXISTENT;
            kb.split_held = true;
            return 0;
        case STOPWATCH_KEY_RECALL:
            kb.recall = !kb.recall;
            return pointer_moved(regs, false);
        case STOPWATCH_KEY_DELTA:
            kb.delta = !kb.delta;
            return 0;
        case STOPWATCH_KEY_SST:
            if (*pointer < STOPWATCH_POINTER_MAX)
                ++*pointer;
            return pointer_moved(regs, true);
        case STOPWATCH_KEY_BST:
            if (*pointer > 0)
                --*pointer;
            return pointer_moved(regs, false);
        case STOPWATCH_KEY_REG:
            kb.hide_pointer = !kb.hide_pointer;
            return 0;
        case STOPWATCH_KEY_RNNN:
            kb.three_digits = !kb.three_digits;
            if (!kb.three_digits)
                *pointer %= 100;
            return 0;
        default:
            kb.frozen = true;
            kb.frozen_ms = stopwatch_ms(now_ms);
            return 0;
    }
}

int stopwatch_key_up(const stopwatch_regs_t *regs) {
    kb.frozen = false;
    if (!kb.split_held)
        return 0;
    kb.split_held = false;
    if (kb.pointer[0] < STOPWATCH_POINTER_MAX)
        kb.pointer[0]++;
    return pointer_moved(regs, true);
}

bool stopwatch_ticking(void) {
    return timer.running && !kb.recall && !kb.frozen && !kb.split_held;
}

void stopwatch_pointers(int *store, int *recall, bool *delta) {
    *store = kb.pointer[0];
    *recall = kb.pointer[1];
    if (kb.typed_digits > 0)
        *(kb.recall ? recall : store) = 0;
    *delta = kb.delta;
}

/* HH:MM:SS.ss from a register's digits as they are: minutes and seconds over 59
 * show, only hours over 99 can't (the manual's page 271). */
static bool time_text(char *text, size_t size, int64_t digits, bool hundredths) {
    int64_t d = digits < 0 ? -digits : digits;
    if (d / 1000000 > 99)
        return false;
    int n = snprintf(text, size, "%s%02d:%02d:%02d.%02d", digits < 0 ? "-" : "", (int) (d / 1000000),
                     (int) (d / 10000 % 100), (int) (d / 100 % 100), (int) (d % 100));
    if (!hundredths) /* the running display: one digit fewer, in its place */
        text[n - 1] = ' ';
    return true;
}

/* Register `at` less the one before. Register 0 has none before it, so it's
 * itself. In storing, a difference that makes no sense shows the register alone;
 * in recalling it's an error (page 273). */
static bool static_digits(int at, const stopwatch_regs_t *regs, int64_t *digits) {
    int64_t now_digits, before_digits, now_ms, before_ms;
    if (regs->get(at, &now_digits) != STOPWATCH_REG_OK)
        return false;
    *digits = now_digits;
    if (!kb.delta || at == 0)
        return true;
    bool fine = digits_to_ms(now_digits, &now_ms) && regs->get(at - 1, &before_digits) == STOPWATCH_REG_OK
                && digits_to_ms(before_digits, &before_ms) && now_ms > before_ms;
    if (fine)
        *digits = ms_to_digits(now_ms - before_ms);
    return fine || !kb.recall;
}

static void pointer_text(char *text, size_t size) {
    int at = kb.pointer[kb.recall];
    char sign = kb.recall ? '=' : '>', kind = kb.delta ? 'D' : 'R';
    int width = kb.three_digits ? 3 : 2;
    if (kb.typed_digits > 0)
        snprintf(text, size, "%c%c%0*d%.*s", sign, kind, kb.typed_digits, kb.typed, width - kb.typed_digits, "__");
    else
        snprintf(text, size, "%c%c%0*d", sign, kind, width, at);
}

void stopwatch_text(char *text, size_t size, uint64_t now_ms, const stopwatch_regs_t *regs) {
    char pointer[8] = "";
    if (!kb.hide_pointer)
        pointer_text(pointer, sizeof(pointer));

    char time[16];
    int64_t digits = 0;
    bool fine = true, hundredths = true;
    if (kb.recall || kb.split_held)
        fine = static_digits(kb.pointer[kb.recall], regs, &digits);
    else if (kb.frozen)
        digits = ms_to_digits(kb.frozen_ms);
    else {
        digits = ms_to_digits(stopwatch_ms(now_ms));
        hundredths = !timer.running;
    }
    if (fine && time_text(time, sizeof(time), digits, hundredths))
        snprintf(text, size, "%s%s", time, pointer);
    else
        snprintf(text, size, "ERROR %s", pointer[0] != 0 ? pointer : (kb.recall ? "=" : ">"));
}
