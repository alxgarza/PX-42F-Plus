#include "printer.h"

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "tusb.h"
#include "settings.h"

#define PRINTER_CDC 1 /* CDC interface number of the printer port (usb_descriptors.c) */

/* How long a print waits for room in the buffer before dropping output --
 * only when a terminal is reading slower than the calculator prints. */
#define FULL_WAIT_MS 500

/* Single-producer (core 1, Free42 printing), single-consumer (core 0, the
 * USB pump) ring. Free-running indices; size a power of two. TinyUSB is
 * only ever called on core 0 -- its task runs there, in pico_stdio_usb's
 * background IRQ, and calling it from both cores at once isn't safe. */
#define RING_SIZE 8192u
static uint8_t ring[RING_SIZE];
static volatile uint32_t ring_head; /* written by core 1 */
static volatile uint32_t ring_tail; /* written by core 0 */

static int usb_task_irq = -1;

/* Diagnostics for printer_report() (the console's !PRINTER command). */
static volatile uint32_t calls, bytes_queued, bytes_sent, bytes_dropped, pumps;

void printer_set_usb_task_irq(unsigned irq) {
    usb_task_irq = (int) irq;
}

void printer_write(const char *utf8, int length) {
    calls++;
    if (px_settings.printer != PRINTER_USB_SERIAL || !tud_cdc_n_connected(PRINTER_CDC)) {
        bytes_dropped += (uint32_t) length;
        return; /* nobody listening: drop it, never wait */
    }

    absolute_time_t give_up = make_timeout_time_ms(FULL_WAIT_MS);
    while (length > 0) {
        uint32_t head = ring_head;
        uint32_t room = RING_SIZE - (head - ring_tail);
        if (room == 0) {
            if (time_reached(give_up) || !tud_cdc_n_connected(PRINTER_CDC)) {
                bytes_dropped += (uint32_t) length;
                return;
            }
            watchdog_update();
            sleep_us(500);
            continue;
        }
        uint32_t n = (uint32_t) length < room ? (uint32_t) length : room;
        for (uint32_t i = 0; i < n; i++)
            ring[(head + i) % RING_SIZE] = (uint8_t) utf8[i];
        __dmb(); /* data before the index that publishes it */
        ring_head = head + n;
        bytes_queued += n;
        utf8 += n;
        length -= (int) n;
    }
}

void printer_usb_pump(void) {
    pumps++;
    if (ring_head == ring_tail)
        return;

    /* Keep this core's own TinyUSB task (pico_stdio_usb's background IRQ)
     * from running in the middle of these calls. */
    if (usb_task_irq >= 0)
        irq_set_enabled((uint) usb_task_irq, false);

    if (!tud_cdc_n_connected(PRINTER_CDC)) {
        bytes_dropped += ring_head - ring_tail;
        ring_tail = ring_head; /* the terminal went away: discard */
    } else {
        __dmb();
        uint32_t tail = ring_tail, head = ring_head;
        uint32_t avail = tud_cdc_n_write_available(PRINTER_CDC);
        while (avail > 0 && tail != head) {
            uint32_t chunk = head - tail;
            uint32_t to_wrap = RING_SIZE - tail % RING_SIZE;
            if (chunk > to_wrap)
                chunk = to_wrap;
            if (chunk > avail)
                chunk = avail;
            uint32_t n = tud_cdc_n_write(PRINTER_CDC, &ring[tail % RING_SIZE], chunk);
            if (n == 0)
                break;
            tail += n;
            avail -= n;
        }
        bytes_sent += tail - ring_tail;
        ring_tail = tail;
        tud_cdc_n_write_flush(PRINTER_CDC);
    }

    if (usb_task_irq >= 0)
        irq_set_enabled((uint) usb_task_irq, true);
}

void printer_report(void) {
    printf("printer: target=%u (1=USB serial) port_connected(DTR)=%d usb_task_irq=%d\n",
           (unsigned) px_settings.printer, (int) tud_cdc_n_connected(PRINTER_CDC), usb_task_irq);
    printf("printer: calls=%lu queued=%lu sent=%lu dropped=%lu buffered=%lu core0_pumps=%lu\n",
           (unsigned long) calls, (unsigned long) bytes_queued, (unsigned long) bytes_sent,
           (unsigned long) bytes_dropped, (unsigned long) (ring_head - ring_tail), (unsigned long) pumps);
}

/* Unicode quadrant blocks, indexed by upper-left | upper-right << 1 |
 * lower-left << 2 | lower-right << 3. */
static const char *const quadrant[16] = {
        " ", "▘", "▝", "▀", "▖", "▌", "▞", "▛",
        "▗", "▚", "▐", "▜", "▄", "▙", "▟", "█",
};

static int pixel(const uint8_t *bits, int bytesperline, int width, int height, int x, int y) {
    if (x >= width || y >= height)
        return 0;
    return (bits[y * bytesperline + (x >> 3)] >> (x & 7)) & 1;
}

void printer_write_bitmap(const uint8_t *bits, int bytesperline, int width, int height) {
    if (px_settings.printer != PRINTER_USB_SERIAL || !tud_cdc_n_connected(PRINTER_CDC))
        return;

    char line[3 * 128 + 2];
    for (int y = 0; y < height; y += 2) {
        int len = 0, used = 0; /* used: length without trailing spaces */
        for (int x = 0; x < width && len < (int) sizeof(line) - 5; x += 2) {
            int q = pixel(bits, bytesperline, width, height, x, y)
                    | pixel(bits, bytesperline, width, height, x + 1, y) << 1
                    | pixel(bits, bytesperline, width, height, x, y + 1) << 2
                    | pixel(bits, bytesperline, width, height, x + 1, y + 1) << 3;
            for (const char *c = quadrant[q]; *c; c++)
                line[len++] = *c;
            if (q != 0)
                used = len;
        }
        line[used++] = '\r';
        line[used++] = '\n';
        printer_write(line, used);
    }
}
