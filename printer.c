#include "printer.h"

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "tusb.h"
#include "settings.h"
#include "hp82240.h"
#include "irtx.h"
#include "display_fb.h"
#include "sounds.h"

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

/* The HP 82240B's buffer as this side reckons it, the printer having no
 * way to say: the lines sent, each one's bytes and when it will have
 * printed. HP's interfacing guide: a line takes up to 1.8 s on batteries
 * (the slowest printer, nearly flat), and its bytes leave the 200-byte
 * buffer only once it has printed; 190 leaves the margin it advises.
 * The fewest bytes a line takes is 3 (a blank graphics band), so 64 lines
 * always cover the buffer. */
#define IR_BUFFER_BYTES 190
#define IR_LINE_MS 1800
#define IR_LINES 64u
static struct {
    uint16_t bytes;
    absolute_time_t printed;
} ir_line[IR_LINES];
static uint32_t ir_first, ir_end; /* free-running, like the ring's */
static int ir_buffered;
static uint32_t ir_lines_sent, ir_bytes_sent;

static void ir_send_line(const uint8_t *bytes, int n) {
    for (;;) {
        while (ir_first != ir_end && time_reached(ir_line[ir_first % IR_LINES].printed)) {
            ir_buffered -= ir_line[ir_first % IR_LINES].bytes;
            ir_first++;
        }
        if (ir_buffered + n <= IR_BUFFER_BYTES && ir_end - ir_first < IR_LINES)
            break;
        watchdog_update();
        tight_loop_contents();
    }
    /* Not under a sound: PRLCD's first line, sent during the key's click
     * (the speaker's ~200 mA peaks), lost its graphics count, and its dots
     * printed as characters. */
    sounds_wait();
    irtx_send_redeye(bytes, n);

    /* It prints after the lines before it. */
    absolute_time_t start = get_absolute_time();
    if (ir_first != ir_end) {
        absolute_time_t before = ir_line[(ir_end - 1) % IR_LINES].printed;
        if (absolute_time_diff_us(start, before) > 0)
            start = before;
    }
    ir_line[ir_end % IR_LINES].bytes = (uint16_t) n;
    ir_line[ir_end % IR_LINES].printed = delayed_by_ms(start, IR_LINE_MS);
    ir_end++;
    ir_buffered += n;
    ir_lines_sent++;
    ir_bytes_sent += (uint32_t) n;
}

void printer_hp82240b_text(const char *text, int length, const uint8_t *bits, int bytesperline, bool double_wide) {
    uint8_t line[HP82240_LINE_MAX];
    ir_send_line(line, hp82240_text_line(line, text, length, bits, bytesperline, double_wide));
}

void printer_hp82240b_screen(void) {
    uint8_t line[HP82240_LINE_MAX];
    for (int band = 0; band < (DISPLAY_WIDTH + 7) / 8; band++)
        ir_send_line(line, hp82240_screen_line(line, &display_fb[0][0], DISPLAY_STRIDE, DISPLAY_WIDTH,
                                               DISPLAY_HEIGHT, band));
}

void printer_hp82240b_paper(int height) {
    uint8_t line[HP82240_LINE_MAX];
    for (int band = 0; band < (height + 7) / 8; band++)
        ir_send_line(line, hp82240_upright_line(line, &display_fb[0][0], DISPLAY_STRIDE, DISPLAY_WIDTH, height, band));
}

void printer_report(void) {
    printf("printer: target=%u (1=USB serial, 2=HP 82240B) port_connected(DTR)=%d usb_task_irq=%d\n",
           (unsigned) px_settings.printer, (int) tud_cdc_n_connected(PRINTER_CDC), usb_task_irq);
    printf("printer: calls=%lu queued=%lu sent=%lu dropped=%lu buffered=%lu core0_pumps=%lu\n",
           (unsigned long) calls, (unsigned long) bytes_queued, (unsigned long) bytes_sent,
           (unsigned long) bytes_dropped, (unsigned long) (ring_head - ring_tail), (unsigned long) pumps);
    printf("printer: HP 82240B lines=%lu bytes=%lu buffer_after_last_line=%d\n", (unsigned long) ir_lines_sent,
           (unsigned long) ir_bytes_sent, ir_buffered);
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
