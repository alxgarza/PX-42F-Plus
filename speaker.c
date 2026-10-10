#include "speaker.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

/* The sample clock: another PWM slice, used only as a timer -- its wrap
 * interrupt fires at SPEAKER_SAMPLE_HZ. Slice 0's pins (GP0/GP1) are
 * keyboard rows under SIO control, so its counter is free. */
#define TIMER_SLICE 0

#define CARRIER_WRAP 1023 /* 10-bit duty steps: 0..1024, 1024 = always on */

/* Duty values waiting to go out: ~64ms at 32kHz -- enough to cover a
 * file read while the interrupt keeps playing. */
#define RING_SIZE 2048u
#define RING_MASK (RING_SIZE - 1)

static uint16_t ring[RING_SIZE];
static volatile uint32_t ring_head, ring_tail; /* written by the filler / the interrupt */
static uint speaker_slice, speaker_chan;
static uint16_t last_level;

static uint64_t last_tone_us;

uint64_t speaker_last_tone_us(void) {
    return last_tone_us;
}

void speaker_init(void) {
    /* Output latch low *before* enabling the output, so the gate never
     * sees a high glitch. */
    gpio_init(SPEAKER_PIN);
    gpio_put(SPEAKER_PIN, 0);
    gpio_set_dir(SPEAKER_PIN, GPIO_OUT);

    /* Backup only: if the pin ever stops being driven (e.g. during reset,
     * before this runs, the pad's reset-default pull-down does the same
     * job), the pull-down still holds the gate low. */
    gpio_pull_down(SPEAKER_PIN);

    /* Strongest drive, for fast gate edges (the gate is a capacitive load
     * that has to be charged/discharged every PWM cycle). */
    gpio_set_drive_strength(SPEAKER_PIN, GPIO_DRIVE_STRENGTH_12MA);

    speaker_slice = pwm_gpio_to_slice_num(SPEAKER_PIN);
    speaker_chan = pwm_gpio_to_channel(SPEAKER_PIN);
}

/* At SPEAKER_SAMPLE_HZ: the next duty value out (held, should the filler
 * fall behind). In RAM: it runs 32000 times a second, and mustn't wait on
 * the flash cache. The level is latched by the PWM at its next period, so
 * a pulse is never cut short. */
static void __not_in_flash_func(sample_irq)(void) {
    pwm_hw->intr = 1u << TIMER_SLICE;
    uint32_t tail = ring_tail;
    if (tail != ring_head) {
        last_level = ring[tail & RING_MASK];
        ring_tail = tail + 1;
    }
    pwm_set_chan_level(speaker_slice, speaker_chan, last_level);
}

/* While both cores sleep, the chip stops the clocks not on cpuclock.c's
 * keep list -- the PWM's among them. Waiting for the sample interrupt in
 * WFI (below) puts this core to sleep, and on battery the other one sleeps
 * too: the PWM -- the carrier and the sample clock -- would stop, and with
 * it the interrupt that was to end the wait (a hang, the watchdog's
 * reset). So the PWM keeps its clock for the length of a sound. */
static void keep_pwm_clock(bool keep) {
    io_rw_32 *reg = &clocks_hw->sleep_en[CLK_DEST_SYS_PWM / 32];
    uint32_t bit = 1u << (CLK_DEST_SYS_PWM % 32);
    if (keep)
        hw_set_bits(reg, bit);
    else
        hw_clear_bits(reg, bit);
}

void speaker_play(speaker_fill_fn fill, void *ctx) {
    ring_head = ring_tail = 0;
    last_level = 0;
    keep_pwm_clock(true);

    /* The carrier: the speaker's own slice, starting silent. */
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, 1);
    pwm_config_set_wrap(&cfg, CARRIER_WRAP);
    pwm_init(speaker_slice, &cfg, false);
    pwm_set_chan_level(speaker_slice, speaker_chan, 0);
    gpio_set_function(SPEAKER_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(speaker_slice, true);

    /* Prime the ring before the clock starts. */
    bool more = true;
    while (more && ring_head < RING_SIZE / 2) {
        uint32_t n = fill(ctx, &ring[ring_head], RING_SIZE / 2 - ring_head);
        ring_head += n;
        more = n != 0;
    }

    /* The sample clock. */
    uint32_t wrap = (clock_get_hz(clk_sys) + SPEAKER_SAMPLE_HZ / 2) / SPEAKER_SAMPLE_HZ - 1;
    cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, 1);
    pwm_config_set_wrap(&cfg, (uint16_t) wrap);
    pwm_init(TIMER_SLICE, &cfg, false);
    pwm_clear_irq(TIMER_SLICE);
    pwm_set_irq_enabled(TIMER_SLICE, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP_0, sample_irq);
    irq_set_enabled(PWM_IRQ_WRAP_0, true);
    pwm_set_enabled(TIMER_SLICE, true);

    /* Keep the ring topped up -- in contiguous pieces, up to where it
     * wraps -- and wait (WFI: the sample interrupt ends it) while full. */
    while (more) {
        watchdog_update();
        uint32_t space = RING_SIZE - (ring_head - ring_tail);
        if (space < 64) {
            __wfi();
            continue;
        }
        uint32_t at = ring_head & RING_MASK;
        uint32_t max = RING_SIZE - at < space ? RING_SIZE - at : space;
        uint32_t n = fill(ctx, &ring[at], max);
        ring_head += n;
        more = n != 0;
    }
    while (ring_tail != ring_head) {
        watchdog_update();
        __wfi();
    }

    pwm_set_enabled(TIMER_SLICE, false);
    irq_set_enabled(PWM_IRQ_WRAP_0, false);
    pwm_set_irq_enabled(TIMER_SLICE, false);
    irq_remove_handler(PWM_IRQ_WRAP_0, sample_irq);

    /* Transistor off: hand the pin back to plain GPIO, driving low. The
     * PWM output could be stopped mid-pulse (high), so don't just disable
     * the slice -- take the pin away from it. */
    gpio_put(SPEAKER_PIN, 0);
    gpio_set_function(SPEAKER_PIN, GPIO_FUNC_SIO);
    pwm_set_enabled(speaker_slice, false);
    keep_pwm_clock(false);
    last_tone_us = time_us_64();
}
