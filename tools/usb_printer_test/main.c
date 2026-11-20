/* USB host test for the PX-42F: the RP2350's USB port as host, an ESC/POS
 * printer on it, a test slip printed. Every step shown on the panel. */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"
#include "pico/multicore.h"
#include "display.h"
#include "font_8x12.h"
#include "font_6x8.h"
#include <stdarg.h>

/* TinyUSB's log (CFG_TUSB_DEBUG_PRINTF), on the screen in the 6x8 font: the
 * last LOG_LINES lines. Called from the USB interrupt too, so it only
 * stores; the main loop draws. */
#define LOG_LINES 13
#define LOG_COLS  41
static char log_text[LOG_LINES][LOG_COLS + 1];
static char log_part[128];
static volatile bool log_changed;
static int log_after_stall = -1; /* lines left before the log freezes */

int log_printf(const char *fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    uint32_t irq = save_and_disable_interrupts();
    for (const char *c = buf; *c; c++) {
        size_t len = strlen(log_part);
        if (*c == '\n') {
            if (len && log_after_stall != 0 && !strstr(log_part, "Buffer complete") && !strstr(log_part, "Transfer complete")) {
                if (log_after_stall > 0)
                    log_after_stall--;
                else if (strstr(log_part, "STALLED") || strstr(log_part, "FAILED"))
                    log_after_stall = 2;
                memmove(log_text[0], log_text[1], sizeof log_text - sizeof log_text[0]);
                snprintf(log_text[LOG_LINES - 1], LOG_COLS + 1, "%s", log_part);
                log_changed = true;
            }
            log_part[0] = 0;
        } else if (*c != '\r' && len < sizeof log_part - 1) {
            log_part[len] = *c;
            log_part[len + 1] = 0;
        }
    }
    restore_interrupts(irq);
    return n;
}

static void log_draw(const char *counter) {
    display_clear();
    for (int i = 0; i < LOG_LINES; i++)
        display_draw_string(&font_6x8, log_text[i], 0, (uint8_t)(i * 9), 0);
    display_draw_string(&font_6x8, counter, 0, DISPLAY_HEIGHT - 8, 0);
    display_flush();
}


static void say(const char *s) {
    log_printf(">> %s\n", s);
}



/* TinyUSB's RP2 host driver never resets the bus (its hcd_port_reset() is
 * empty): this printer answers Set Address with a STALL without one. The
 * controller's own reset, linked in place of it (-Wl,--wrap). */
#include "hardware/structs/usb.h"
/* The reset's SE0 looks to the controller like an unplug, then a plug: the
 * driver's interrupt would start enumeration over, which resets again...
 * So the connect interrupt is masked from the reset to its end, and the
 * flag cleared then if the device is still there. */
/* A real bus reset: the controller's RESET_BUS bit clears itself after
 * 2 us (measured), USB wants >= 10 ms of SE0. Both lines driven low through
 * the PHY's override, then let go. */
static void bus_reset_start(void) {
    hw_clear_bits(&usb_hw->inte, USB_INTE_HOST_CONN_DIS_BITS);
    usb_hw->phy_direct = USB_USBPHY_DIRECT_TX_DP_OE_BITS | USB_USBPHY_DIRECT_TX_DM_OE_BITS; /* DP = DM = 0, single-ended */
    usb_hw->phy_direct_override = USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DIFFMODE_OVERRIDE_EN_BITS;
    log_printf(">> SE0 on\n");
}

static void bus_reset_end(void) {
    usb_hw->phy_direct_override = 0;
    usb_hw->phy_direct = 0;
    /* The controller takes a while to see the device again after the SE0
     * (20 ms wasn't enough: speed 0): wait for it, half a second at most,
     * then the 10 ms of reset recovery. */
    uint32_t t0 = time_us_32(), speed;
    while (!(speed = usb_hw->sie_status & USB_SIE_STATUS_SPEED_BITS) && time_us_32() - t0 < 500000)
        tight_loop_contents();
    uint32_t back_us = time_us_32() - t0;
    busy_wait_ms(10);
    if (speed) /* still there: the "change" was the reset's */
        hw_clear_alias(usb_hw)->sie_status = USB_SIE_STATUS_SPEED_BITS;
    hw_set_bits(&usb_hw->inte, USB_INTE_HOST_CONN_DIS_BITS);
    log_printf(">> SE0 off, speed %lu after %lu us\n", (unsigned long)(speed >> USB_SIE_STATUS_SPEED_LSB),
               (unsigned long)back_us);
}

void __wrap_hcd_port_reset(uint8_t rhport) { (void)rhport; bus_reset_start(); }
void __wrap_hcd_port_reset_end(uint8_t rhport) { (void)rhport; bus_reset_end(); }

/* Windows, macOS and Linux reset a new device a second time between the
 * first device-descriptor read and Set Address; TinyUSB has that step but
 * switched off (ENUM_RESET_2). This printer stalls Set Address without it.
 * Every request goes through hcd_setup_send(): reset before Set Address to
 * address 0. */
bool __real_hcd_setup_send(uint8_t rhport, uint8_t daddr, uint8_t const setup[8]);
bool __wrap_hcd_setup_send(uint8_t rhport, uint8_t daddr, uint8_t const setup[8]) {
    if (daddr == 0 && setup[0] == 0x00 && setup[1] == 0x05) {
        bus_reset_start();
        busy_wait_ms(20);
        bus_reset_end();
    }
    return __real_hcd_setup_send(rhport, daddr, setup);
}

static volatile uint8_t mounted_addr;
static volatile bool unmounted;
void tuh_mount_cb(uint8_t daddr) { mounted_addr = daddr; }
void tuh_umount_cb(uint8_t daddr) { (void)daddr; unmounted = true; }

static volatile uint32_t loops;
static uint32_t t_start;
/* Core 1 only draws (the panel driver sleeps between writes, so not from
 * an interrupt): the log and core 0's loop count, which stops if the USB
 * loop hangs. */
static void draw_core1(void) {
    for (;;) {
        char s[48];
        snprintf(s, sizeof s, "-- %lus  loop %lu", (unsigned long)((time_us_32() - t_start) / 1000000),
                 (unsigned long)loops);
        log_draw(s);
        sleep_ms(500);
    }
}

static volatile bool xfer_done;
static volatile xfer_result_t xfer_res;
static void xfer_cb(tuh_xfer_t *x) { xfer_res = x->result; xfer_done = true; }

static bool send(uint8_t daddr, uint8_t ep, const uint8_t *buf, uint32_t len) {
    static uint8_t chunk[512] __attribute__((aligned(4)));
    while (len) {
        uint16_t n = len > sizeof chunk ? sizeof chunk : (uint16_t)len;
        memcpy(chunk, buf, n);
        tuh_xfer_t x = { .daddr = daddr, .ep_addr = ep, .buflen = n, .buffer = chunk,
                         .complete_cb = xfer_cb };
        xfer_done = false;
        if (!tuh_edpt_xfer(&x)) return false;
        absolute_time_t until = make_timeout_time_ms(5000);
        while (!xfer_done) {
            tuh_task_ext(10, false);
            if (time_reached(until)) return false;
        }
        if (xfer_res != XFER_RESULT_SUCCESS) return false;
        buf += n;
        len -= n;
    }
    return true;
}

static void print_test(uint8_t daddr) {
    char s[48];
    uint16_t vid, pid;
    tuh_vid_pid_get(daddr, &vid, &pid);
    snprintf(s, sizeof s, "Device %04X:%04X", vid, pid);
    say(s);

    static uint8_t cfg[256] __attribute__((aligned(4)));
    if (tuh_descriptor_get_configuration_sync(daddr, 0, cfg, sizeof cfg) != XFER_RESULT_SUCCESS) {
        say("Config read FAILED");
        return;
    }
    uint16_t total = (uint16_t)(cfg[2] | cfg[3] << 8);
    if (total > sizeof cfg) total = sizeof cfg;

    /* The printer-class interface (7) and its bulk OUT endpoint. */
    const tusb_desc_endpoint_t *out = NULL;
    bool in_printer = false;
    for (uint16_t i = 0; i + 2 <= total && cfg[i]; i += cfg[i]) {
        if (cfg[i + 1] == TUSB_DESC_INTERFACE) {
            in_printer = cfg[i + 5] == 7;
        } else if (cfg[i + 1] == TUSB_DESC_ENDPOINT && in_printer && !out) {
            const tusb_desc_endpoint_t *ep = (const void *)&cfg[i];
            if (ep->bmAttributes.xfer == TUSB_XFER_BULK && !(ep->bEndpointAddress & 0x80))
                out = ep;
        }
    }
    if (!out) {
        say("No printer interface");
        return;
    }
    snprintf(s, sizeof s, "Printer, OUT ep %02X", out->bEndpointAddress);
    say(s);
    if (!tuh_edpt_open(daddr, out)) {
        say("Endpoint open FAILED");
        return;
    }

    static const uint8_t job[] =
        "\x1b@"
        "Printed by the PX-42F\n"
        "over USB, as host\n"
        "\x1b" "E\x01" "Bold line\n" "\x1b" "E\x00"
        "\x1d!\x11" "Big 2x\n" "\x1d!\x00"
        "1.234567890E-12   SQRT  ENTER\n"
        "\x1b" "d\x04";
    say(send(daddr, out->bEndpointAddress, job, sizeof job - 1) ? "Sent. Printed?" : "Send FAILED");
}

int main(void) {
    /* Speaker MOSFET gate (GP15) and IR LED (GP14) held off, as the firmware does first. */
    for (uint pin = 14; pin <= 15; pin++) {
        gpio_init(pin);
        gpio_put(pin, 0);
        gpio_set_dir(pin, GPIO_OUT);
    }
    display_init();
    display_clear();
    say("PX-42F USB host test");
    say("Plug the printer in");
    say("(OTG adapter/cable)");

    t_start = time_us_32();
    multicore_launch_core1(draw_core1);
    tuh_init(0);
    for (;;) {
        loops++;
        tuh_task_ext(50, false);
        if (mounted_addr) {
            uint8_t a = mounted_addr;
            mounted_addr = 0;
            say("Connected!");
            print_test(a);
        }
        if (unmounted) {
            unmounted = false;
            say("Disconnected");
        }
    }
}
