#include "usbprint.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/irq.h"
#include "hardware/structs/usb.h"
#include "hardware/watchdog.h"
#include "tusb.h"
#include "escpos.h"
#include "printer.h"
#include "settings.h"
#include "speaker.h"

/* Plugged in and on, the printer is found in about a second (the
 * debouncing TinyUSB waits after it appears is half of it). */
#define CONNECT_MS 5000
/* No line for this long: the paper fed up past the tear bar, once. */
#define FEED_MS 1500
/* No line for this long: the port is the computer's again (the user's
 * choice: host only while printing). */
#define IDLE_MS 30000
/* After the printer wasn't found or went away, prints are dropped this
 * long, not each waiting CONNECT_MS for it. */
#define UNAVAILABLE_MS 10000

typedef enum {
    ROLE_DEVICE,     /* the computer's: console, drive (pico_stdio_usb) */
    ROLE_CONNECTING, /* host, waiting for the printer and its descriptors */
    ROLE_READY,      /* host, the printer's bulk OUT endpoint open */
} role_t;

static volatile role_t role = ROLE_DEVICE;
static volatile bool release_requested; /* core 1 */
static volatile uint64_t unavailable_until_us;

static uint8_t printer_addr, out_ep;
static volatile bool gone, xfer_busy, xfer_failed;
static uint32_t xfer_from_ring; /* of the bytes in flight, those to take from the ring after */
static uint64_t since_us;       /* ROLE_CONNECTING: when it began; ROLE_READY: the last line */
static bool fed = true;         /* nothing printed since the last feed */
static bool greeted;            /* ESCPOS_INIT sent since connecting */
static repeating_timer_t tick;
static uint8_t tx[1024];
static uint8_t config[256];

static uint32_t connects, failures, bytes_sent;

/* ------------------------------------------------------------------ *
 * The bus resets this printer needs (tools/usb_printer_test, 2026-11):
 * TinyUSB's RP2 host driver never resets the bus -- its hcd_port_reset()
 * is empty, and the controller's RESET_BUS bit lasts 2 us, USB wants
 * 10 ms -- and without one the printer stalled Set Address. So both lines
 * are driven low through the PHY's override instead, linked in place of
 * the driver's (-Wl,--wrap). The SE0 looks to the controller like an
 * unplug: the connect interrupt is masked from the reset to its end, and
 * its flag cleared if the printer is back -- it took longer than 20 ms. */
static void bus_reset_start(void) {
    hw_clear_bits(&usb_hw->inte, USB_INTE_HOST_CONN_DIS_BITS);
    usb_hw->phy_direct = USB_USBPHY_DIRECT_TX_DP_OE_BITS | USB_USBPHY_DIRECT_TX_DM_OE_BITS; /* both low */
    usb_hw->phy_direct_override = USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS |
                                  USB_USBPHY_DIRECT_OVERRIDE_TX_DIFFMODE_OVERRIDE_EN_BITS;
}

static void bus_reset_end(void) {
    usb_hw->phy_direct_override = 0;
    usb_hw->phy_direct = 0;
    uint64_t t0 = time_us_64();
    uint32_t speed;
    while (!(speed = usb_hw->sie_status & USB_SIE_STATUS_SPEED_BITS) && time_us_64() - t0 < 500000)
        tight_loop_contents();
    busy_wait_ms(10); /* reset recovery */
    if (speed)
        hw_clear_alias(usb_hw)->sie_status = USB_SIE_STATUS_SPEED_BITS;
    hw_set_bits(&usb_hw->inte, USB_INTE_HOST_CONN_DIS_BITS);
}

void __wrap_hcd_port_reset(uint8_t rhport) {
    (void) rhport;
    bus_reset_start(); /* TinyUSB waits 50 ms, then ends it */
}

void __wrap_hcd_port_reset_end(uint8_t rhport) {
    (void) rhport;
    bus_reset_end();
}

/* Windows and macOS reset a new device again between its first descriptor
 * and Set Address; TinyUSB's step for it is switched off. This printer
 * needs it: a reset before Set Address to address 0. */
bool __real_hcd_setup_send(uint8_t rhport, uint8_t daddr, uint8_t const setup[8]);
bool __wrap_hcd_setup_send(uint8_t rhport, uint8_t daddr, uint8_t const setup[8]) {
    if (daddr == 0 && setup[0] == 0x00 && setup[1] == TUSB_REQ_SET_ADDRESS) {
        bus_reset_start();
        busy_wait_ms(20);
        bus_reset_end();
    }
    return __real_hcd_setup_send(rhport, daddr, setup);
}

/* ------------------------------------------------------------------ *
 * The printer: its configuration read, its printer-class interface's
 * bulk OUT endpoint opened. */

static void config_read(tuh_xfer_t *x) {
    if (x->result != XFER_RESULT_SUCCESS) {
        gone = true;
        return;
    }
    uint16_t total = (uint16_t) (config[2] | config[3] << 8);
    if (total > sizeof config)
        total = sizeof config;
    bool printer_class = false;
    for (uint16_t i = 0; i + 2 <= total && config[i] != 0; i += config[i]) {
        if (config[i + 1] == TUSB_DESC_INTERFACE) {
            printer_class = config[i + 5] == TUSB_CLASS_PRINTER;
        } else if (config[i + 1] == TUSB_DESC_ENDPOINT && printer_class && out_ep == 0) {
            const tusb_desc_endpoint_t *ep = (const tusb_desc_endpoint_t *) &config[i];
            if (ep->bmAttributes.xfer == TUSB_XFER_BULK && tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_OUT &&
                tuh_edpt_open(x->daddr, ep))
                out_ep = ep->bEndpointAddress;
        }
    }
    if (out_ep == 0) {
        gone = true; /* not a printer */
        return;
    }
    printer_addr = x->daddr;
    role = ROLE_READY;
}

void tuh_mount_cb(uint8_t daddr) {
    if (!tuh_descriptor_get_configuration(daddr, 0, config, sizeof config, config_read, 0))
        gone = true;
}

void tuh_umount_cb(uint8_t daddr) {
    (void) daddr;
    gone = true;
}

static void sent(tuh_xfer_t *x) {
    if (x->result == XFER_RESULT_SUCCESS) {
        printer_ring_take(xfer_from_ring);
        bytes_sent += x->actual_len;
    } else {
        xfer_failed = true;
    }
    xfer_busy = false;
}

static void send(const uint8_t *data, uint32_t n, bool from_ring) {
    memcpy(tx, data, n);
    xfer_from_ring = from_ring ? n : 0;
    tuh_xfer_t x = {.daddr = printer_addr, .ep_addr = out_ep, .buflen = n, .buffer = tx, .complete_cb = sent};
    xfer_busy = true;
    if (!tuh_edpt_xfer(&x)) {
        xfer_busy = false;
        xfer_failed = true;
    }
}

/* ------------------------------------------------------------------ *
 * The roles. */

void usbprint_device_started(void) {
    for (uint irq = FIRST_USER_IRQ; irq < FIRST_USER_IRQ + NUM_USER_IRQS; irq++)
        if (user_irq_is_claimed(irq)) {
            irq_set_priority(irq, PICO_LOWEST_IRQ_PRIORITY);
            printer_set_usb_task_irq(irq); /* the pump holds it off while it runs */
        }
}

/* Core 0 sleeps in WFE between interrupts; the host's own come only with
 * traffic, so a tick keeps the timeouts and the feed going. */
static bool wake(repeating_timer_t *t) {
    (void) t;
    return true;
}

static void to_host(void) {
    stdio_usb_deinit();
    printer_addr = out_ep = 0;
    gone = xfer_busy = xfer_failed = greeted = false;
    fed = true;
    tuh_init(0);
    add_repeating_timer_ms(10, wake, NULL, &tick);
    since_us = time_us_64();
    role = ROLE_CONNECTING;
    connects++;
}

static void to_device(void) {
    cancel_repeating_timer(&tick);
    tuh_deinit(0);
    stdio_usb_init();
    usbprint_device_started();
    role = ROLE_DEVICE;
}

/* No printer, or it went away: what's waiting is dropped, and so are the
 * prints of the next UNAVAILABLE_MS. */
static void give_up(void) {
    printer_ring_drop();
    failures++;
    unavailable_until_us = time_us_64() + UNAVAILABLE_MS * 1000ull;
    to_device();
}

bool usbprint_unavailable(void) {
    return time_us_64() < unavailable_until_us;
}

void usbprint_poll(void) {
    bool wanted = px_settings.printer == PRINTER_ESCPOS;
    const uint8_t *data;
    if (role == ROLE_DEVICE) {
        release_requested = false;
        if (!wanted || printer_ring_peek(&data) == 0)
            return;
        if (usbprint_unavailable()) {
            printer_ring_drop();
            return;
        }
        /* Finding the printer holds this core about a second (TinyUSB's
         * waits), and it refills the speaker: not over a key's click. */
        if (speaker_busy())
            return;
        to_host();
        return;
    }

    tuh_task_ext(0, false);
    uint64_t now = time_us_64();
    if (!wanted || release_requested) {
        printer_ring_drop();
        to_device();
        return;
    }
    if (gone || xfer_failed) {
        give_up();
        return;
    }
    if (role == ROLE_CONNECTING) { /* config_read() makes it ROLE_READY */
        if (now - since_us > CONNECT_MS * 1000ull)
            give_up();
        return;
    }
    if (xfer_busy)
        return;
    if (!greeted) {
        send((const uint8_t *) ESCPOS_INIT, ESCPOS_INIT_LEN, false);
        greeted = true;
        since_us = now;
        return;
    }
    uint32_t n = printer_ring_peek(&data);
    if (n > 0) {
        send(data, n < sizeof tx ? n : sizeof tx, true);
        since_us = now;
        fed = false;
    } else if (!fed && now - since_us > FEED_MS * 1000ull) {
        send((const uint8_t *) ESCPOS_FEED, ESCPOS_FEED_LEN, false);
        fed = true;
    } else if (now - since_us > IDLE_MS * 1000ull) {
        to_device();
    }
}

void usbprint_shutdown(void) {
    if (role == ROLE_DEVICE) {
        stdio_usb_deinit();
    } else {
        cancel_repeating_timer(&tick);
        tuh_deinit(0);
    }
}

void usbprint_release(void) {
    if (role == ROLE_DEVICE)
        return;
    release_requested = true;
    absolute_time_t until = make_timeout_time_ms(2000);
    while (role != ROLE_DEVICE && !time_reached(until)) {
        watchdog_update();
        sleep_ms(1);
    }
}

void usbprint_report(void) {
    static const char *const names[] = {"device (computer)", "host, connecting", "host, printer ready"};
    printf("usb printer: %s, connects=%lu failures=%lu sent=%lu%s\n", names[role], (unsigned long) connects,
           (unsigned long) failures, (unsigned long) bytes_sent, usbprint_unavailable() ? " (unavailable)" : "");
}
