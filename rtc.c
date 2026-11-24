#include "rtc.h"
#include "timemodule.h"
#include "stopwatch.h"
#include "ra8900.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/powman.h"
#include "hardware/watchdog.h"
#include "hardware/timer.h"
#include "settings.h"
#include "cpuclock.h"

/* Where the clock starts after a power-on without the V1.1 board's clock
 * chip, or with one that lost the time: 2021-03-05 00:59:00. */
#define RTC_DEFAULT_YEAR   2021
#define RTC_DEFAULT_MONTH  3
#define RTC_DEFAULT_DAY    5
#define RTC_DEFAULT_HOUR   0
#define RTC_DEFAULT_MINUTE 59

#define MS_PER_DAY (86400ull * 1000ull)

/* A rate in Hz with the trim applied: telling the clock its source runs
 * a little slower than it does makes it count faster, and vice versa. */
static uint32_t trimmed(uint32_t hz) {
    return (uint32_t) ((int64_t) hz - (int64_t) hz * px_settings.time_trim / 864000);
}

void rtc_apply_trim(void) {
    powman_timer_set_1khz_tick_source_xosc_with_hz(trimmed(XOSC_HZ));
}

/* ===================== the clock chip (board V1.1) ===================== */

/* The V1.1 board has an Epson RA8900CE (ra8900.h) on I2C1, the bus the V1
 * board left free for it, its /INT on GP17; V1 has nothing there. The chip
 * keeps the time through anything (RESET, an empty battery: it has its own
 * supply pin) and to +-5 ppm, temperature compensated -- the always-on
 * timer drifted 0.4-2% while off, from the low-power oscillator.
 *
 * The always-on timer stays the clock everything reads (it has the
 * milliseconds, and reading it costs nothing), kept to the chip: at each
 * boot and every hour on it's put into the chip's second at once, then to
 * the millisecond when the second turns -- the chip's update interrupt
 * pulses /INT then (edge_irq()). While off the chip's countdown wakes the
 * calculator (rtc_chip_wake_at(), /INT a power-manager wake source), as
 * its second turns: the wake knows the time exactly. And the chip is
 * written whenever the time is set. */
#define CHIP_I2C      i2c1
#define CHIP_SDA_PIN  26
#define CHIP_SCL_PIN  27
#define CHIP_INT_PIN  17     /* /INT, open drain, low 7.8 ms per event; UART0 RX on V1, never read */
#define CHIP_BAUD     100000 /* the chip takes 400 kHz; the bus may only have the pads' pull-ups */
#define CHIP_TIMEOUT_US 5000 /* a transfer: ~1 ms at 100 kHz */
/* After its supply comes up the chip answers only 30 ms later (tCL). */
#define CHIP_READY_US 40000
#define CHIP_POLL_US  (3600ull * 1000000ull)
/* The update interrupt is left on at most this long for the second's turn. */
#define EDGE_WAIT_US  3000000

/* The flags written to clear these and leave the rest (only a 0 can be
 * written to a flag; a 1 changes nothing). */
#define FLAG_CLEAR(bits) ((uint8_t) ~(bits))

static bool chip_found;
static uint64_t chip_synced_us;

/* The time at /INT's next pulse (0: none expected) -- set from the
 * interrupt on core 1. */
static volatile uint64_t edge_ms;
static bool edge_waiting;
static uint64_t edge_since_us;
static bool edge_irq_installed;

bool rtc_has_chip(void) {
    return chip_found;
}

/* The bus on its pins, at the baud rate for clk_sys now (I2C counts in
 * clk_sys, which goes between 150 and 48 MHz): before each use. The
 * block's clock is off otherwise (cpuclock.c): without it i2c_init() waits
 * forever for the block to come out of reset -- the first boot on a V1.1
 * board hung there, the screen blank. */
static void chip_bus(void) {
    hw_set_bits(&clocks_hw->wake_en[CLK_DEST_SYS_I2C1 / 32], 1u << (CLK_DEST_SYS_I2C1 % 32));
    i2c_init(CHIP_I2C, CHIP_BAUD);
    gpio_set_function(CHIP_SDA_PIN, GPIO_FUNC_I2C); /* also lifts the pads' isolation after a power-down */
    gpio_set_function(CHIP_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(CHIP_SDA_PIN);
    gpio_pull_up(CHIP_SCL_PIN);
}

/* After each use: the block back in reset, its clock off. */
static void chip_bus_end(void) {
    i2c_deinit(CHIP_I2C);
    hw_clear_bits(&clocks_hw->wake_en[CLK_DEST_SYS_I2C1 / 32], 1u << (CLK_DEST_SYS_I2C1 % 32));
}

static bool chip_read(uint8_t reg, uint8_t *buf, size_t n) {
    if (i2c_write_timeout_us(CHIP_I2C, RA8900_ADDR, &reg, 1, true, CHIP_TIMEOUT_US) != 1)
        return false;
    return i2c_read_timeout_us(CHIP_I2C, RA8900_ADDR, buf, n, false, CHIP_TIMEOUT_US) == (int) n;
}

static bool chip_write(uint8_t reg, const uint8_t *buf, size_t n) {
    uint8_t msg[8];
    msg[0] = reg;
    memcpy(msg + 1, buf, n);
    return i2c_write_timeout_us(CHIP_I2C, RA8900_ADDR, msg, n + 1, false, CHIP_TIMEOUT_US) == (int) n + 1;
}

static bool chip_write1(uint8_t reg, uint8_t value) {
    return chip_write(reg, &value, 1);
}

/* The chip's time, whole seconds. */
static bool chip_read_ms(uint64_t *ms) {
    uint8_t regs[7];
    return chip_read(RA8900_SEC, regs, 7) && ra8900_decode(regs, ms);
}

/* No countdown, no interrupts, their flags cleared: /INT stays high. On
 * the bus. */
static void chip_quiet(void) {
    if (edge_waiting) {
        gpio_set_irq_enabled(CHIP_INT_PIN, GPIO_IRQ_EDGE_FALL, false);
        edge_ms = 0;
        edge_waiting = false;
    }
    uint8_t regs[3] = {RA8900_EXT_SET, FLAG_CLEAR(RA8900_FLAG_UF | RA8900_FLAG_TF | RA8900_FLAG_AF), RA8900_CTRL_CSEL};
    chip_write(RA8900_EXT, regs, 3);
}

/* Looks for the chip (V1.1), leaving the pins as they were on a V1 board:
 * unconnected there, with the pull-downs they come up with. GP17 is the
 * chip's /INT (open drain) on V1.1 and unconnected on V1: an input with
 * its pull-up on both (the pull-down on an enabled input with nothing
 * connected is RP2350 erratum E9's leak). */
static bool chip_find(void) {
    gpio_init(CHIP_INT_PIN);
    gpio_pull_up(CHIP_INT_PIN);
    chip_bus();
    uint8_t flag;
    chip_found = chip_read(RA8900_FLAG, &flag, 1);
    chip_bus_end();
    if (!chip_found) {
        gpio_set_function(CHIP_SDA_PIN, GPIO_FUNC_NULL);
        gpio_set_function(CHIP_SCL_PIN, GPIO_FUNC_NULL);
        gpio_pull_down(CHIP_SDA_PIN);
        gpio_pull_down(CHIP_SCL_PIN);
    }
    return chip_found;
}

/* Gives the chip our time. It counts whole seconds, and its RESET starts the
 * second over: written just after ours turns, so the two turn together
 * (within a millisecond or two). Waits up to a second for that -- only
 * when the time is set, or the chip had lost it. On the bus. */
static void chip_set_on_bus(void) {
    if (edge_waiting)
        chip_quiet(); /* the timer is ours now: no turn to catch */
    uint64_t now = powman_timer_get_ms();
    if (now % 1000 > 2) {
        uint64_t next = now / 1000 * 1000 + 1000;
        while ((now = powman_timer_get_ms()) < next)
            watchdog_update();
    }
    uint8_t regs[7];
    if (!ra8900_encode(now, regs))
        return; /* a year it can't count (2000-2099): it keeps what it had */
    /* RESET first: the next second is then 1000 ms off, so it can't turn
     * between the two writes. */
    static const uint8_t ctrl[3] = {RA8900_EXT_SET, RA8900_FLAG_SET, RA8900_CTRL_SET};
    if (chip_write(RA8900_EXT, ctrl, 3))
        chip_write(RA8900_SEC, regs, 7);
}

static void chip_set(void) {
    chip_bus();
    chip_set_on_bus();
    chip_bus_end();
}

/* /INT fell: the chip's second turned (the update interrupt). Core 1. */
static void edge_irq(void) {
    if (!(gpio_get_irq_event_mask(CHIP_INT_PIN) & GPIO_IRQ_EDGE_FALL))
        return;
    gpio_acknowledge_irq(CHIP_INT_PIN, GPIO_IRQ_EDGE_FALL);
    uint64_t ms = edge_ms;
    if (ms != 0) {
        powman_timer_set_ms(ms);
        edge_ms = 0;
    }
}

/* Has the update interrupt mark the next turn of the chip's second, for
 * edge_irq() to set the timer to it. Core 1, on the bus. */
static void edge_catch(void) {
    if (!edge_irq_installed) {
        gpio_add_raw_irq_handler(CHIP_INT_PIN, edge_irq);
        irq_set_enabled(IO_IRQ_BANK0, true);
        edge_irq_installed = true;
    }
    edge_ms = 0;
    gpio_set_irq_enabled(CHIP_INT_PIN, GPIO_IRQ_EDGE_FALL, true); /* clears an old edge */
    if (!chip_write1(RA8900_CTRL, RA8900_CTRL_CSEL | RA8900_CTRL_UIE)) {
        gpio_set_irq_enabled(CHIP_INT_PIN, GPIO_IRQ_EDGE_FALL, false);
        return;
    }
    edge_waiting = true;
    edge_since_us = time_us_64();
    /* Read twice: a turn between the reading and edge_ms being set would
     * be missed -- the second reading then names the turn after it. */
    uint64_t first, again;
    if (!chip_read_ms(&first))
        return;
    edge_ms = first + 1000;
    if (chip_read_ms(&again) && again != first)
        edge_ms = again + 1000;
}

/* Our clock from the chip's. woke_by_chip: the chip's countdown woke the
 * calculator just now, as its second turned -- the time is that second and
 * the time since. Otherwise into the chip's second at once, and on core 1
 * to the millisecond at the next turn. A chip that has lost the time gets
 * ours. */
static void chip_sync(bool woke_by_chip, bool catch_edge) {
    chip_synced_us = time_us_64();
    chip_bus();
    uint8_t flag;
    uint64_t chip_ms;
    if (!chip_read(RA8900_FLAG, &flag, 1)) {
        chip_bus_end();
        return;
    }
    if ((flag & RA8900_FLAG_VLF) || !chip_read_ms(&chip_ms)) {
        chip_set_on_bus();
        chip_bus_end();
        return;
    }
    chip_quiet();
    /* Drift corrected, not the time set: the stopwatch and the drift
     * measurement don't hear of it. */
    uint64_t since_wake_ms = time_us_64() / 1000; /* the timer starts at the wake's reset */
    if (woke_by_chip && (flag & RA8900_FLAG_TF) && since_wake_ms < 1000) {
        powman_timer_set_ms(chip_ms + since_wake_ms);
    } else {
        uint64_t now = powman_timer_get_ms();
        uint64_t settled = ra8900_settle(now, chip_ms);
        if (llabs((int64_t) (settled - now)) > 1000)
            settled = chip_ms + 500; /* far off (a power-on, a long OFF): the middle, until the turn */
        if (settled != now)
            powman_timer_set_ms(settled);
        if (catch_edge)
            edge_catch();
    }
    chip_bus_end();
}

void rtc_chip_wake(bool woke_by_chip) {
    if (chip_find())
        chip_sync(woke_by_chip, false);
}

bool rtc_chip_wake_at(uint64_t wake_ms) {
    if (!chip_found)
        return false;
    chip_bus();
    chip_quiet();
    bool ok = true;
    if (wake_ms != 0) {
        ra8900_timer_t t = ra8900_wake_timer(powman_timer_get_ms(), wake_ms);
        /* The count is written with the countdown stopped (TE 0), then
         * started: it always starts from the count written. */
        uint8_t regs[5] = {(uint8_t) t.count, (uint8_t) (t.count >> 8), t.tsel,
                           FLAG_CLEAR(RA8900_FLAG_UF | RA8900_FLAG_TF | RA8900_FLAG_AF),
                           RA8900_CTRL_CSEL | RA8900_CTRL_TIE};
        ok = chip_write(RA8900_TIMER, regs, 5) && chip_write1(RA8900_EXT, t.tsel | RA8900_EXT_TE);
    }
    chip_bus_end();
    return ok;
}

void rtc_chip_poll(void) {
    if (!chip_found)
        return;
    if (edge_waiting && (edge_ms == 0 || time_us_64() - edge_since_us >= EDGE_WAIT_US)) {
        chip_bus();
        chip_quiet();
        chip_bus_end();
    }
    if (time_us_64() - chip_synced_us >= CHIP_POLL_US)
        chip_sync(false, true);
}

int rtc_chip_phase_ms(void) {
    if (!chip_found)
        return RTC_CHIP_NO_PHASE;
    chip_bus();
    uint8_t first, sec;
    int phase = RTC_CHIP_NO_PHASE;
    if (chip_read(RA8900_SEC, &first, 1)) {
        uint64_t give_up = time_us_64() + 1100000;
        while (time_us_64() < give_up && chip_read(RA8900_SEC, &sec, 1)) {
            if (sec != first) {
                uint64_t now = powman_timer_get_ms(); /* a read is ~0.3 ms at 100 kHz */
                int ms = (int) (now % 1000);
                phase = ms >= 500 ? ms - 1000 : ms;
                break;
            }
        }
    }
    chip_bus_end();
    return phase;
}

bool rtc_chip_time(rtc_datetime_t *dt) {
    if (!chip_found)
        return false;
    chip_bus();
    uint64_t ms;
    bool ok = chip_read_ms(&ms);
    chip_bus_end();
    if (ok)
        time_ms_to_datetime(ms, dt);
    return ok;
}

/* ===================== the clock ===================== */

void rtc_clock_init(void) {
    /* Ticks from the 12MHz crystal, not the internal low-power RC
     * oscillator: the LPOSC isn't trimmed to a crystal and can drift by
     * minutes per day, too much for a calendar clock. The crystal keeps
     * running through this firmware's sleep (WFI, not dormant mode). */
    rtc_apply_trim();

    /* Powman sits in the always-on power domain, so a watchdog or soft
     * reset can leave it running with the correct time -- only start it
     * over from the default after an actual power-on (or a RESET). The
     * chip, if the board has one, then says what time it really is. */
    if (!powman_timer_is_running()) {
        powman_timer_set_ms((uint64_t) time_days_from_civil(RTC_DEFAULT_YEAR, RTC_DEFAULT_MONTH, RTC_DEFAULT_DAY) * MS_PER_DAY
                            + ((uint64_t) RTC_DEFAULT_HOUR * 3600 + RTC_DEFAULT_MINUTE * 60) * 1000);
        powman_timer_start();
        /* A power-on: the chip's supply may have just come up too. */
        while (time_us_64() < CHIP_READY_US)
            tight_loop_contents();
    }
    if (chip_find())
        chip_sync(false, true);
}

uint64_t rtc_now_ms(void) {
    return powman_timer_get_ms();
}

void rtc_get_datetime(rtc_datetime_t *dt) {
    time_ms_to_datetime(powman_timer_get_ms(), dt);
}

void rtc_get_hms(int *hour, int *min, int *sec) {
    rtc_datetime_t dt;
    rtc_get_datetime(&dt);
    *hour = dt.hour;
    *min = dt.min;
    *sec = dt.sec;
}

/* The clock was just set from old_ms to new_ms: if that was a correction
 * (under 30 minutes) at least 6 hours after the last setting, it measures
 * the drift. Either way, this is the new reference. */
static bool measured_now;

bool rtc_set_measured(void) {
    return measured_now;
}

static void note_time_set(uint64_t old_ms, uint64_t new_ms) {
    int64_t error_ms = (int64_t) (old_ms - new_ms); /* + = the clock was fast */
    int64_t span_ms = (int64_t) (new_ms - px_settings.cal_ref_ms);
    measured_now = px_settings.cal_ref_ms != 0 && llabs(error_ms) < 30 * 60000 && span_ms >= 6 * 3600000LL;
    if (measured_now) {
        int64_t drift = error_ms * 864000 / span_ms; /* tenths of a second per day */
        px_settings.cal_drift = (int16_t) (drift > 32767 ? 32767 : drift < -32767 ? -32767 : drift);
        px_settings.cal_span_h = (uint16_t) (span_ms / 3600000 > 65535 ? 65535 : span_ms / 3600000);
    }
    px_settings.cal_ref_ms = new_ms;
    settings_save_later();
}

void rtc_set_ms_of_day(uint32_t ms) {
    uint64_t old_ms = powman_timer_get_ms();
    uint64_t new_ms = old_ms / MS_PER_DAY * MS_PER_DAY + ms;
    powman_timer_set_ms(new_ms);
    stopwatch_clock_moved((int64_t) (new_ms - old_ms));
    note_time_set(old_ms, new_ms);
    if (chip_found)
        chip_set();
}

void rtc_set_hms(int hour, int min, int sec) {
    rtc_set_ms_of_day((uint32_t) (hour * 3600 + min * 60 + sec) * 1000);
}

/* With the clock chip, only the years it counts (2000-2099): another would
 * be left out of it (chip_set_on_bus()), and the next sync would put the
 * clock back to the chip's date within the hour. */
static bool chip_can_hold(uint64_t ms) {
    uint8_t regs[7];
    return !chip_found || ra8900_encode(ms, regs);
}

bool rtc_add_ms(int64_t ms) {
    uint64_t old_ms = powman_timer_get_ms();
    if (ms < 0 && (uint64_t) -ms > old_ms)
        return false;
    if (!chip_can_hold(old_ms + (uint64_t) ms))
        return false;
    powman_timer_set_ms(old_ms + (uint64_t) ms);
    stopwatch_clock_moved(ms);
    if (px_settings.cal_ref_ms != 0) {
        px_settings.cal_ref_ms += (uint64_t) ms;
        settings_save_later();
    }
    if (chip_found)
        chip_set();
    return true;
}

void rtc_set_trim(int tenths) {
    px_settings.time_trim = (int16_t) (tenths > 9999 ? 9999 : tenths < -9999 ? -9999 : tenths);
    rtc_apply_trim();
}

void rtc_measure_from_now(void) {
    px_settings.cal_ref_ms = powman_timer_get_ms();
    px_settings.cal_span_h = 0;
    settings_save_later();
}

bool rtc_set_date(int year, int month, int day) {
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > time_days_in_month(year, month))
        return false;
    uint64_t old_ms = powman_timer_get_ms();
    uint64_t new_ms = (uint64_t) time_days_from_civil(year, month, day) * MS_PER_DAY + old_ms % MS_PER_DAY;
    if (!chip_can_hold(new_ms))
        return false;
    powman_timer_set_ms(new_ms);
    stopwatch_clock_moved((int64_t) (new_ms - old_ms));
    /* Whole days moved: the calibration reference moves with them. */
    if (px_settings.cal_ref_ms != 0 && new_ms != old_ms) {
        px_settings.cal_ref_ms += new_ms - old_ms;
        settings_save_later();
    }
    if (chip_found)
        chip_set();
    return true;
}

/* ===================== the low-power oscillator ===================== */

/* The frequency counter, against clk_ref (the crystal): 1/32kHz steps, so
 * one reading is good to ~0.1% of the LPOSC's ~32kHz. Averaging 8 readings,
 * each over the longest window (~33ms), gets well below that. */
#define LPOSC_READINGS 8
/* At a wake while off: two (~70ms), for the cost of the wake -- ~0.1%. */
#define LPOSC_QUICK_READINGS 2

/* How much of the straight line through the two measurements the rate
 * follows below them (rtc_lposc_hz_at()), in percent: measured on the
 * PX-42F, Oct 2026. */
#define LPOSC_CURVE_PERCENT 72

/* One measurement at the present core voltage, untrimmed: the average of
 * readings readings. */
static uint32_t measure_lposc(int readings) {
    fc_hw_t *fc = &clocks_hw->fc0;
    uint64_t sum = 0; /* in 1/32 kHz */
    busy_wait_ms(2); /* the oscillator settling, after a voltage change */
    for (int i = 0; i < readings; i++) {
        watchdog_update(); /* the whole measurement outlasts the 100ms window */
        while (fc->status & CLOCKS_FC0_STATUS_RUNNING_BITS)
            tight_loop_contents();
        fc->ref_khz = clock_get_hz(clk_ref) / 1000;
        fc->interval = 15;
        fc->min_khz = 0;
        fc->max_khz = 0xffffffff;
        fc->src = CLOCKS_FC0_SRC_VALUE_LPOSC_CLKSRC; /* starts it */
        while (!(fc->status & CLOCKS_FC0_STATUS_DONE_BITS))
            tight_loop_contents();
        sum += fc->result;
    }
    uint32_t hz = (uint32_t) (sum * 1000 / (32 * (uint64_t) readings));
    if (hz < 20000 || hz > 50000)
        hz = 32768; /* a reading that can't be right: the nominal rate */
    return hz;
}

uint32_t rtc_lposc_hz_at(int mv, uint32_t *at_fast, uint32_t *at_slow) {
    cpuclock_set_fast(true);
    uint32_t fast = measure_lposc(LPOSC_READINGS);
    cpuclock_set_fast(false);
    uint32_t slow = measure_lposc(LPOSC_READINGS);
    if (at_fast != NULL)
        *at_fast = fast;
    if (at_slow != NULL)
        *at_slow = slow;
    /* Below the slow voltage the rate climbs less steeply than between the
     * two measured: off at 0.90V, the clock ran 0.8% slow with a straight
     * line (twice, 3.7h and 5h), which puts the curve at 72% of it. */
    int64_t hz = (int64_t) slow
                 + ((int64_t) slow - fast) * (CPUCLOCK_SLOW_MV - mv) * LPOSC_CURVE_PERCENT
                           / ((CPUCLOCK_FAST_MV - CPUCLOCK_SLOW_MV) * 100);
    if (hz < 20000 || hz > 50000)
        hz = slow; /* a line that can't be right: the nearest measurement */
    return trimmed((uint32_t) hz);
}

uint32_t rtc_lposc_hz_again(uint32_t off_hz, uint32_t slow_hz) {
    cpuclock_set_fast(false);
    uint32_t now = measure_lposc(LPOSC_QUICK_READINGS);
    if (off_hz == 0 || slow_hz == 0)
        return 0; /* nothing to scale: keep the rate set */
    return (uint32_t) ((uint64_t) off_hz * now / slow_hz);
}
