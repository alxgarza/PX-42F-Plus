#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "free42_app.h"
#include "display.h"
#include "keyboard.h"
#include "font_24x10.h"
#include "font_10x16.h"

/* Free42's core is a full desktop-oriented C++ calculator engine (BID128
 * decimal string parsing/formatting, nested command dispatch, etc.), and a
 * single call chain during ordinary digit entry can run several frames
 * deep (core_keydown -> keydown -> keydown_number_entry -> string2phloat
 * -> bid128_from_string, plus redisplay() -> phloat2string() ->
 * bid128_to_string() on top). The Pico SDK's default core0 stack is only
 * 0x800 (2KB), and even the most that fits in RP2350's dedicated SCRATCH_Y
 * bank (where the SDK's linker script places the core0 stack) is 4KB --
 * both proved too small: repeated digit keystrokes reliably hung inside
 * core_keydown() (confirmed by an isolated test showing the BID library
 * itself is fine in a *shallow* call, so it's the combined depth of the
 * real dispatch chain that overflows). Rather than fight the linker script
 * to relocate the stack out of that 4KB bank, run all of Free42 on core 1
 * with an explicit, generously-sized stack in ordinary SRAM instead --
 * RP2350 has 520KB of it, so there's no cost to being generous here. */
#define CORE1_STACK_WORDS (32 * 1024 / sizeof(uint32_t))
static uint32_t core1_stack[CORE1_STACK_WORDS];

/* ===================== GPIO pin assignment =====================
 * GP23, GP24, GP25, GP29 are reserved for the onboard CYW43 wireless
 * chip (pico2_w) and are not available on the header -- do not use them.
 * Free: GP22, GP26, GP27, GP28.
 * Display pins (SPI0, RS, RST) are defined in display.h.
 * Keyboard matrix pins (rows GP0-7, columns GP8-12) are defined in
 * keyboard.h.
 * ================================================================= */

/* --- Speaker / IR ---
 * The speaker is differentially (bridge) driven across two GPIOs instead
 * of one GPIO vs. GND: SPEAKER_P and SPEAKER_N are always driven to
 * opposite logic levels, so the piezo element sees the full swing between
 * +3.3V and -3.3V (6.6V peak-to-peak) instead of 0-3.3V. Both must idle at
 * the *same* level (both low) when silent, or the element sees a standing
 * DC bias. Both pins run at 12mA drive strength -- see the current-budget
 * note in init_speaker_pin(). Connect the buzzer directly between the two
 * pins; neither side connects to GND.
 */
#define SPEAKER_P_PIN 13
#define SPEAKER_N_PIN 15
#define IR_LED_PIN    14  /* PWM-capable; drive via transistor, see note */

/* --- Debug serial (UART0, separate from the USB CDC console) --- */
#define SERIAL_UART     uart0
#define SERIAL_BAUD     115200
#define SERIAL_TX_PIN   16
#define SERIAL_RX_PIN   17

static void init_speaker_pin(void) {
    gpio_init(SPEAKER_P_PIN);
    gpio_init(SPEAKER_N_PIN);
    gpio_set_dir(SPEAKER_P_PIN, GPIO_OUT);
    gpio_set_dir(SPEAKER_N_PIN, GPIO_OUT);

    /* Both start low (zero differential across the buzzer -- silent). */
    gpio_put(SPEAKER_P_PIN, 0);
    gpio_put(SPEAKER_N_PIN, 0);

    /* Max drive strength on both sides of the bridge. Note this does not
     * raise the pin's output voltage (still 3.3V logic) -- the "double
     * voltage" comes from the differential swing across the two pins, not
     * from the drive strength setting. 12mA is the practical ceiling for
     * continuous use per pad; see the design-chat note on buzzer current. */
    gpio_set_drive_strength(SPEAKER_P_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(SPEAKER_N_PIN, GPIO_DRIVE_STRENGTH_12MA);
}

static void init_ir_pin(void) {
    gpio_init(IR_LED_PIN);
    gpio_set_dir(IR_LED_PIN, GPIO_OUT);
    gpio_put(IR_LED_PIN, 0);
}

static void init_serial(void) {
    uart_init(SERIAL_UART, SERIAL_BAUD);
    gpio_set_function(SERIAL_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(SERIAL_RX_PIN, GPIO_FUNC_UART);
}

static void core1_main(void)
{
    display_init();

    display_clear();
    display_draw_string(&font_24x10, "PX-42S", 0, 0, 0x00);
    /* y=30 is arbitrary -- not a multiple of any hardware block size --
     * to demonstrate pixel-exact placement alongside the other font. */
    display_draw_string(&font_10x16, "HELLO AGAIN", 0, 30, 0x00);
    display_flush();
    sleep_ms(3000);
    // for(;;);

    keyboard_init();
    init_speaker_pin();
    init_ir_pin();
    init_serial();

    if (watchdog_caused_reboot()) {
        printf("Rebooted by Watchdog!\n");
    }

    /* Requires watchdog_update() at least this often or the chip reboots. */
    watchdog_enable(100, 1);
    watchdog_update();

    /* Cold-start the Free42 core and print the initial (all-zero) stack. */
    free42_init();
    printf("Ready. Type digits, +-*/, or Enter.\n");

    while (true) {
        watchdog_update();

        /* Drops the panel back to Low Power Mode once it's been idle for a
         * bit since the last redraw -- see display_tick()/display_flush()
         * in display.c. */
        display_tick();

        /* USB serial console input (kept for convenient testing/typing). */
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            free42_process_char((char) c);
        }

        /* Physical keyboard matrix input. Split into down/hold/up instead
         * of one shot, so we can drive the real HP-42S/Free42 "hold to
         * confirm" timing: hold 250ms and the key's function name appears
         * on the Y row (free42_key_timeout1()); keep holding to 2000ms
         * and it switches to "NULL", meaning release now cancels the key
         * instead of performing it (free42_key_timeout2()). See the call
         * sequence documented in free42_app.h. */
        uint8_t row, col;
        if (keyboard_scan(&row, &col)) {
            free42_key_down(row, col);

            bool enqueued = free42_key_is_enqueued();
            int repeat_mode = free42_key_repeat_mode();
            absolute_time_t press_time = get_absolute_time();
            absolute_time_t next_repeat_time =
                    make_timeout_time_ms(repeat_mode == 1 ? 1000 : 500);
            bool shown_name = false;
            bool shown_null = false;

            /* Must feed the watchdog in here too: any ordinary human
             * keypress (even just 100-200ms, completely normal) already
             * exceeds the 100ms watchdog window, so without this every
             * single button press -- not just a stuck/miswired one --
             * would starve the watchdog and reboot. The serial input path
             * never hits this because it has no release-wait at all.
             *
             * This must NOT give up early on a still-held key: an earlier
             * version capped the wait at 2s, but a press held longer than
             * that fell through to the top of the main loop while the key
             * was still down, and the very next scan saw it as a brand
             * new press -- firing the same key repeatedly for as long as
             * it was held. The 6000-iteration (60s) cap here is only a
             * safety net for a truly stuck/miswired key, not something a
             * real press should ever reach. */
            for (int i = 0; i < 6000 && keyboard_scan(&row, &col); i++) {
                watchdog_update();

                if (!enqueued && repeat_mode != 0) {
                    if (absolute_time_diff_us(get_absolute_time(), next_repeat_time) <= 0) {
                        free42_key_repeat_tick();
                        repeat_mode = free42_key_repeat_mode();
                        next_repeat_time = make_timeout_time_ms(repeat_mode == 1 ? 200 : 100);
                    }
                } else if (!enqueued) {
                    int64_t held_ms = absolute_time_diff_us(press_time, get_absolute_time()) / 1000;
                    if (!shown_name && held_ms >= 250) {
                        free42_key_timeout1();
                        shown_name = true;
                    } else if (shown_name && !shown_null && held_ms >= 2000) {
                        free42_key_timeout2();
                        shown_null = true;
                    }
                }

                sleep_ms(10);
            }

            free42_key_up();
        }

        sleep_ms(10);
    }
}

int main()
{
    stdio_init_all();

    /* Everything else runs on core 1, with the large explicit stack
     * declared above -- see the comment there for why. */
    multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof(core1_stack));

    while (true)
        tight_loop_contents();
}
