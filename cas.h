#ifndef CAS_H
#define CAS_H

#include <stdbool.h>
#include <stdint.h>

#include "flash_layout.h"
#include "retained.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The CAS: Giac (Bernard Parisse's computer algebra system), a program of
 * its own in flash (FLASH_CAS_OFFSET), built in its own repo, PX-42F-CAS,
 * next to this one, from this one's board code. Never linked with this
 * one: Giac is GPL3, Plus42 GPL2 only. The two take turns, each starting
 * the other by a restart: this one saves the calculator's memory
 * to the drive (as for an app), leaves CAS_HANDOFF_TO_CAS in retained RAM
 * and restarts; main() finds it and starts the CAS before anything else.
 * The CAS then has all the RAM (Plus42 never started), and leaving it, it
 * leaves CAS_HANDOFF_FROM_CAS and restarts: back here as after ON from OFF,
 * the memory from the drive. The word: between the framebuffer and the
 * next alarm (retained.h), which no startup code clears. */
#define CAS_HANDOFF          (*(volatile uint32_t *) RETAINED_CAS_HANDOFF)
#define CAS_HANDOFF_TO_CAS   0x31534143u /* "CAS1" */
#define CAS_HANDOFF_FROM_CAS 0x32345850u /* "PX42" */
/* The CAS left because its Sleep Timer ran out (or the battery is empty):
 * turn off, and ON goes back into the CAS (CAS_RESUME). The CAS can't power
 * down itself: the splash, the alarms, the clock chip are all this one's. */
#define CAS_HANDOFF_OFF      0x46464F43u /* "COFF" */
/* The Sleep Timer's minutes (px_settings.sleep_minutes), for the CAS. */
#define CAS_SLEEP_MINUTES    (*(volatile uint32_t *) RETAINED_CAS_SLEEP)
/* Set at the OFF the CAS asked for; ON then starts the CAS again. */
#define CAS_RESUME           (*(volatile uint32_t *) RETAINED_CAS_RESUME)
#define CAS_RESUME_ON        0x4D555352u /* "RSUM" */

/* Whether the CAS is in its place: its vector table's stack and entry
 * point where a program built for it has them (erased flash hasn't). */
bool cas_installed(void);

/* Saves what OFF saves and starts the CAS (doesn't return); an error
 * message if it can't: not installed, or the memory can't be saved. */
const char *cas_start(void);

/* First thing in main(): starts the CAS if that's what the restart was for. */
void cas_boot_check(void);

/* Whether this start is the CAS leaving (once: the word is cleared), and
 * whether it asked to be turned off (CAS_HANDOFF_OFF). */
bool cas_returned(bool *turn_off);

#ifdef __cplusplus
}
#endif

#endif
