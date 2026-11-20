#include <stdio.h>
#include <string.h>

#include "alarms.h"
#include "sounds.h"

/* A flash clears the alarms with the rest of the memory, so no firmware ever
 * meets an older file: a new layout only needs a new magic. */
#define FILE_MAGIC 0x334D4C41u /* "ALM3" */

static struct {
    uint32_t magic;
    uint32_t count;
    alarm_t alarm[ALARMS_MAX];
} table = {FILE_MAGIC, 0, {{0}}};

static bool dirty;

alarm_kind_t alarm_kind(const alarm_t *a) {
    if (a->length >= 2 && a->text[0] == ALARM_ARROW && a->text[1] == ALARM_ARROW)
        return ALARM_CONTROL;
    return a->length >= 1 && a->text[0] == ALARM_ARROW ? ALARM_CONDITIONAL : ALARM_MESSAGE;
}

const char *alarm_label(const alarm_t *a, int *length) {
    int skip = alarm_kind(a) == ALARM_CONTROL ? 2 : alarm_kind(a) == ALARM_CONDITIONAL ? 1 : 0;
    *length = a->length - skip;
    return a->text + skip;
}

bool alarm_label_ok(const alarm_t *a) {
    if (alarm_kind(a) == ALARM_MESSAGE)
        return true;
    int length;
    const char *label = alarm_label(a, &length);
    if (length > 7)
        return false;
    for (int i = 0; i < length; i++)
        if (label[i] < ' ' || label[i] > '~')
            return false;
    return true;
}

int alarms_count(void) {
    return (int) table.count;
}

const alarm_t *alarms_get(int i) {
    return &table.alarm[i];
}

static void insert(const alarm_t *a) {
    int at = (int) table.count;
    while (at > 0 && table.alarm[at - 1].at_ms > a->at_ms)
        at--;
    memmove(&table.alarm[at + 1], &table.alarm[at], (table.count - (uint32_t) at) * sizeof(alarm_t));
    table.alarm[at] = *a;
    table.count++;
    dirty = true;
}

bool alarms_add(uint64_t at_ms, uint64_t repeat_ms, const char *text, int length, uint64_t now_ms) {
    if (table.count >= ALARMS_MAX || length < 0 || length > ALARM_TEXT_MAX)
        return false;
    alarm_t a;
    memset(&a, 0, sizeof(a));
    a.at_ms = at_ms;
    a.repeat_ms = repeat_ms;
    a.past_due = at_ms <= now_ms;
    a.length = (uint8_t) length;
    memcpy(a.text, text, (size_t) length);
    insert(&a);
    return true;
}

bool alarms_add_owned(uint64_t at_ms, const char *text, int length, const char *owner, uint64_t now_ms) {
    size_t n = strlen(owner);
    if (n == 0 || n > ALARM_OWNER_MAX || table.count >= ALARMS_MAX || length < 0 || length > ALARM_TEXT_MAX)
        return false;
    alarm_t a;
    memset(&a, 0, sizeof(a));
    a.at_ms = at_ms;
    a.past_due = at_ms <= now_ms;
    a.length = (uint8_t) length;
    memcpy(a.text, text, (size_t) length);
    memcpy(a.owner, owner, n);
    if (alarm_kind(&a) != ALARM_MESSAGE)
        return false;
    insert(&a);
    return true;
}

int alarms_remove_owned(const char *owner) {
    int gone = 0;
    for (int i = (int) table.count - 1; i >= 0; i--)
        if (owner[0] && strcmp(table.alarm[i].owner, owner) == 0) {
            alarms_remove(i);
            gone++;
        }
    return gone;
}

void alarms_remove(int i) {
    table.count--;
    memmove(&table.alarm[i], &table.alarm[i + 1], (table.count - (uint32_t) i) * sizeof(alarm_t));
    dirty = true;
}

void alarms_clear(void) {
    if (table.count != 0)
        dirty = true;
    table.count = 0;
}

bool alarms_set_sound(int i, alarm_sound_t sound, const char *name) {
    if (strlen(name) > ALARM_SOUND_NAME_MAX)
        return false;
    table.alarm[i].sound = (uint8_t) sound;
    strcpy(table.alarm[i].sound_name, name);
    dirty = true;
    return true;
}

bool alarm_sound_path(const alarm_t *a, char *path, size_t size) {
    if (a->sound == ALARM_SOUND_PLUS42)
        return false;
    int n = a->sound == ALARM_SOUND_WAV ? snprintf(path, size, SOUNDS_DIR "/%s.wav", a->sound_name)
                                        : snprintf(path, size, MIDI_DIR "/%s.mid", a->sound_name);
    return n > 0 && (size_t) n < size;
}

int alarms_find_text(const char *text, int length) {
    for (int i = 0; i < (int) table.count; i++)
        if (table.alarm[i].length == length && memcmp(table.alarm[i].text, text, (size_t) length) == 0)
            return i;
    return -1;
}

int alarms_due(uint64_t now_ms) {
    for (int i = 0; i < (int) table.count && table.alarm[i].at_ms <= now_ms; i++)
        if (!table.alarm[i].past_due)
            return i;
    return -1;
}

void alarms_done(int i, uint64_t now_ms) {
    alarm_t a = table.alarm[i];
    alarms_remove(i);
    if (a.repeat_ms == 0)
        return;
    if (a.at_ms <= now_ms)
        a.at_ms += ((now_ms - a.at_ms) / a.repeat_ms + 1) * a.repeat_ms;
    a.past_due = false;
    insert(&a);
}

void alarms_mark_past_due(int i) {
    table.alarm[i].past_due = true;
    dirty = true;
}

bool alarms_any_past_due(void) {
    for (int i = 0; i < (int) table.count; i++)
        if (table.alarm[i].past_due)
            return true;
    return false;
}

int alarms_past_due_program(void) {
    for (int i = 0; i < (int) table.count; i++)
        if (table.alarm[i].past_due && alarm_kind(&table.alarm[i]) != ALARM_MESSAGE)
            return i;
    return -1;
}

uint64_t alarms_next_at(void) {
    for (int i = 0; i < (int) table.count; i++)
        if (!table.alarm[i].past_due)
            return table.alarm[i].at_ms;
    return 0;
}

const void *alarms_image(size_t *size) {
    *size = offsetof(__typeof__(table), alarm) + table.count * sizeof(alarm_t);
    return &table;
}

bool alarms_load(const void *data, size_t size) {
    static const size_t header = offsetof(__typeof__(table), alarm);
    uint32_t head[2];
    table.count = 0;
    dirty = false;
    if (size < header)
        return false;
    memcpy(head, data, sizeof(head));
    if (head[0] != FILE_MAGIC || head[1] > ALARMS_MAX || size != header + head[1] * sizeof(alarm_t))
        return false;
    for (uint32_t i = 0; i < head[1]; i++) {
        alarm_t a;
        memcpy(&a, (const char *) data + header + i * sizeof(alarm_t), sizeof(a));
        if (a.length > ALARM_TEXT_MAX || !alarm_label_ok(&a) || a.sound > ALARM_SOUND_MIDI
            || memchr(a.sound_name, 0, sizeof(a.sound_name)) == NULL || memchr(a.owner, 0, sizeof(a.owner)) == NULL
            || (a.owner[0] && alarm_kind(&a) != ALARM_MESSAGE)) {
            table.count = 0;
            return false;
        }
        a.past_due = *(const unsigned char *) &a.past_due != 0;
        insert(&a); /* in order, whatever order the file has */
    }
    dirty = false;
    return true;
}

bool alarms_dirty(void) {
    return dirty;
}

void alarms_mark_saved(void) {
    dirty = false;
}
