#ifndef DEBUG_CHECKPOINT_H
#define DEBUG_CHECKPOINT_H

#include <stdint.h>

#include "hardware/watchdog.h"

/* "Where were we?" breadcrumb for the intermittent-reboot investigation.
 * A printf() right before a hang can't be trusted to actually reach the
 * host: it goes out over USB CDC, which needs the code to keep running
 * (servicing the USB task) to actually transmit -- if the reboot is
 * caused by something hanging, whatever was mid-flight in that buffer
 * may never make it out before the 100ms watchdog (main.c) fires and
 * resets the chip. The watchdog peripheral's own scratch registers,
 * though, are plain hardware state that isn't touched by a watchdog
 * reset (that's the entire reason they exist -- see
 * WATCHDOG_NON_REBOOT_MAGIC in the Pico SDK's watchdog.c, which uses
 * scratch[4] the same way). So: write the current checkpoint here right
 * before anything that might hang, and main() reports whatever was last
 * written the next time it boots -- surviving the reset instead of
 * racing it. scratch[4] is already used by the SDK itself
 * (watchdog_enable()/watchdog_caused_reboot()); 0 and 1 are used here,
 * and 2, 3, 5, 6, 7 hold the crash record (crash_report.c). None spare. */
enum {
    CKPT_NONE = 0,
    CKPT_MAIN_LOOP_TOP,
    CKPT_SERIAL_CHAR,
    CKPT_KEY_DOWN,
    CKPT_KEY_REPEAT_TICK,
    CKPT_KEY_TIMEOUT1,
    CKPT_KEY_TIMEOUT2,
    CKPT_KEY_UP,
    CKPT_DISPLAY_BLIT,
    CKPT_DISPLAY_FLUSH,
    CKPT_MENU,
    CKPT_FS_INIT,
    CKPT_FS_OPEN,
    CKPT_STATE_SAVE,
    CKPT_STATE_LOAD,
    CKPT_USB_DISK,
    CKPT_FLASH_WRITE,  /* detail = flash offset */
    CKPT_FLASH_DONE,
    CKPT_PROGRAM_SLICE,
    CKPT_IDLE_WAIT,    /* detail = ms until the deadline */
};

static inline void debug_checkpoint(uint32_t code, uint32_t detail) {
    watchdog_hw->scratch[0] = code;
    watchdog_hw->scratch[1] = detail;
}

/* Human-readable name for a CKPT_* code, for the boot-time report in
 * main.c. Kept next to the enum so the two can't drift apart. */
static inline const char *debug_checkpoint_name(uint32_t code) {
    switch (code) {
        case CKPT_NONE: return "NONE";
        case CKPT_MAIN_LOOP_TOP: return "MAIN_LOOP_TOP";
        case CKPT_SERIAL_CHAR: return "SERIAL_CHAR";
        case CKPT_KEY_DOWN: return "KEY_DOWN";
        case CKPT_KEY_REPEAT_TICK: return "KEY_REPEAT_TICK";
        case CKPT_KEY_TIMEOUT1: return "KEY_TIMEOUT1";
        case CKPT_KEY_TIMEOUT2: return "KEY_TIMEOUT2";
        case CKPT_KEY_UP: return "KEY_UP";
        case CKPT_DISPLAY_BLIT: return "DISPLAY_BLIT";
        case CKPT_DISPLAY_FLUSH: return "DISPLAY_FLUSH";
        case CKPT_MENU: return "MENU";
        case CKPT_FS_INIT: return "FS_INIT";
        case CKPT_FS_OPEN: return "FS_OPEN";
        case CKPT_STATE_SAVE: return "STATE_SAVE";
        case CKPT_STATE_LOAD: return "STATE_LOAD";
        case CKPT_USB_DISK: return "USB_DISK";
        case CKPT_FLASH_WRITE: return "FLASH_WRITE";
        case CKPT_FLASH_DONE: return "FLASH_DONE";
        case CKPT_PROGRAM_SLICE: return "PROGRAM_SLICE";
        case CKPT_IDLE_WAIT: return "IDLE_WAIT";
        default: return "?";
    }
}

#endif
