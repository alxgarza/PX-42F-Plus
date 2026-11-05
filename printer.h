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
 * R/S stops it (free42_print_stop()). */

typedef enum {
    PRINTER_OFF = 0,
    PRINTER_USB_SERIAL = 1,
    PRINTER_HP82240B = 2,
} printer_target_t;

/* PRLCD on the HP 82240B (px_settings.screen_print): the screen redrawn
 * upright for the paper (free42_draw_paper()), or the panel's own dots,
 * turned a quarter -- also when the screen is too wide to redraw. */
typedef enum { SCREEN_PRINT_UPRIGHT, SCREEN_PRINT_AS_IS, SCREEN_PRINTS } screen_print_t;

/* Prints to the HP 82240B (core 1) a text line, text and bits from
 * Plus42's print_text(); the screen as it is in the framebuffer (PRLCD:
 * the status bar, the PX's fonts and dividers, not Plus42's own 6x8
 * display), turned a quarter to fit the paper; or the framebuffer's top
 * height rows upright, as free42_draw_paper() left them. Each returns when
 * it's been sent, after waiting for room in the printer's buffer, or for a
 * picture, once stop() says to, at the end of a line. */
void printer_hp82240b_text(const char *text, int length, const uint8_t *bits, int bytesperline, bool double_wide);
void printer_hp82240b_screen(bool (*stop)(void));
void printer_hp82240b_paper(int height, bool (*stop)(void));

/* False while the HP 82240B's buffer has no room for a text line: sending
 * one now would wait for it, up to 1.8 s (core 1). */
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
