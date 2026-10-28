#include "speaker.h"

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "cpuclock.h"
#include "debug_checkpoint.h"

/* The sample clock: another PWM slice, used only as a timer -- each time it
 * wraps, at SPEAKER_SAMPLE_HZ, it asks the DMA for the next duty value
 * (its DREQ). Slice 0's pins (GP0/GP1) are keyboard rows under SIO
 * control, so its counter is free. */
#define TIMER_SLICE 0

#define CARRIER_WRAP 1023 /* 10-bit duty steps: 0..1024, 1024 = always on */

/* Duty values waiting to go out, in two blocks of 32ms at 32kHz: the DMA
 * plays one while the other is filled -- time enough for a file read.
 * Two channels, one a block, each chained to the other, so the samples
 * keep going out with no processor involved: a flash write (which parks
 * core 0 and stops interrupts) can't hold the speaker at one level. */
#define BLOCK 1024u

static uint16_t blocks[2][BLOCK];
static int dma_ch[2] = {-1, -1};
static volatile uint32_t blocks_free; /* bit b: block b has played (the DMA interrupt) */
static uint speaker_slice, speaker_chan;

static uint64_t last_tone_us;

/* What core 1 asked for and how far core 0 has got. */
typedef enum {
    IDLE,     /* nothing playing */
    STARTING, /* core 1 has handed over a sound */
    PLAYING,  /* the DMA is playing it; core 0 refills each block it finishes */
    ENDING,   /* all of it is in the blocks: stop after its last sample */
} play_state_t;

static volatile play_state_t state = IDLE;
static volatile bool stop_requested;
static speaker_fill_fn fill_fn;
static void *fill_ctx;
static int playing;      /* the block the DMA is playing */
static int last;         /* the block with the sound's end in it; -1 = not yet */
static uint32_t last_n;  /* how much of that block is the sound */

uint64_t speaker_last_tone_us(void) {
    return state != IDLE ? time_us_64() : last_tone_us;
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
    dma_ch[0] = dma_claim_unused_channel(true);
    dma_ch[1] = dma_claim_unused_channel(true);
}

/* A block has played: the hardware has already chained to the other one.
 * Set this one up to go again (it isn't started: the other's end starts
 * it) and say it's free to fill. The event (SEV) wakes core 0 to fill it
 * -- even had it looked at blocks_free just before going to sleep -- and
 * core 1 waiting in speaker_wait(), to feed the watchdog. In RAM: flash
 * writes park core 0 in RAM. */
static void __not_in_flash_func(block_done_irq)(void) {
    for (int b = 0; b < 2; b++) {
        uint32_t bit = 1u << dma_ch[b];
        if (dma_hw->ints0 & bit) {
            dma_hw->ints0 = bit;
            dma_channel_set_read_addr(dma_ch[b], blocks[b], false);
            dma_channel_set_trans_count(dma_ch[b], BLOCK, false);
            blocks_free |= 1u << b;
        }
    }
    __sev();
}

/* The clocks cpuclock.c leaves off or stops in sleep, kept on for the
 * length of a sound. The DMA's is off altogether (nothing else uses it):
 * its registers can't even be reached without it. And while both cores
 * sleep, the chip stops the clocks not on cpuclock.c's keep list: waiting
 * for the next block in WFI puts this core to sleep, and on battery the
 * other one sleeps too -- the PWM and the DMA would stop, and with them
 * the interrupt that was to end the wait (a hang, the watchdog's reset). */
static void sound_clocks(bool on) {
    static const uint dests[] = {CLK_DEST_SYS_PWM, CLK_DEST_SYS_DMA};
    for (uint i = 0; i < count_of(dests); i++) {
        uint32_t bit = 1u << (dests[i] % 32);
        if (on) {
            hw_set_bits(&clocks_hw->wake_en[dests[i] / 32], bit);
            hw_set_bits(&clocks_hw->sleep_en[dests[i] / 32], bit);
        } else {
            hw_clear_bits(&clocks_hw->sleep_en[dests[i] / 32], bit);
            if (dests[i] == CLK_DEST_SYS_DMA)
                hw_clear_bits(&clocks_hw->wake_en[dests[i] / 32], bit); /* the PWM's stays: always on when awake */
        }
    }
}

/* Fills block b from the sound, the rest with silence; returns how many
 * of its samples are the sound's -- fewer than BLOCK once it has run out. */
static uint32_t fill_block(int b, speaker_fill_fn fill, void *ctx) {
    uint32_t n = 0, got;
    while (n < BLOCK && (got = fill(ctx, &blocks[b][n], BLOCK - n)) != 0)
        n += got;
    memset(&blocks[b][n], 0, (BLOCK - n) * sizeof(blocks[b][0]));
    return n;
}

/* Takes block b if it has played (core 0). */
static bool take_free(int b) {
    uint32_t saved = save_and_disable_interrupts();
    bool done = (blocks_free & (1u << b)) != 0;
    blocks_free &= ~(1u << b);
    restore_interrupts(saved);
    return done;
}

/* ===================== core 0: playing ===================== */

/* The carrier, both blocks full, the DMA, then the sample clock. */
static void begin(void) {
    sound_clocks(true);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, 1);
    pwm_config_set_wrap(&cfg, CARRIER_WRAP);
    pwm_init(speaker_slice, &cfg, false);
    pwm_set_chan_level(speaker_slice, speaker_chan, 0);
    gpio_set_function(SPEAKER_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(speaker_slice, true);

    last = -1;
    last_n = 0;
    uint32_t n = fill_block(0, fill_fn, fill_ctx);
    if (n < BLOCK) {
        last = 0;
        last_n = n;
        memset(blocks[1], 0, sizeof(blocks[1]));
    } else if ((n = fill_block(1, fill_fn, fill_ctx)) < BLOCK) {
        last = 1;
        last_n = n;
    }

    /* Each channel writes its block to the carrier's compare register, one
     * value a sample-clock wrap, then starts the other. A 16-bit write to
     * a peripheral register lands in both halves: channel A gets the same
     * level, harmless -- its pin (GP14, the IR LED) is never the PWM's. */
    for (int b = 0; b < 2; b++) {
        dma_channel_config c = dma_channel_get_default_config(dma_ch[b]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pwm_get_dreq(TIMER_SLICE));
        channel_config_set_chain_to(&c, dma_ch[b ^ 1]);
        dma_channel_configure(dma_ch[b], &c, &pwm_hw->slice[speaker_slice].cc, blocks[b], BLOCK, false);
    }
    blocks_free = 0;
    dma_hw->ints0 = (1u << dma_ch[0]) | (1u << dma_ch[1]);
    irq_set_exclusive_handler(DMA_IRQ_0, block_done_irq);
    dma_channel_set_irq0_enabled(dma_ch[0], true);
    dma_channel_set_irq0_enabled(dma_ch[1], true);
    irq_set_enabled(DMA_IRQ_0, true); /* this core's: the block interrupts come here */

    uint32_t wrap = (clock_get_hz(clk_sys) + SPEAKER_SAMPLE_HZ / 2) / SPEAKER_SAMPLE_HZ - 1;
    cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, 1);
    pwm_config_set_wrap(&cfg, (uint16_t) wrap);
    pwm_init(TIMER_SLICE, &cfg, false);
    playing = 0;
    dma_channel_start(dma_ch[0]);
    pwm_set_enabled(TIMER_SLICE, true);
}

/* Silence: the DMA stopped, the transistor off, the clocks back off. */
static void end(void) {
    /* The channels unchained first, so stopping one can't start the
     * other, then the sample clock. */
    for (int i = 0; i < 2; i++)
        hw_clear_bits(&dma_hw->ch[dma_ch[i]].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort(dma_ch[0]);
    dma_channel_abort(dma_ch[1]);
    pwm_set_enabled(TIMER_SLICE, false);
    irq_set_enabled(DMA_IRQ_0, false);
    dma_channel_set_irq0_enabled(dma_ch[0], false);
    dma_channel_set_irq0_enabled(dma_ch[1], false);
    dma_hw->ints0 = (1u << dma_ch[0]) | (1u << dma_ch[1]);
    irq_remove_handler(DMA_IRQ_0, block_done_irq);

    /* Transistor off: hand the pin back to plain GPIO, driving low. The
     * PWM output could be stopped mid-pulse (high), so don't just disable
     * the slice -- take the pin away from it. */
    gpio_put(SPEAKER_PIN, 0);
    gpio_set_function(SPEAKER_PIN, GPIO_FUNC_SIO);
    pwm_set_enabled(speaker_slice, false);
    sound_clocks(false);
    last_tone_us = time_us_64();
}

bool speaker_core0_poll(void) {
    play_state_t s = state;
    if (s == IDLE)
        return false;
    if (stop_requested) {
        if (s != STARTING)
            end();
        state = IDLE;
        __sev(); /* core 1, waiting in speaker_stop() */
        return false;
    }
    if (s == STARTING) {
        begin();
        s = last < 0 ? PLAYING : ENDING;
    }

    /* Refill each block as it finishes, until the sound has run out. */
    while (s == PLAYING && take_free(playing)) {
        int b = playing;
        playing = b ^ 1;
        uint32_t n = fill_block(b, fill_fn, fill_ctx);
        if (n < BLOCK) {
            last = b;
            last_n = n;
            s = ENDING;
        }
    }

    /* Then on to the block with the end (silence after it, should the
     * chain get past it), and stop right after its last sample -- not at
     * the block's end: a waiting sound (speaker_play()) would last up to
     * 32ms longer. Close to the end, keep looking without sleeping. */
    if (s == ENDING) {
        if (playing != last) {
            if (!take_free(playing)) {
                state = s;
                return false;
            }
            memset(blocks[playing], 0, sizeof(blocks[0]));
            playing = last;
        }
        if ((blocks_free & (1u << last)) || BLOCK - dma_channel_hw_addr(dma_ch[last])->transfer_count >= last_n) {
            end();
            state = IDLE;
            __sev(); /* core 1, waiting in speaker_wait() */
            return false;
        }
        state = s;
        return true;
    }
    state = s;
    return false;
}

/* ===================== core 1: asking ===================== */

bool speaker_busy(void) {
    return state != IDLE;
}

void speaker_stop(void) {
    if (state == IDLE)
        return;
    stop_requested = true;
    __sev();
    while (state != IDLE) {
        watchdog_update();
        tight_loop_contents();
    }
    stop_requested = false;
}

void speaker_start(speaker_fill_fn fill, void *ctx) {
    speaker_stop();
    cpuclock_set_fast(true);
    fill_fn = fill;
    fill_ctx = ctx;
    __dmb(); /* the sound in place before core 0 sees STARTING */
    state = STARTING;
    __sev();
}

void speaker_wait(void) {
    debug_checkpoint_saved_t saved = debug_checkpoint_save();
    debug_checkpoint(CKPT_SOUND, 0);
    /* Core 0 signals (SEV) as each block ends and when the sound does: no
     * wait here is longer than a block, 32ms. */
    while (state != IDLE) {
        watchdog_update();
        __wfe();
    }
    debug_checkpoint_restore(saved);
}

void speaker_play(speaker_fill_fn fill, void *ctx) {
    speaker_start(fill, ctx);
    speaker_wait();
}
