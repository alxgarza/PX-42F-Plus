#include "crash_report.h"

#include <stdio.h>

#include "hardware/structs/scb.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "pico/platform.h"
#include <stddef.h>

#define MARK_HARDFAULT   0xC4A50000u /* | core1 stack peak in 256-byte units << 8 | core number */
#define MARK_FLASH_ERROR 0xC4A60000u /* | (rc & 0xFF) */
#define MARK_OFF_REFUSED 0xC4A70000u /* | CURRENT_PWRUP_REQ (7 bits) */
#define MARK_MASK        0xFFFF0000u

#define STACK_FILL 0xC0DEC0DEu

static uint32_t *core1_stack_base;
static uint32_t core1_stack_words;

void crash_watch_core1_stack(uint32_t *base, uint32_t words) {
    for (uint32_t i = 0; i < words; i++)
        base[i] = STACK_FILL;
    core1_stack_base = base;
    core1_stack_words = words;
}

uint32_t crash_core1_stack_peak(void) {
    if (core1_stack_base == NULL)
        return 0;
    uint32_t untouched = 0; /* the stack grows down: untouched words are at the bottom */
    while (untouched < core1_stack_words && core1_stack_base[untouched] == STACK_FILL)
        untouched++;
    return (core1_stack_words - untouched) * 4;
}

/* Called from isr_hardfault with the exception stack frame:
 * r0 r1 r2 r3 r12 lr pc xpsr. */
void __attribute__((used, noreturn)) crash_hardfault_record(uint32_t *frame) {
    uint32_t peak_units = crash_core1_stack_peak() / 256;
    watchdog_hw->scratch[2] = MARK_HARDFAULT | ((peak_units & 0xFF) << 8) | get_core_num();
    watchdog_hw->scratch[3] = frame[6]; /* stacked PC */
    watchdog_hw->scratch[5] = frame[5]; /* stacked LR */
    watchdog_hw->scratch[6] = scb_hw->cfsr;
    /* The data address involved, when the hardware knows it (bus or
     * memory-management fault); otherwise r1 -- often the base register
     * of the access that faulted. */
    uint32_t cfsr = scb_hw->cfsr;
    watchdog_hw->scratch[7] = (cfsr & (1u << 15)) ? scb_hw->bfar : (cfsr & (1u << 7)) ? scb_hw->mmfar : frame[1];
    watchdog_reboot(0, 0, 0);
    while (true)
        __wfi();
}

/* Overrides the SDK's default (a breakpoint loop that only the watchdog
 * ends, losing where it happened). Picks the stack the fault was taken
 * on (bit 2 of EXC_RETURN: 0 = MSP, 1 = PSP). */
void __attribute__((naked)) isr_hardfault(void) {
    __asm volatile(
        "movs r0, #4            \n"
        "mov  r1, lr            \n"
        "tst  r0, r1            \n"
        "beq  1f                \n"
        "mrs  r0, psp           \n"
        "b    crash_hardfault_record \n"
        "1:                     \n"
        "mrs  r0, msp           \n"
        "b    crash_hardfault_record \n");
}

void crash_note_flash_error(int rc, uint32_t flash_offset) {
    watchdog_hw->scratch[2] = MARK_FLASH_ERROR | ((uint32_t) rc & 0xFFu);
    watchdog_hw->scratch[3] = flash_offset;
    printf("flash write at 0x%06lx failed: rc=%d\n", (unsigned long) flash_offset, rc);
}

void crash_note_off_refused(uint32_t pending) {
    watchdog_hw->scratch[2] = MARK_OFF_REFUSED | (pending & 0x7Fu);
}

bool crash_report_take(char *line1, char *line2, char *line3, int size, const char *last_checkpoint_name,
                       bool by_watchdog) {
    uint32_t mark = watchdog_hw->scratch[2];
    uint32_t a = watchdog_hw->scratch[3], b = watchdog_hw->scratch[5], c = watchdog_hw->scratch[6];
    uint32_t d = watchdog_hw->scratch[7];
    watchdog_hw->scratch[2] = 0;
    line3[0] = 0;

    if ((mark & MARK_MASK) == MARK_HARDFAULT) {
        unsigned long core = mark & 0xF, peak = ((mark >> 8) & 0xFF) * 256;
        snprintf(line1, (size_t) size, "FAULT core%lu PC=%08lx", core, (unsigned long) a);
        snprintf(line2, (size_t) size, "LR=%08lx A=%08lx", (unsigned long) b, (unsigned long) d);
        snprintf(line3, (size_t) size, "C=%08lx S=%lu %s", (unsigned long) c, peak, last_checkpoint_name);
        printf("Last reset: HardFault on core %lu, PC=0x%08lx LR=0x%08lx CFSR=0x%08lx addr/r1=0x%08lx, "
               "core1 stack peak %lu bytes, last checkpoint %s\n",
               core, (unsigned long) a, (unsigned long) b, (unsigned long) c, (unsigned long) d, peak,
               last_checkpoint_name);
        return true;
    }
    if ((mark & MARK_MASK) == MARK_FLASH_ERROR) {
        snprintf(line1, (size_t) size, "FLASH ERR rc=%d @%06lx", (int) (int8_t) (mark & 0xFF), (unsigned long) a);
        snprintf(line2, (size_t) size, "%s", last_checkpoint_name);
        printf("Earlier: flash write failed, rc=%d at 0x%06lx, last checkpoint %s\n",
               (int) (int8_t) (mark & 0xFF), (unsigned long) a, last_checkpoint_name);
        return true;
    }
    if ((mark & MARK_MASK) == MARK_OFF_REFUSED) {
        unsigned long pending = mark & 0x7F;
        snprintf(line1, (size_t) size, "OFF REFUSED");
        snprintf(line2, (size_t) size, "(power-down failed)");
        snprintf(line3, (size_t) size, "pending wake 0x%02lx", pending);
        printf("Last reset: the power manager refused to power down (pending wake-ups 0x%02lx); rebooted instead\n",
               pending);
        return true;
    }
    if (by_watchdog) {
        snprintf(line1, (size_t) size, "WATCHDOG RESET");
        snprintf(line2, (size_t) size, "%s", last_checkpoint_name);
        return true;
    }
    return false;
}
