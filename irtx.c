#include "irtx.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/watchdog.h"
#include "irtx.pio.h"
#include "hp82240.h"

#define CYCLES_PER_PERIOD 32 /* irtx.pio */

static PIO const pio = pio0;
static int sm = -1;
static int carrier_offset = -1, redeye_offset = -1; /* each program loaded the first time it's used */
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

/* Turns PIO0 on and loads program, the first time; returns where it is. */
static uint load(const pio_program_t *program, int *offset) {
    pio_clock(true);
    if (sm < 0)
        sm = pio_claim_unused_sm(pio, true);
    if (*offset < 0)
        *offset = (int) pio_add_program(pio, program);
    return (uint) *offset;
}

/* Starts the state machine on the LED, clocked at pio_hz; the caller
 * fills the FIFO. */
static void sm_begin(pio_sm_config *c, uint offset, float pio_hz) {
    sm_config_set_set_pins(c, IR_LED_PIN, 1);
    sm_config_set_fifo_join(c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(c, (float) clock_get_hz(clk_sys) / pio_hz);
    pio_sm_init(pio, sm, offset, c);
    pio_sm_set_pins_with_mask(pio, sm, 0, 1u << IR_LED_PIN);
    pio_sm_set_consecutive_pindirs(pio, sm, IR_LED_PIN, 1, true);
    pio_gpio_init(pio, IR_LED_PIN);
    pio_sm_set_enabled(pio, sm, true);
}

static void put(uint32_t word) {
    watchdog_update();
    pio_sm_put_blocking(pio, sm, word);
}

/* Done when the state machine is left waiting for more; the LED goes back
 * to a plain output, low. */
static void sm_end(void) {
    uint32_t stalled = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    pio->fdebug = stalled;
    while (!(pio->fdebug & stalled)) {
        watchdog_update();
        tight_loop_contents();
    }
    last_end = get_absolute_time();

    pio_sm_set_enabled(pio, sm, false);
    gpio_put(IR_LED_PIN, 0); /* set before it's switched over */
    gpio_set_dir(IR_LED_PIN, GPIO_OUT);
    gpio_set_function(IR_LED_PIN, GPIO_FUNC_SIO);
    pio_clock(false);
}

void irtx_send(const px_ir_burst_t *b) {
    wait_until(delayed_by_us(last_end, b->us[0]));

    uint offset = load(&ir_carrier_program, &carrier_offset);
    pio_sm_config c = ir_carrier_program_get_default_config(offset);
    sm_begin(&c, offset, (float) b->carrier_hz * CYCLES_PER_PERIOD);
    /* Pulses and gaps, then a one-period gap so it ends off. */
    for (int i = 1; i < b->count; i++)
        put(periods(b->us[i], b->carrier_hz));
    put(0);
    sm_end();
}

void irtx_send_redeye(const uint8_t *bytes, int count, bool (*between)(void)) {
    uint offset = load(&redeye_program, &redeye_offset);
    pio_sm_config c = redeye_program_get_default_config(offset);
    sm_config_set_out_shift(&c, false, true, 30); /* a frame a word, from bit 31 down */
    sm_begin(&c, offset, 32768.0f * CYCLES_PER_PERIOD);
    for (int i = 0; i < count; i++) {
        put(hp82240_frame(bytes[i]));
        if (between != NULL) /* the FIFO keeps the frames' timing meanwhile */
            between();
    }
    sm_end();
}
