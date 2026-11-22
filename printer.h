#ifndef PRINTER_H
#define PRINTER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Printer emulation: where Free42's printer output (shell_print() in
 * shell.cc -- PRX, PRA, PRSTK, PRP, PRLCD, TRACE/NORM printing, ...) goes,
 * chosen by px_settings.printer (the settings menu's Printer item).
 *
 * USB serial: a second USB serial port, "PX-42F Printer", separate from the
 * debug console -- open it in any terminal for a running paper roll. Text
 * arrives as UTF-8 (HP characters like the divide sign or Sigma translated),
 * one printed line per line; graphics (PRLCD) as Unicode block art. Output
 * is dropped while no terminal has the port open, so printing never stalls
 * the calculator.
 *
 * HP 82240B: the HP infrared printer, by the IR LED (irtx.c). Text goes as
 * the printer's own characters, those it lacks and the screen (PRLCD) as
 * dot columns (hp82240.c), at the printer's pace: it can't answer, so this side
 * keeps its 200-byte buffer from overflowing. What's printing waits for
 * room in free42_service() (printer_ready()), reading the keys meanwhile;
 * PRLCD, a single print, holds the calculator up until it's sent or EXIT or
 * R/S stops it (free42_print_stop()).
 *
 * USB thermal: a 58 mm ESC/POS receipt printer on the USB port, the PX its
 * host while printing (usbprint.c). Text in the printer's own font, the
 * characters it lacks drawn from Plus42's shapes, the screen as a picture
 * (escpos.c); it all waits in the ring below for core 0 to send. */

typedef enum {
    PRINTER_OFF = 0,
    PRINTER_USB_SERIAL = 1,
    PRINTER_HP82240B = 2,
    PRINTER_ESCPOS = 3,
} printer_target_t;

/* PRLCD (px_settings.screen_print), on the HP 82240B: the screen redrawn
 * upright for the paper (free42_draw_paper()), or the panel's own dots,
 * turned a quarter -- also when the screen is too wide to redraw. On the
 * USB ESC/POS printer: the panel's dots upright (they fit), or turned a
 * quarter and 3 times bigger. */
typedef enum { SCREEN_PRINT_UPRIGHT, SCREEN_PRINT_AS_IS, SCREEN_PRINTS } screen_print_t;

/* DELAY (px_settings.print_delay_ms): the time the HP 82240B is reckoned to
 * take per line. 1.8 s is the HP-42S's own default, and HP's interfacing
 * guide's slowest printer (batteries nearly flat); DELAY takes 0 to 1.9 s. */
#define PRINT_DELAY_DEFAULT_MS 1800
#define PRINT_DELAY_MAX_MS     1949

/* Prints to the HP 82240B (core 1) a text line, text and bits from
 * Plus42's print_text(); the screen as it is in the framebuffer (PRLCD:
 * the status bar, the PX's fonts and dividers, not Plus42's own 6x8
 * display), turned a quarter to fit the paper; or the framebuffer's top
 * height rows upright, as free42_draw_paper() left them. Each returns when
 * it's been sent, after waiting for room in the printer's buffer, or for a
 * picture, once stop() says to, at the end of a line. */
void printer_hp82240b_text(const char *text, int length, const uint8_t *bits, int bytesperline, bool double_wide);
bool printer_hp82240b_screen(bool (*stop)(void)); /* false: stopped */
void printer_hp82240b_paper(int height, bool (*stop)(void));

/* To the USB thermal printer (core 1): a text line, as for the HP 82240B;
 * the screen as it is in the framebuffer, upright. Each waits for room in
 * the ring, unless the printer is unavailable (dropped). The screen returns
 * false if stop said so between rows. */
void printer_escpos_text(const char *text, int length, const uint8_t *bits, int bytesperline, bool double_wide);
bool printer_escpos_screen(bool turned, bool (*stop)(void));

/* Core 0 (usbprint.c): the bytes waiting, from the oldest, as many as lie
 * together (0: none); taking n of them once sent; dropping them all. */
uint32_t printer_ring_peek(const uint8_t **data);
void printer_ring_take(uint32_t n);
void printer_ring_drop(void);

/* Where a print goes now: PRINTER_OFF too when the USB port has no
 * terminal open (its output would be dropped). */
printer_target_t printer_target(void);

/* The screen to the USB port, as Unicode blocks (an app's print_screen()):
 * display_fb as it is, 2 x 2 pixels a character (core 1). */
void printer_write_screen(void);

/* False while the HP 82240B's buffer has no room for a text line: sending
 * one now would wait for it, up to 1.8 s -- or the USB thermal printer's
 * ring has none, while it's being found or is busy (core 1). */
bool printer_ready(void);

/* The status bar's printer icon. printer_will_print(), before a print: true if
 * it goes anywhere (USB with no terminal open is dropped). printer_busy(): a
 * moment after the last line went out, so a program's lines don't blink it, and
 * USB's output, gone at once, can be seen. Not until the paper has it: the icon
 * is about sending, and the reckoning's 1.8 s a line is the slowest printer's. */
bool printer_will_print(void);
bool printer_busy(void);

/* Text output, already UTF-8 (core 1). */
void printer_write(const char *utf8, int length);

/* A graphics print: width x height pixels, rows of bytesperline bytes,
 * least significant bit first -- Free42's own layout. Rendered as Unicode
 * quadrant blocks, 2 x 2 pixels per character (core 1). */
void printer_write_bitmap(const uint8_t *bits, int bytesperline, int width, int height);

/* Core 0: the USB task's IRQ number (see main()), and the pump that moves
 * buffered output to the USB port -- call it from core 0's idle loop. */
void printer_set_usb_task_irq(unsigned irq);
void printer_usb_pump(void);

/* Prints the printer's state and counters to the serial console (the
 * console's !PRINTER command). */
void printer_report(void);

#ifdef __cplusplus
}
#endif

#endif
