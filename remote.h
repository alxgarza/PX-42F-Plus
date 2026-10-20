#ifndef REMOTE_H
#define REMOTE_H

#include <stdbool.h>

#include "irfile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The calculator as a remote control: a map of the keyboard with each
 * key's label (irfile.h), and a key sends its code from the infrared LED
 * (irtx.h) -- again and again while held. ON (EXIT) goes back to the
 * calculator; held 2 seconds it turns the calculator off, and the Sleep
 * Timer does too. Remotes are /IR/<name>.ir files, chosen in the settings
 * menu. */

#define IR_DIR "/IR"

/* Runs until ON (false) or turning off (true). */
bool remote_run(const ir_remote_t *remote, const char *name);

#ifdef __cplusplus
}
#endif

#endif
