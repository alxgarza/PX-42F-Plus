#include "power.h"

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/powman.h"
#include "hardware/resets.h"
#include "hardware/structs/systick.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "display.h"
#include "keyboard.h"
#include "speaker.h"
#include "splash.h"
#include "rtc.h"
#include "settings.h"

#define IR_LED_PIN    14 /* see main.c */
#define SERIAL_RX_PIN 17 /* see main.c */

/* Until the power-down request is made, a step that hangs reboots instead
 * of leaving the calculator stuck awake, draining the battery (the caller
 * has paused the normal 100ms watchdog). */
#define OFF_SAFETY_WATCHDOG_MS 5000

/* The low-power regulator's voltage while off (VSEL: 0.55V + 50mV steps;
 * 0b00111 = 0.90V, the reset default is 1.10V). It feeds the always-on
 * logic and the RAM kept powered for the calculator's memory, which both
 * leak less lower down. Should the RAM not hold its contents at this
 * voltage, ramstate.c's checksum catches it (the calculator then starts
 * from Memory Clear). */
#define OFF_VREG_VSEL 0x07u

static volatile bool core0_park_request = false;
static volatile bool core0_parked = false;

/* Masks every interrupt in this core's NVIC and stops its SysTick, so
 * nothing can wake it from WFI: the power manager only powers the
 * processors down once both are asleep, and stay asleep. */
static void quiesce_this_core(void) {
    for (uint n = 0; n < (NUM_IRQS + 31) / 32; n++)
        irq_set_mask_n_enabled(n, 0xFFFFFFFFu, false);
    systick_hw->csr = 0;
}

void power_core0_poll(void) {
    if (!core0_park_request)
        return;
    /* Shut USB down first, as the SDK's pico_low_power does before a
     * power-down: the USB transceiver otherwise stays biased. This core
     * owns USB (and the SDK's timer pool, which stdio_usb_deinit() needs),
     * so it happens here. Then hold the controller in reset. */
    stdio_usb_deinit();
    reset_block(RESETS_RESET_USBCTRL_BITS);
    (void) save_and_disable_interrupts();
    quiesce_this_core();
    core0_parked = true;
    __sev();
    while (true)
        __wfi();
}

/* While the splash shows, the power manager's timer wakes the chip once a
 * minute: power_splash_wake() redraws the clock and powers down again. What
 * it needs is kept in the power manager's scratch registers, which are in
 * the always-on domain. */
#define SPLASH_MAGIC 0x53504C53u /* "SPLS" */
#define SCRATCH_MAGIC   0 /* SPLASH_MAGIC while a splash is up */
#define SCRATCH_ON_KEY  1 /* on_row | on_col << 8 */
#define SCRATCH_UNTIL_L 2 /* when the splash ends: always-on timer ms, low half */
#define SCRATCH_UNTIL_H 3 /* ... high half */

/* The wake from LAST_SWCORE_PWRUP: one bit per source -- bit 0 chip reset,
 * 1-4 the GPIO sources (ours, the ON key, is 1), 5 the debugger, 6 the
 * power manager's timer alarm. */
#define PWRUP_ALARM_BIT (1u << 6)

/* Outputs that switch real current -- the speaker MOSFET and the IR LED's
 * transistor -- explicitly low, with pull-downs as a backstop in case the
 * pins aren't held through the power-down; and the UART's RX input off. */
static void quiet_pins(void) {
    gpio_put(SPEAKER_PIN, 0);
    gpio_pull_down(SPEAKER_PIN);
    gpio_init(IR_LED_PIN);
    gpio_put(IR_LED_PIN, 0);
    gpio_set_dir(IR_LED_PIN, GPIO_OUT);
    gpio_pull_down(IR_LED_PIN);

    /* UART RX (GP17): its input buffer is on (the UART enabled it) and
     * nothing is connected -- RP2350 erratum E9 territory (an enabled
     * input with a pull-down can leak ~100uA). Off for the power-down. */
    gpio_disable_pulls(SERIAL_RX_PIN);
    gpio_set_input_enabled(SERIAL_RX_PIN, false);
}

/* The power-down itself, from the only core running (the other parked, or
 * never started): woken by the ON key (wake_pin low), and also by the
 * timer at alarm_ms (always-on timer time) unless it's 0. lposc_hz: the
 * low-power oscillator's measured rate (rtc_lposc_hz()), or 0 to keep the
 * one already set. Never returns; if the power manager refuses, the chip
 * reboots. */
static void __attribute__((noreturn)) power_down(uint wake_pin, uint64_t alarm_ms, uint32_t lposc_hz) {
    /* The always-on timer (the RTC) must keep counting with the crystal
     * off -- the low-power oscillator does, at the rate measured for it.
     * rtc_clock_init() switches back to the crystal at the next ordinary
     * boot. */
    powman_timer_set_1khz_tick_source_lposc_with_hz(lposc_hz);

    reset_block(RESETS_RESET_USBCTRL_BITS); /* the transceiver: see power_core0_poll() */
    (void) save_and_disable_interrupts();
    quiesce_this_core();

    powman_set_debug_power_request_ignored(true); /* power down even with a debugger attached */

    /* Unlock the core voltage regulator, so the power manager can actually
     * switch it to its low-power mode (VREG_LP_ENTRY) on the way down --
     * locked, it stays in its normal mode, feeding an all-but-empty core.
     * The SDK's own pico_low_power does the same (prepare_for_pstate_change()). */
    hw_set_bits(&powman_hw->vreg_ctrl, POWMAN_PASSWORD_BITS | POWMAN_VREG_CTRL_UNLOCK_BITS);
    hw_write_masked(&powman_hw->vreg_lp_entry, POWMAN_PASSWORD_BITS | (OFF_VREG_VSEL << POWMAN_VREG_LP_ENTRY_VSEL_LSB),
                    POWMAN_PASSWORD_BITS | POWMAN_VREG_LP_ENTRY_VSEL_BITS);

    /* Off: processors, XIP cache and SRAM bank 0 off; SRAM bank 1 (the top
     * 256KB) stays powered -- it holds the calculator's memory and the
     * display's framebuffer meanwhile (retained.h). On: everything back,
     * booting from the start. */
    powman_power_state off_state = POWMAN_POWER_STATE_NONE;
    off_state = powman_power_state_with_domain_on(off_state, POWMAN_POWER_DOMAIN_SRAM_BANK1);
    powman_power_state on_state = POWMAN_POWER_STATE_NONE;
    on_state = powman_power_state_with_domain_on(on_state, POWMAN_POWER_DOMAIN_SWITCHED_CORE);
    on_state = powman_power_state_with_domain_on(on_state, POWMAN_POWER_DOMAIN_XIP_CACHE);
    on_state = powman_power_state_with_domain_on(on_state, POWMAN_POWER_DOMAIN_SRAM_BANK0);
    on_state = powman_power_state_with_domain_on(on_state, POWMAN_POWER_DOMAIN_SRAM_BANK1);

    powman_disable_all_wakeups();
    powman_enable_gpio_wakeup(0, wake_pin, false, false); /* level, low: ON held down */
    if (alarm_ms != 0)
        powman_enable_alarm_wakeup_at_ms(alarm_ms);
    bool ok = powman_configure_wakeup_state(off_state, on_state);
    for (int i = 0; i < 4; i++)
        powman_hw->boot[i] = 0; /* wake = an ordinary boot, not a resume vector */
    if (ok) {
        int rc = powman_set_power_state(off_state);

        /* The SDK gives the power manager only a short spin to raise
         * WAITING ("waiting for the processors to sleep") and returns
         * PICO_ERROR_TIMEOUT if it hasn't -- but the power manager runs on
         * a slow clock. Give it longer before deciding. */
        if (rc == PICO_ERROR_TIMEOUT) {
            for (int i = 0; i < 500; i++) {
                if (powman_hw->state & (POWMAN_STATE_WAITING_BITS | POWMAN_STATE_CHANGING_BITS)) {
                    rc = PICO_OK;
                    break;
                }
                busy_wait_us(100);
            }
        }
        if (rc == PICO_OK) {
            /* Sleep: the power manager switches the processors off now. */
            watchdog_disable();
            while (true)
                __wfi();
        }
    }

    /* Refused: reboot rather than hang with both cores parked (the state
     * in RAM survives a watchdog reboot too). */
    watchdog_reboot(0, 0, 0);
    while (true)
        __wfi();
}

/* The next wake while the splash shows: the start of the next minute, or
 * the end of the splash if that's sooner. */
static uint64_t next_splash_alarm(uint64_t now_ms, uint64_t until_ms) {
    uint64_t next_minute = (now_ms / 60000 + 1) * 60000;
    return until_ms < next_minute ? until_ms : next_minute;
}

void power_deep_off(uint8_t on_row, uint8_t on_col, uint64_t splash_until_ms) {
    watchdog_enable(OFF_SAFETY_WATCHDOG_MS, true);
    if (splash_until_ms == 0)
        display_sleep();
    quiet_pins();

    /* Only the ON key's row is driven low: the pads hold their state
     * through the power-down (isolation latches), so pressing ON pulls its
     * column low -- the wake source. Wait for keys to be released first
     * (the key that turned the calculator off is often still down), or it
     * would wake straight away. */
    uint wake_pin = keyboard_arm_wake_key(on_row, on_col);
    while (!gpio_get(wake_pin)) {
        watchdog_update(); /* a key held this long is a person, not a fault */
        sleep_ms(10);
    }
    sleep_ms(50); /* contact bounce */

    /* Measured now, while the crystal still runs to measure it against. */
    uint32_t lposc_hz = rtc_lposc_hz();

    uint64_t alarm_ms = 0;
    if (splash_until_ms != 0) {
        powman_hw->scratch[SCRATCH_ON_KEY] = on_row | (uint32_t) on_col << 8;
        powman_hw->scratch[SCRATCH_UNTIL_L] = (uint32_t) splash_until_ms;
        powman_hw->scratch[SCRATCH_UNTIL_H] = (uint32_t) (splash_until_ms >> 32);
        powman_hw->scratch[SCRATCH_MAGIC] = SPLASH_MAGIC;
        alarm_ms = next_splash_alarm(powman_timer_get_ms(), splash_until_ms);
    } else {
        powman_hw->scratch[SCRATCH_MAGIC] = 0;
    }

    /* Park core 0. It idles in WFE (main()), which an event from this core
     * ends -- it can't be left to an interrupt: on battery there's no USB
     * traffic, and core 0 could sleep through the request indefinitely. */
    core0_park_request = true;
    while (!core0_parked) {
        __sev();
        busy_wait_us(100);
    }
    power_down(wake_pin, alarm_ms, lposc_hz);
}

bool power_splash_wake(void) {
    bool splash_wake = (powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS)
                       && (powman_hw->last_swcore_pwrup & PWRUP_ALARM_BIT)
                       && powman_hw->scratch[SCRATCH_MAGIC] == SPLASH_MAGIC;
    /* Whatever this boot is, the splash wakes are over unless set up again
     * below: no alarm left armed while the calculator is on, and should
     * anything here crash, the next boot is an ordinary one, not this. */
    powman_hw->scratch[SCRATCH_MAGIC] = 0;
    powman_disable_alarm_wakeup();
    if (!splash_wake)
        return false;

    uint8_t on_row = (uint8_t) powman_hw->scratch[SCRATCH_ON_KEY];
    uint8_t on_col = (uint8_t) (powman_hw->scratch[SCRATCH_ON_KEY] >> 8);
    uint64_t until_ms = powman_hw->scratch[SCRATCH_UNTIL_L] | (uint64_t) powman_hw->scratch[SCRATCH_UNTIL_H] << 32;

    keyboard_init();
    uint wake_pin = keyboard_arm_wake_key(on_row, on_col);
    if (!gpio_get(wake_pin))
        return false; /* ON is down already: turn on, an ordinary boot */

    watchdog_enable(OFF_SAFETY_WATCHDOG_MS, true);
    settings_load(); /* the date and time formats, for the clock bar */
    display_attach(); /* not reset: the panel is still showing the splash */
    quiet_pins();
    uint64_t now_ms = powman_timer_get_ms();
    uint64_t alarm_ms = 0;
    if (now_ms + 50 >= until_ms) {
        display_sleep(); /* Splash Time is up: blank, and wait for ON only */
    } else {
        splash_update_clock();
        alarm_ms = next_splash_alarm(now_ms, until_ms);
        powman_hw->scratch[SCRATCH_MAGIC] = SPLASH_MAGIC;
    }
    power_down(wake_pin, alarm_ms, 0); /* the rate measured at the power-off still holds */
}

bool power_woke_from_off(void) {
    return (powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS) != 0;
}
