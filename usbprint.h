#ifndef USBPRINT_H
#define USBPRINT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The USB thermal printer (Settings -> Printer -> USB Thermal): the
 * RP2350's one USB port taken from the computer (console, drive) and made
 * the printer's host only while printing. What core 1 prints waits in
 * printer.c's ring; the first bytes switch the port to host, the printer is
 * found, the bytes go out (ESC/POS, escpos.c), and 30 s after the last the
 * port is the computer's again. No 5 V is put on the port: the printer runs
 * on its own battery. Everything here but usbprint_release() runs on core
 * 0, which owns USB. */

/* After the USB device stack (pico_stdio_usb) has started, at boot and on
 * each return from host: its task interrupt to the lowest priority (main.c
 * says why) and told to printer.c. Core 0. */
void usbprint_device_started(void);

/* From core 0's loop: switches roles, runs the host and sends. */
void usbprint_poll(void);

/* Before a power-down: USB stopped in whichever role it's in. Core 0. */
void usbprint_shutdown(void);

/* The port back to the computer now (USB Drive's Connect to PC): waits up
 * to 2 s for core 0 to switch. Core 1. */
void usbprint_release(void);

/* True for a while after the printer couldn't be found or went away:
 * prints are dropped meanwhile, so the calculator never waits for it. */
bool usbprint_unavailable(void);

/* The state and counters to the console (!PRINTER). */
void usbprint_report(void);

#ifdef __cplusplus
}
#endif

#endif
