#ifndef CRASH_REPORT_H
#define CRASH_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Post-mortem for unexpected resets. A HardFault on either core records
 * the faulting core, PC, LR and fault status in watchdog scratch registers
 * (which survive the reset -- see debug_checkpoint.h) and reboots at once;
 * a flash write that couldn't be done safely is recorded the same way.
 * The next boot shows the record on the splash screen and over serial,
 * then clears it. Uses scratch[2], [3], [5], [6], [7]: [0]/[1] are the
 * checkpoint, [4] belongs to the SDK (and [5]-[7] only when rebooting to
 * a specific address, which this firmware never does). */

/* Records a failed flash_safe_execute() (rc) for a flash offset. Doesn't
 * reset. */
void crash_note_flash_error(int rc, uint32_t flash_offset);

/* If the last reset left a record, formats it into two short lines (for
 * the splash screen), prints it in full over serial, clears it, and
 * returns true. last_checkpoint_name is the debug_checkpoint name of the
 * last checkpoint before the reset. */
bool crash_report_take(char *line1, char *line2, char *line3, int size, const char *last_checkpoint_name,
                       bool by_watchdog);

/* Fills core 1's stack with a marker pattern (call before launching core
 * 1), so crash_core1_stack_peak() can tell how deep it has ever gone --
 * recorded with every HardFault, and shown on the Info screen. */
void crash_watch_core1_stack(uint32_t *base, uint32_t words);
uint32_t crash_core1_stack_peak(void);

#ifdef __cplusplus
}
#endif

#endif
