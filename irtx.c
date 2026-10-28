#include "irtx.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/watchdog.h"
#include "irtx.pio.h"

#define CYCLES_PER_PERIOD 32 /* irtx.pio */

static PIO const pio = pio0;
static int sm = -1;
static uint offset;
static absolute_time_t last_end; /* of the last transmission; 0 = long ago */

/* PIO0's clock is off unless something's being sent (cpuclock.c). */
static void pio_clock(bool on) {
    io_rw_32 *reg = &clocks_hw->wake_en[CLK_DEST_SYS_PIO0 / 32];
    uint32_t bit = 1u << (CLK_DEST_SYS_PIO0 % 32);
    if (on)
        hw_set_bits(reg, bit);
    else
        hw_clear_bits(reg, bit);
}

static void wait_until(absolute_time_t t) {
    while (!time_reached(t)) {
        watchdog_update(); /* a gap can be longer than the watchdog's 100ms */
        tight_loop_contents();
    }
}

/* Carrier periods in us microseconds, minus one (the FIFO's count). */
static uint32_t periods(uint32_t us, uint32_t carrier_hz) {
    uint32_t n = (uint32_t) (((uint64_t) us * carrier_hz + 500000) / 1000000);
    return n > 0 ? n - 1 : 0;
}

void irtx_send(const px_ir_burst_t *b) {
    wait_until(delayed_by_us(last_end, b->us[0]));

    pio_clock(true);
    if (sm < 0) {
        offset = pio_add_program(pio, &ir_carrier_program);
        sm = pio_claim_unused_sm(pio, true);
    }
    pio_sm_config c = ir_carrier_program_get_default_config(offset);
    sm_config_set_set_pins(&c, IR_LED_PIN, 1);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float) clock_get_hz(clk_sys) / ((float) b->carrier_hz * CYCLES_PER_PERIOD));
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_pins_with_mask(pio, sm, 0, 1u << IR_LED_PIN);
    pio_sm_set_consecutive_pindirs(pio, sm, IR_LED_PIN, 1, true);
    pio_gpio_init(pio, IR_LED_PIN);
    pio_sm_set_enabled(pio, sm, true);

    /* Pulses and gaps, then a one-period gap so it ends off; done when the
     * state machine is left waiting for more. */
    for (int i = 1; i < b->count; i++) {
        watchdog_update();
        pio_sm_put_blocking(pio, sm, periods(b->us[i], b->carrier_hz));
    }
    pio_sm_put_blocking(pio, sm, 0);
    uint32_t stalled = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    pio->fdebug = stalled;
    while (!(pio->fdebug & stalled)) {
        watchdog_update();
        tight_loop_contents();
    }
    last_end = get_absolute_time();

    pio_sm_set_enabled(pio, sm, false);
    gpio_put(IR_LED_PIN, 0); /* back to a plain output, low -- set before it's switched over */
    gpio_set_dir(IR_LED_PIN, GPIO_OUT);
    gpio_set_function(IR_LED_PIN, GPIO_FUNC_SIO);
    pio_clock(false);
}
