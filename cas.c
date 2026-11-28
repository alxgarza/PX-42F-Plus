#include "cas.h"

#include "hardware/regs/addressmap.h"
#include "hardware/structs/nvic.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#include "free42_app.h"
#include "settings.h"

/* The CAS image's vector table: first in it, as in any program built for
 * the RP2350 (the SDK's linker script puts .vectors at the start). */
#define CAS_VECTORS ((const uint32_t *) (XIP_BASE + FLASH_CAS_OFFSET))

bool cas_installed(void) {
    uint32_t sp = CAS_VECTORS[0], entry = CAS_VECTORS[1];
    return sp > SRAM_BASE && sp <= SRAM_END && (entry & 1) /* Thumb */
           && entry > XIP_BASE + FLASH_CAS_OFFSET && entry < XIP_BASE + FLASH_CAS_OFFSET + FLASH_CAS_SIZE;
}

const char *cas_start(void) {
    if (!cas_installed())
        return "The CAS isn't installed";
    if (!free42_save_for_restart())
        return "Can't save the memory";
    CAS_HANDOFF = CAS_HANDOFF_TO_CAS;
    CAS_SLEEP_MINUTES = px_settings.sleep_minutes;
    watchdog_reboot(0, 0, 0);
    while (true)
        tight_loop_contents();
}

void cas_boot_check(void) {
    if (CAS_HANDOFF != CAS_HANDOFF_TO_CAS)
        return;
    CAS_HANDOFF = 0; /* once: a CAS that crashes comes back here */
    if (!cas_installed())
        return;
    /* As the boot ROM leaves a program: no interrupt enabled or pending,
     * the SysTick off. Only this core runs yet. Then the CAS's own startup
     * sets up everything again, its vector table included. */
    systick_hw->csr = 0;
    for (unsigned i = 0; i < count_of(nvic_hw->icer); i++) {
        nvic_hw->icer[i] = 0xFFFFFFFFu;
        nvic_hw->icpr[i] = 0xFFFFFFFFu;
    }
    scb_hw->vtor = (uint32_t) CAS_VECTORS;
    __asm volatile("msr msp, %0\n\tbx %1\n" : : "r"(CAS_VECTORS[0]), "r"(CAS_VECTORS[1]) : "memory");
    __builtin_unreachable();
}

bool cas_returned(bool *turn_off) {
    *turn_off = CAS_HANDOFF == CAS_HANDOFF_OFF;
    if (CAS_HANDOFF != CAS_HANDOFF_FROM_CAS && !*turn_off)
        return false;
    CAS_HANDOFF = 0;
    return true;
}
