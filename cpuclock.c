#include "cpuclock.h"

#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/vreg.h"
#include "hardware/structs/scb.h"
#include "hardware/sync.h"
#include "pico/time.h"

/* The core voltage at 48MHz. The SDK's default, 1.10V, is what 150MHz
 * needs; 48MHz runs with a good margin below it, and dynamic power goes
 * with the square of the voltage. */
#define IDLE_VREG VREG_VOLTAGE_1_00

/* After raising the voltage, before speeding up: the regulator's output
 * has to get there (the SDK waits the same after raising it at startup). */
#define VREG_SETTLE_US SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST_DELAY_US

static bool is_fast = true; /* the SDK's startup code leaves it at 150MHz */

/* Blocks this firmware never uses: their clocks stay off. */
static const clock_dest_num_t never_used[] = {
    CLK_DEST_HSTX, CLK_DEST_SYS_HSTX,
    CLK_DEST_SYS_I2C0, CLK_DEST_SYS_I2C1,
    CLK_DEST_SYS_PIO0, /* on only while the IR LED sends (irtx.c) */
    CLK_DEST_SYS_PIO1, CLK_DEST_SYS_PIO2,
    CLK_DEST_PERI_SPI1, CLK_DEST_SYS_SPI1,
    CLK_DEST_PERI_UART1, CLK_DEST_SYS_UART1,
    CLK_DEST_SYS_DMA, CLK_DEST_SYS_SHA256, CLK_DEST_SYS_TIMER1,
};

/* While both cores are asleep, every clock stops except these -- what
 * wakes them, what has to keep running meanwhile, and the processors'
 * memory system. The other peripherals restart by themselves before
 * either core runs again.
 *
 * The memory system has to stay: with it stopped too, core 0 crashed
 * right after flash writes, its stack pointer off -- the registers it
 * saves on its stack on the way into the interrupt that parks it during
 * the write (multicore lockout), and restores on the way out, came back
 * wrong. */
static const clock_dest_num_t keep_in_sleep[] = {
    CLK_DEST_SYS_BUSFABRIC, CLK_DEST_SYS_SIO, CLK_DEST_SYS_XIP, CLK_DEST_SYS_ROM, CLK_DEST_SYS_BOOTRAM,
    CLK_DEST_SYS_SRAM0, CLK_DEST_SYS_SRAM1, CLK_DEST_SYS_SRAM2, CLK_DEST_SYS_SRAM3, CLK_DEST_SYS_SRAM4,
    CLK_DEST_SYS_SRAM5, CLK_DEST_SYS_SRAM6, CLK_DEST_SYS_SRAM7, CLK_DEST_SYS_SRAM8, CLK_DEST_SYS_SRAM9,
    CLK_DEST_SYS_TIMER0, CLK_DEST_REF_TICKS,   /* both cores' alarms (and the 1us tick) */
    CLK_DEST_SYS_IO,                           /* the keyboard's column interrupts */
    CLK_DEST_SYS_USBCTRL, CLK_DEST_USB,        /* USB keeps its connection */
    CLK_DEST_REF_POWMAN, CLK_DEST_SYS_POWMAN,  /* the always-on timer: the clock */
    CLK_DEST_SYS_WATCHDOG,                     /* the watchdog keeps counting */
};

void cpuclock_init(void) {
    clock_configure_undivided(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, USB_CLK_HZ);

    clock_stop(clk_hstx); /* its own generator, from the system PLL */
    for (uint i = 0; i < count_of(never_used); i++) {
        hw_clear_bits(&clocks_hw->wake_en[never_used[i] / 32], 1u << (never_used[i] % 32));
        hw_clear_bits(&clocks_hw->sleep_en[never_used[i] / 32], 1u << (never_used[i] % 32));
    }

    clock_dest_bitset_t keep = clock_dest_bitset_none();
    for (uint i = 0; i < count_of(keep_in_sleep); i++)
        clock_dest_bitset_add(&keep, keep_in_sleep[i]);
    clock_gate_sleep_en(&keep);
}

/* The sleep clocks are only gated once both cores are in deep sleep
 * (WFI/WFE with SLEEPDEEP set in each core's own SCR). Left on all the
 * time, that corrupted both cores' stacks around flash writes (when each
 * core waits for the other, in WFE, while core 0 is parked); the SDK's
 * own low-power code also only enables it around one controlled sleep.
 * So it's on only for core 1's idle wait -- and core 0 follows: it sleeps
 * deep only while core 1 asks it to, and says when it no longer does. */
static volatile bool deep_requested = false;
static volatile bool core0_deep = false;

void cpuclock_deep_sleep_begin(void) {
    scb_hw->scr |= M33_SCR_SLEEPDEEP_BITS;
    deep_requested = true;
    __sev(); /* core 0 comes round its loop and goes back to sleep deep */
}

void cpuclock_deep_sleep_end(void) {
    scb_hw->scr &= ~M33_SCR_SLEEPDEEP_BITS;
    deep_requested = false;
    while (core0_deep) {
        __sev();
        tight_loop_contents();
    }
}

void cpuclock_core0_before_sleep(void) {
    if (deep_requested)
        scb_hw->scr |= M33_SCR_SLEEPDEEP_BITS;
    else
        scb_hw->scr &= ~M33_SCR_SLEEPDEEP_BITS;
    core0_deep = deep_requested;
}

void cpuclock_set_fast(bool fast) {
    if (fast == is_fast)
        return;
    if (fast) {
        vreg_set_voltage(VREG_VOLTAGE_DEFAULT);
        busy_wait_us(VREG_SETTLE_US);
        /* The same PLL settings the SDK's startup code uses. pll_init()
         * waits for it to lock. */
        pll_init(pll_sys, PLL_SYS_REFDIV, PLL_SYS_VCO_FREQ_HZ, PLL_SYS_POSTDIV1, PLL_SYS_POSTDIV2);
        clock_configure_undivided(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                                  CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, SYS_CLK_HZ);
    } else {
        clock_configure_undivided(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                                  CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, USB_CLK_HZ);
        pll_deinit(pll_sys);
        vreg_set_voltage(IDLE_VREG); /* lowering needs no wait */
    }
    is_fast = fast;
}
