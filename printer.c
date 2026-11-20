#include "printer.h"

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "tusb.h"
#include "settings.h"
#include "hp82240.h"
#include "escpos.h"
#include "usbprint.h"
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

/* The status bar's printer icon (printer_busy()), shown this long after
 * USB output. */
#define USB_SHOWN_MS 500
static absolute_time_t print_shown_until;

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

/* Core 1's side of the ring for the USB thermal printer: waits for room
 * (core 0 is finding the printer, or it's printing) unless the printer is
 * unavailable or stop says so. False if not all of it went in. */
static bool escpos_put(const uint8_t *bytes, uint32_t length, bool (*stop)(void)) {
    calls++;
    while (length > 0) {
        if (usbprint_unavailable() || (stop != NULL && stop())) {
            bytes_dropped += length;
            return false;
        }
        uint32_t head = ring_head;
        uint32_t room = RING_SIZE - (head - ring_tail);
        if (room == 0) {
            watchdog_update();
            sleep_us(500);
            continue;
        }
        uint32_t n = length < room ? length : room;
        for (uint32_t i = 0; i < n; i++)
            ring[(head + i) % RING_SIZE] = bytes[i];
        __dmb(); /* data before the index that publishes it */
        ring_head = head + n;
        bytes_queued += n;
        bytes += n;
        length -= n;
    }
    print_shown_until = make_timeout_time_ms(USB_SHOWN_MS);
    return true;
}

static bool last_was_picture; /* the spacing after one differs (escpos.h) */

void printer_escpos_text(const char *text, int length, const uint8_t *bits, int bytesperline, bool double_wide) {
    uint8_t line[ESCPOS_LINE_MAX];
    int n = escpos_text_line(line, text, length, bits, bytesperline, double_wide, &last_was_picture);
    escpos_put(line, (uint32_t) n, NULL);
}

bool printer_escpos_screen(bool (*stop)(void)) {
    uint8_t bytes[ESCPOS_ROW_BYTES(DISPLAY_WIDTH)];
    if (!escpos_put(bytes, (uint32_t) escpos_picture_header(bytes, DISPLAY_WIDTH, DISPLAY_HEIGHT), stop))
        return false;
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        /* A picture cut short would take the next bytes as its dots: once
         * begun, it goes out whole. */
        int n = escpos_picture_row(bytes, display_fb[y], DISPLAY_WIDTH);
        if (!escpos_put(bytes, (uint32_t) n, NULL))
            return false;
    }
    last_was_picture = true;
    return true;
}

uint32_t printer_ring_peek(const uint8_t **data) {
    uint32_t tail = ring_tail;
    uint32_t n = ring_head - tail;
    __dmb(); /* the index before the data it publishes */
    uint32_t to_wrap = RING_SIZE - tail % RING_SIZE;
    *data = &ring[tail % RING_SIZE];
    return n < to_wrap ? n : to_wrap;
}

void printer_ring_take(uint32_t n) {
    bytes_sent += n;
    ring_tail += n;
}

void printer_ring_drop(void) {
    bytes_dropped += ring_head - ring_tail;
    ring_tail = ring_head;
}

void printer_usb_pump(void) {
    pumps++;
    /* The USB thermal printer's bytes are usbprint.c's, and while the port
     * is its host the device stack isn't running. */
    if (ring_head == ring_tail || px_settings.printer != PRINTER_USB_SERIAL || !tud_inited())
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
 * buffer only once it has printed; 190 leaves the margin it advises. The
 * time per line is DELAY's (px_settings.print_delay_ms, 1.8 s unless a
 * program asks for less): a faster printer, on fresh batteries or its
 * adapter, keeps up with less.
 * The fewest bytes a line takes is 3 (a blank graphics band), so 64 lines
 * always cover the buffer. */
#define IR_BUFFER_BYTES 190
#define IR_LINES 64u
#define IR_TEXT_LINE_BYTES 29 /* 2 escapes, 24 characters, the linefeed; more only with graphics characters */
static struct {
    uint16_t bytes;
    absolute_time_t printed;
} ir_line[IR_LINES];
static uint32_t ir_first, ir_end; /* free-running, like the ring's */
static int ir_buffered;
static uint32_t ir_lines_sent, ir_bytes_sent;

/* Takes out of the reckoning the lines printed by now. */
static void ir_forget_printed(void) {
    while (ir_first != ir_end && time_reached(ir_line[ir_first % IR_LINES].printed)) {
        ir_buffered -= ir_line[ir_first % IR_LINES].bytes;
        ir_first++;
    }
}

bool printer_ready(void) {
    if (px_settings.printer == PRINTER_ESCPOS)
        return usbprint_unavailable() || RING_SIZE - (ring_head - ring_tail) >= ESCPOS_LINE_MAX;
    ir_forget_printed();
    return ir_buffered + IR_TEXT_LINE_BYTES <= IR_BUFFER_BYTES;
}

#define IR_SHOWN_MS 250

bool printer_will_print(void) {
    if (px_settings.printer == PRINTER_HP82240B)
        return true;
    if (px_settings.printer == PRINTER_ESCPOS)
        return !usbprint_unavailable();
    if (px_settings.printer != PRINTER_USB_SERIAL || !tud_cdc_n_connected(PRINTER_CDC))
        return false;
    print_shown_until = make_timeout_time_ms(USB_SHOWN_MS);
    return true;
}

printer_target_t printer_target(void) {
    if (px_settings.printer == PRINTER_USB_SERIAL && !tud_cdc_n_connected(PRINTER_CDC))
        return PRINTER_OFF;
    return (printer_target_t) px_settings.printer;
}

bool printer_busy(void) {
    return !time_reached(print_shown_until);
}

/* False if stop (it may be NULL) said so while waiting for room, nothing sent.
 * A line that has begun always goes out whole: cut short, the printer would take
 * whatever came next for the rest of its graphics (HP's guide). stop is still
 * asked during it, so a tap in those 2 s isn't missed. */
static bool ir_send_line(const uint8_t *bytes, int n, bool (*stop)(void)) {
    for (;;) {
        ir_forget_printed();
        if (ir_buffered + n <= IR_BUFFER_BYTES && ir_end - ir_first < IR_LINES)
            break;
        if (stop != NULL && stop())
            return false;
        watchdog_update();
        tight_loop_contents();
    }
    /* Not under a sound: PRLCD's first line, sent during the key's click
     * (the speaker's ~200 mA peaks), lost its graphics count, and its dots
     * printed as characters. */
    sounds_wait();
    irtx_send_redeye(bytes, n, stop);

    /* It prints after the lines before it. */
    absolute_time_t start = get_absolute_time();
    if (ir_first != ir_end) {
        absolute_time_t before = ir_line[(ir_end - 1) % IR_LINES].printed;
        if (absolute_time_diff_us(start, before) > 0)
            start = before;
    }
    ir_line[ir_end % IR_LINES].bytes = (uint16_t) n;
    ir_line[ir_end % IR_LINES].printed = delayed_by_ms(start, px_settings.print_delay_ms);
    ir_end++;
    ir_buffered += n;
    ir_lines_sent++;
    ir_bytes_sent += (uint32_t) n;
    print_shown_until = make_timeout_time_ms(IR_SHOWN_MS);
    return true;
}

void printer_hp82240b_text(const char *text, int length, const uint8_t *bits, int bytesperline, bool double_wide) {
    uint8_t line[HP82240_LINE_MAX];
    ir_send_line(line, hp82240_text_line(line, text, length, bits, bytesperline, double_wide), NULL);
}

bool printer_hp82240b_screen(bool (*stop)(void)) {
    uint8_t line[HP82240_LINE_MAX];
    for (int band = 0; band < (DISPLAY_WIDTH + 7) / 8; band++) {
        int n = hp82240_screen_line(line, &display_fb[0][0], DISPLAY_STRIDE, DISPLAY_WIDTH, DISPLAY_HEIGHT, band);
        if (!ir_send_line(line, n, stop) || stop())
            return false;
    }
    return true;
}

void printer_hp82240b_paper(int height, bool (*stop)(void)) {
    uint8_t line[HP82240_LINE_MAX];
    for (int band = 0; band < (height + 7) / 8; band++) {
        int n = hp82240_upright_line(line, &display_fb[0][0], DISPLAY_STRIDE, DISPLAY_WIDTH, height, band);
        if (!ir_send_line(line, n, stop) || stop())
            return;
    }
}

void printer_report(void) {
    printf("printer: target=%u (1=USB serial, 2=HP 82240B, 3=USB thermal) port_connected(DTR)=%d usb_task_irq=%d\n",
           (unsigned) px_settings.printer, (int) tud_cdc_n_connected(PRINTER_CDC), usb_task_irq);
    printf("printer: calls=%lu queued=%lu sent=%lu dropped=%lu buffered=%lu core0_pumps=%lu\n",
           (unsigned long) calls, (unsigned long) bytes_queued, (unsigned long) bytes_sent,
           (unsigned long) bytes_dropped, (unsigned long) (ring_head - ring_tail), (unsigned long) pumps);
    printf("printer: HP 82240B lines=%lu bytes=%lu buffer_after_last_line=%d\n", (unsigned long) ir_lines_sent,
           (unsigned long) ir_bytes_sent, ir_buffered);
    usbprint_report();
}

/* Unicode quadrant blocks, indexed by upper-left | upper-right << 1 |
 * lower-left << 2 | lower-right << 3. */
static const char *const quadrant[16] = {
        " ", "▘", "▝", "▀", "▖", "▌", "▞", "▛",
        "▗", "▚", "▐", "▜", "▄", "▙", "▟", "█",
};

/* A picture's pixels: Free42's bitmaps put the leftmost in the least
 * significant bit, the framebuffer in the most. */
typedef struct {
    const uint8_t *bits;
    int bytesperline, width, height;
    bool msb_first;
} picture_t;

static int pixel(const picture_t *p, int x, int y) {
    if (x >= p->width || y >= p->height)
        return 0;
    uint8_t byte = p->bits[y * p->bytesperline + (x >> 3)];
    return (p->msb_first ? byte >> (7 - (x & 7)) : byte >> (x & 7)) & 1;
}

static void write_blocks(const picture_t *p) {
    if (px_settings.printer != PRINTER_USB_SERIAL || !tud_cdc_n_connected(PRINTER_CDC))
        return;

    char line[3 * 128 + 2];
    for (int y = 0; y < p->height; y += 2) {
        int len = 0, used = 0; /* used: length without trailing spaces */
        for (int x = 0; x < p->width && len < (int) sizeof(line) - 5; x += 2) {
            int q = pixel(p, x, y) | pixel(p, x + 1, y) << 1 | pixel(p, x, y + 1) << 2 | pixel(p, x + 1, y + 1) << 3;
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

void printer_write_bitmap(const uint8_t *bits, int bytesperline, int width, int height) {
    picture_t p = {bits, bytesperline, width, height, false};
    write_blocks(&p);
}

void printer_write_screen(void) {
    picture_t p = {&display_fb[0][0], DISPLAY_STRIDE, DISPLAY_WIDTH, DISPLAY_HEIGHT, true};
    write_blocks(&p);
}
