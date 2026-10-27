#ifndef RAMSTATE_H
#define RAMSTATE_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The calculator's memory while it's off: Free42's state, serialized into a
 * buffer in RAM that stays powered through the deep power-off (the top
 * 128KB, in SRAM bank 1, see power.c) and that the startup code doesn't
 * clear (it's outside everything the linker places). Plus42 reads and writes it as an ordinary state file at
 * RAMSTATE_PATH -- statefs_fopen() hands out a memory FILE for any path
 * under /RAM/ -- so it's the same .p42 format as Save State, just never
 * touching flash. A magic number and checksum, written only once a save
 * has completed, tell a real saved state from leftover RAM contents. */

#define RAMSTATE_PATH "/RAM/off.p42"

/* True if path is under /RAM/ (statefs.c routes those here). */
bool ramstate_path(const char *path);

/* A FILE on the buffer. "w..." starts a new state (invalid until it's
 * closed without overflowing); "r..." reads the last complete one, or
 * returns NULL if there's none. Opening it for reading uses it up -- it's
 * no longer valid afterwards -- so a state that crashes the calculator
 * while loading isn't loaded again at the next boot. */
FILE *ramstate_fopen(const char *mode);

/* A complete, intact saved state is in the buffer. */
bool ramstate_valid(void);

/* After a state was read and loaded fine: valid again (reading had used it
 * up; the data is untouched until the next save). So a crash later on
 * brings the calculator back with its memory as of this load, not empty. */
void ramstate_keep(void);

#ifdef __cplusplus
}
#endif

#endif
