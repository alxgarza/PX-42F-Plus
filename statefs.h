#ifndef STATEFS_H
#define STATEFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The state-file volume: a FAT filesystem on the flash disk (flashdisk.h),
 * holding Free42 state files as /STATE/<name>.f42 -- the same format the
 * desktop Free42 versions read and write. Like the DM42: states are saved
 * and loaded by name from the settings menu, and copied to and from a
 * computer in USB Disk mode. */

#define STATEFS_NAME_MAX 16 /* characters in a state name, without ".f42" */

/* Mounts the volume, formatting it on first use. Returns false if the
 * disk is unusable (see flashdisk_init()); every other call then fails. */
bool statefs_init(void);
bool statefs_ready(void);

/* Writes anything still cached to flash. Call after saving or loading, so
 * a power loss can't lose it. */
void statefs_sync(void);

/* "/STATE/<name>.f42" */
void statefs_state_path(const char *name, char *path, size_t size);
bool statefs_state_exists(const char *name);
/* Any file or folder, by full path. */
bool statefs_file_exists(const char *path);
/* Makes a folder (fine if it's there already). */
void statefs_mkdir(const char *path);

/* By state name (no extension). Both write to flash before returning.
 * Rename fails if new_name is already taken. */
bool statefs_delete_state(const char *name);
bool statefs_rename_state(const char *old_name, const char *new_name);

/* Up to max state names (no extension), sorted; returns how many. */
int statefs_list_states(char names[][STATEFS_NAME_MAX + 1], int max);

/* Splash images: /SPLASH/<name>.png, chosen in the settings menu. */
#define STATEFS_SPLASH_DIR "/SPLASH"

/* Up to max file names in dir ending in ext (case-insensitive), without
 * the extension, sorted, into names[i * name_size]. Names longer than
 * name_size - 1 are skipped. Returns how many. */
int statefs_list_files(const char *dir, const char *ext, char *names, size_t name_size, int max);

/* Backing for Free42's my_fopen()/my_rename()/my_remove() (see shell.cc):
 * stdio FILEs on the FAT volume, via newlib's funopen(). Modes "rb" and
 * "wb" only -- all the core uses. */
FILE *statefs_fopen(const char *name, const char *mode);
int statefs_rename(const char *oldname, const char *newname);
int statefs_remove(const char *name);

/* USB Disk mode. begin unmounts the volume and hands it to the computer;
 * poll (call often, from core 1) writes back what the computer sends;
 * end takes the volume back and remounts it. */
void statefs_usb_begin(void);
void statefs_usb_poll(void);
void statefs_usb_end(void);

#ifdef __cplusplus
}
#endif

#endif
