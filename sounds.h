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
 *   /SOUNDS/KEY/<name>.wav     the Key Sound choices -- held in RAM, at
 *                              most 250ms (longer ones are cut, with a fade)
 *   /SOUNDS/BOOT.wav           a cold boot                } played straight
 *   /SOUNDS/ON.wav, OFF.wav    turning on (ON), off       } from the drive,
 *   /SOUNDS/TONES/TONE0..9.wav TONE 0-9 (and BEEP)        } any length
 *   /SOUNDS/ERROR.wav          Free42's error squeak      }
 *
 * The built-in sounds are the same files, compiled in (sounds_data.c, made
 * by tools/make_sounds.py): any that's missing from the drive is written
 * there at boot (all of them on the first), and a built-in plays whenever
 * its file is missing or unreadable. */

#define SOUNDS_DIR     "/SOUNDS"
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

/* Play /SOUNDS/BOOT.wav, ON.wav, OFF.wav; blocking. The callers check the
 * Boot Sound / On Sound / Off Sound settings. */
void sounds_play_boot(void);
void sounds_play_on(void);
void sounds_play_off(void);

/* Free42's shell_beeper(): 0-9 = TONE n, 10 = the error squeak. Blocking. */
void sounds_play_tone(int tone);

/* The key sound, from RAM: starts at once. Nothing if Key Sound is Off. */
void sounds_key_play(void);

/* Loads key sound `name` ("" = Off) into RAM, from /SOUNDS/KEY/<name>.wav
 * or else the built-in of that name. False, with the reason in error, if
 * there's neither or the file can't be used. */
bool sounds_key_select(const char *name, char *error, size_t error_size);

/* The Key Sound choices (not Off): the built-ins, in their order, then any
 * other files in /SOUNDS/KEY, sorted. Returns how many. */
int sounds_key_list(char names[][SOUND_NAME_MAX + 1], int max);

#ifdef __cplusplus
}
#endif

#endif
