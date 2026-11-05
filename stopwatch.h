#ifndef STOPWATCH_H
#define STOPWATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The HP-41 Time Module's stopwatch (HP-41CX owner's manual, volume 2, section
 * 17), free of hardware for tests/run.sh: the caller passes the clock in, and
 * the data registers as functions. A time is in ms, negative counting up to
 * zero; in a register it is HH.MMSSss, passed here as that number x 1000000. */

#define STOPWATCH_POINTER_MAX 999
#define STOPWATCH_TEXT_MAX 24

/* The timer. It counts from the always-on clock, so it runs through OFF. */
void stopwatch_reset(void);
int64_t stopwatch_ms(uint64_t now_ms);
bool stopwatch_running(void);
void stopwatch_run(uint64_t now_ms);
void stopwatch_stop(uint64_t now_ms);
void stopwatch_set(int64_t ms, uint64_t now_ms);
/* The clock was set: a change of time isn't a stopwatch time. */
void stopwatch_clock_moved(int64_t delta_ms);
/* When a negative time reaches zero, on the clock; 0 if none is on its way. */
uint64_t stopwatch_timer_at(void);
void stopwatch_timer_done(uint64_t now_ms);
/* Kept on the drive at OFF, like the alarms; only when it has changed. */
bool stopwatch_dirty(void);
void stopwatch_mark_saved(void);
bool stopwatch_is_zero(void);
const void *stopwatch_image(size_t *size);
bool stopwatch_load(const void *data, size_t size);

/* The data registers. */
typedef enum { STOPWATCH_REG_OK, STOPWATCH_REG_MISSING, STOPWATCH_REG_NOT_TIME } stopwatch_reg_t;
typedef struct {
    int (*size)(void);
    stopwatch_reg_t (*get)(int n, int64_t *digits);
    bool (*put)(int n, int64_t digits);
} stopwatch_regs_t;

/* The keyboard: places as on the HP-41's overlay, which the HP-42S's ENTER row
 * matches. */
typedef enum {
    STOPWATCH_KEY_RUN,
    STOPWATCH_KEY_CLEAR,
    STOPWATCH_KEY_SPLIT,
    STOPWATCH_KEY_RECALL,
    STOPWATCH_KEY_DELTA,
    STOPWATCH_KEY_SST,
    STOPWATCH_KEY_BST,
    STOPWATCH_KEY_REG,
    STOPWATCH_KEY_RNNN,
    STOPWATCH_KEY_0,          /* and the other digits: + the digit */
    STOPWATCH_KEY_OTHER = STOPWATCH_KEY_0 + 10
} stopwatch_key_t;

enum {
    STOPWATCH_BEEP = 1,        /* the pointer reached the last register */
    STOPWATCH_LEAVE = 2,       /* the keyboard ends */
    STOPWATCH_NONEXISTENT = 4  /* ... with this error */
};

void stopwatch_keyboard_open(int store, int recall, bool delta);
/* Each returns the flags above. A split's pointer moves when the key comes up. */
int stopwatch_key_down(stopwatch_key_t key, uint64_t now_ms, const stopwatch_regs_t *regs);
int stopwatch_key_up(const stopwatch_regs_t *regs);
/* The running display changes: anything else stays until a key. */
bool stopwatch_ticking(void);
void stopwatch_text(char *text, size_t size, uint64_t now_ms, const stopwatch_regs_t *regs);
/* A pointer still being typed counts as 0, as the manual has it. */
void stopwatch_pointers(int *store, int *recall, bool *delta);

#ifdef __cplusplus
}
#endif

#endif
