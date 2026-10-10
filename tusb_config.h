#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

/* TinyUSB configuration for the PX-42S's composite USB device: the serial
 * console (CDC 0, still run by pico_stdio_usb), the state-file drive (MSC,
 * usb_msc.c), the picotool reset interface (pico_usb_reset) and the
 * printer's serial port (CDC 1, printer.c). Descriptors
 * are in usb_descriptors.c. Because this app provides its own TinyUSB
 * configuration, CMakeLists.txt sets the PICO_STDIO_USB_* / PICO_*USB_RESET*
 * options that the SDK otherwise only enables for its own default
 * descriptors. */

#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE)
#define CFG_TUD_ENDPOINT0_SIZE  64

#define CFG_TUD_CDC             2 /* 0: serial console (stdio), 1: printer (printer.c) */
#define CFG_TUD_MSC             1
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0 /* the reset interface has its own driver */

#define CFG_TUD_CDC_RX_BUFSIZE  64
#define CFG_TUD_CDC_TX_BUFSIZE  64
#define CFG_TUD_CDC_EP_BUFSIZE  64

#define CFG_TUD_MSC_EP_BUFSIZE  512

#endif
