#ifndef ALARMS_H
#define ALARMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The HP-41 Time Module's alarms: the table and its rules, kept free of
 * hardware for tests/run.sh. Times are the clock's ms since 1970. What each kind
 * does when it comes due is in free42_app.cc (alarm_service()). */

#define ALARMS_MAX     32
#define ALARM_TEXT_MAX 44 /* ALPHA */
#define ALARM_ARROW    94 /* ↑ in the HP-42S's characters */
#define ALARM_SOUND_NAME_MAX 28 /* as sounds.h's SOUND_NAME_MAX */
#define ALARM_SOUND_PATH_MAX 48

typedef enum {
    ALARM_SOUND_PLUS42, /* the two tones */
    ALARM_SOUND_WAV,    /* /SOUNDS/<name>.wav */
    ALARM_SOUND_MIDI,   /* /MIDI/<name>.mid */
} alarm_sound_t;

typedef struct {
    uint64_t at_ms;
    uint64_t repeat_ms;  /* 0 = once */
    bool past_due;       /* came due and wasn't acknowledged, or set for the past */
    uint8_t length;
    char text[ALARM_TEXT_MAX]; /* ALPHA as it was: not a C string */
    uint8_t sound;       /* alarm_sound_t */
    char sound_name[ALARM_SOUND_NAME_MAX + 1];
} alarm_t;

typedef enum {
    ALARM_MESSAGE,     /* tones and the text, until acknowledged */
    ALARM_CONTROL,     /* ↑↑LABEL: runs it, always */
    ALARM_CONDITIONAL, /* ↑LABEL: runs it if off or showing the clock */
} alarm_kind_t;

alarm_kind_t alarm_kind(const alarm_t *a);

/* The label a control or conditional alarm runs: length 0 = the current line. */
const char *alarm_label(const alarm_t *a, int *length);

/* A message, or a label the shell can run by name: at most 7 plain characters
 * (core_keydown_command() and arg_struct's text take no more). */
bool alarm_label_ok(const alarm_t *a);

/* The table, earliest first, then in the order they were set. */
int alarms_count(void);
const alarm_t *alarms_get(int i);

/* False if the table is full. An alarm already past is past due at once. */
bool alarms_add(uint64_t at_ms, uint64_t repeat_ms, const char *text, int length, uint64_t now_ms);
void alarms_remove(int i);
void alarms_clear(void);

/* Which sound it plays when it comes due (Settings -> Alarms); a new alarm has
 * Plus42's. False for a name too long. */
bool alarms_set_sound(int i, alarm_sound_t sound, const char *name);

/* The file an alarm plays. False for Plus42's tones. */
bool alarm_sound_path(const alarm_t *a, char *path, size_t size);

/* The first with this text, -1 if none (empty text: the first with none). */
int alarms_find_text(const char *text, int length);

/* The first that has come due and isn't past due yet, or -1. */
int alarms_due(uint64_t now_ms);

/* The alarm is over: gone, or if it repeats, moved to its next time after
 * now, in multiples of the repeat from its own time. */
void alarms_done(int i, uint64_t now_ms);
void alarms_mark_past_due(int i);

bool alarms_any_past_due(void);

/* The earliest past due that runs a program (ALMNOW), or -1. */
int alarms_past_due_program(void);

/* When the next one comes due, 0 if none is waiting. */
uint64_t alarms_next_at(void);

/* For saving to the drive; alarms_load() checks all of it, a file dropped on
 * the drive being anybody's. False leaves the table empty. */
const void *alarms_image(size_t *size);
bool alarms_load(const void *data, size_t size);
bool alarms_dirty(void);
void alarms_mark_saved(void);

#ifdef __cplusplus
}
#endif

#endif
