#ifndef SOUNDS_H
#define SOUNDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The calculator's sounds are WAV files in /SOUNDS on its drive -- any WAV
 * Audacity exports: PCM 8/16/24/32-bit or 32-bit float, mono or stereo
 * (mixed down), any sample rate. Every sound is normalized (its loudest
 * point plays at full volume) and converted on the fly (sounds.c).
 *
 *   /SOUNDS/KEY/<name>/        a Key Sound set: ENTRY.wav for typing,
 *                              OP.wav for operations, ENTER.wav for ENTER
 *                              (key_sound_t) -- held in RAM, at most 100ms
 *                              each (longer ones are cut, with a fade); a
 *                              missing OP sounds like ENTRY, ENTER like OP
 *   /SOUNDS/KEY/<name>.wav     a Key Sound for every key, at most 250ms
 *   /SOUNDS/BOOT.wav           a cold boot                } played straight
 *   /SOUNDS/ON.wav, OFF.wav    turning on (ON), off       } from the drive,
 *   /SOUNDS/TONES/TONE0..9.wav TONE 0-9 (and BEEP)        } any length
 *   /SOUNDS/ERROR.wav          Free42's error squeak      }
 *
 * The built-in sounds are the same files, compiled in (sounds_data.c, made
 * by tools/make_sounds.py): any that's missing from the drive is written
 * there at boot (all of them on the first), one that's still as written is
 * updated when a new firmware changes it (/SOUNDS/BUILTIN.TXT), and a
 * built-in plays whenever its file is missing or unreadable. */

#define SOUNDS_DIR     "/SOUNDS"

/* MIDI (midiplay.h): the songs are .mid files in /MIDI, played by apps
 * (app.c). Made with /SOUNDS. */
#define MIDI_DIR        "/MIDI"
#define SOUND_NAME_MAX 28

typedef struct {
    const char *path; /* under /SOUNDS, e.g. "KEY/Tick.wav" */
    const uint8_t *wav;
    uint32_t size;
} builtin_sound_t;

extern const builtin_sound_t builtin_sounds[];
extern const unsigned builtin_sound_count;

/* After statefs_init(), and again after USB Disk mode: writes any built-in
 * sound missing from /SOUNDS, and
 * loads the chosen key sound (px_settings.key_sound) into RAM.
 * use_files = false: built-ins only (the last boot died loading a sound --
 * see main.c). */
void sounds_init(bool use_files);

/* Sounds play on core 0 (speaker.h): starting one stops the one playing,
 * and those that don't wait return as soon as it starts. */

/* /SOUNDS/BOOT.wav, waiting: the boot's sounds guard (main.c) stays on
 * until it has played. ON.wav: not waiting. OFF.wav, waiting: the power
 * goes off after it. The callers check the On / Off Sound settings. */
void sounds_play_boot(void);
void sounds_play_on(void);
void sounds_play_off(void);

/* Free42's shell_beeper(): 0-9 = TONE n, 10 = the error squeak. Free42
 * waits (a program playing a tune relies on each TONE ending before the
 * next); an app may not. */
void sounds_play_tone(int tone, bool wait);

/* Any WAV on the drive, by its full path (apps, app.c). False if it can't
 * be opened or isn't a WAV this can play. */
bool sounds_play_file(const char *path, bool wait);

/* A MIDI song, by its full path: starts it and returns at once (stopping
 * any sound playing). 0, or why not: */
#define SOUNDS_MIDI_NOT_FOUND (-1) /* no such file, or it can't be read */
#define SOUNDS_MIDI_NOT_MIDI  (-2) /* not a Standard MIDI File this can play */
#define SOUNDS_MIDI_TOO_BIG   (-3) /* over 128 KB */
#define SOUNDS_MIDI_NO_MEMORY (-4)
int sounds_midi_play(const char *path);

/* While a song plays: its length, how far it has got (ms), the channels
 * sounding (bit n = channel n+1), the notes sounding. False if none does. */
bool sounds_midi_status(uint32_t *length_ms, uint32_t *elapsed_ms, uint32_t *channels, uint32_t *voices);

/* A sound is playing. */
bool sounds_playing(void);
/* Until the sound playing has ended. */
void sounds_wait(void);
/* Stops the sound playing, and closes its file: before the drive goes to
 * a computer or is formatted, or anything else that needs the files. */
void sounds_stop(void);

/* Which of a Key Sound set's sounds a key plays (free42_key_sound()):
 * typing a number or text, an operation, ENTER. */
typedef enum { KEY_SOUND_ENTRY, KEY_SOUND_OP, KEY_SOUND_ENTER, KEY_SOUND_KINDS } key_sound_t;

/* A key sound, from RAM: starts at once and doesn't wait -- typing goes
 * on while it plays; the next key's cuts it short. Nothing if Key Sound is
 * Off. */
void sounds_key_play(key_sound_t kind);

/* Loads Key Sound `name` ("" = Off) into RAM: the set /SOUNDS/KEY/<name>/,
 * or else the single /SOUNDS/KEY/<name>.wav -- either from the drive or
 * the built-in of that name. False, with the reason in error, if there's
 * neither or the file can't be used. */
bool sounds_key_select(const char *name, char *error, size_t error_size);

/* The Key Sound choices (not Off): the built-ins, in their order, then any
 * other files in /SOUNDS/KEY, then its folders with an ENTRY.wav, sorted.
 * Returns how many. */
int sounds_key_list(char names[][SOUND_NAME_MAX + 1], int max);

#ifdef __cplusplus
}
#endif

#endif
