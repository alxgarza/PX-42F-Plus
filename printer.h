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
 * the calculator. */

typedef enum {
    PRINTER_OFF = 0,
    PRINTER_USB_SERIAL = 1,
} printer_target_t;

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
