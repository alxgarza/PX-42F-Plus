/* USB descriptors for the PX-42F composite device. Based on the Pico SDK's
 * default stdio descriptors (src/rp2_common/pico_stdio_usb/
 * stdio_usb_descriptors.c), plus a mass-storage interface for the
 * state-file drive:
 *
 *   interface 0-1  CDC serial console (pico_stdio_usb)
 *   interface 2    MSC state-file drive (usb_msc.c)
 *   interface 3    picotool reset (pico_usb_reset) -- must match
 *                  PICO_USB_RESET_MS_OS_20_DESCRIPTOR_ITF in CMakeLists.txt
 *   interface 4-5  CDC printer port (printer.c)
 */
#include "tusb.h"
#include "pico/unique_id.h"
#include "pico/usb_reset.h"

#define USBD_VID (0x2E8A) /* Raspberry Pi */
#define USBD_PID (0x0009) /* Raspberry Pi Pico SDK CDC (RP2350) */

#define USBD_ITF_CDC       0 /* needs 2 interfaces */
#define USBD_ITF_MSC       2
#define USBD_ITF_RPI_RESET 3
#define USBD_ITF_PRINTER   4 /* needs 2 interfaces */
#define USBD_ITF_MAX       6
_Static_assert(USBD_ITF_RPI_RESET == PICO_USB_RESET_MS_OS_20_DESCRIPTOR_ITF,
               "reset interface number must match PICO_USB_RESET_MS_OS_20_DESCRIPTOR_ITF");

#define USBD_CDC_EP_CMD 0x81
#define USBD_CDC_EP_OUT 0x02
#define USBD_CDC_EP_IN  0x82
#define USBD_MSC_EP_OUT 0x03
#define USBD_MSC_EP_IN  0x83
#define USBD_PRN_EP_CMD 0x84
#define USBD_PRN_EP_OUT 0x05
#define USBD_PRN_EP_IN  0x85

#define USBD_DESC_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN + TUD_RPI_RESET_DESC_LEN \
                       + TUD_CDC_DESC_LEN)

enum {
    USBD_STR_0,
    USBD_STR_MANUF,
    USBD_STR_PRODUCT,
    USBD_STR_SERIAL,
    USBD_STR_CDC,
    USBD_STR_MSC,
    USBD_STR_RPI_RESET,
    USBD_STR_PRINTER,
};

static const tusb_desc_device_t usbd_desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210, /* 2.1: the reset interface's Microsoft OS 2.0 descriptor needs it */
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USBD_VID,
    .idProduct = USBD_PID,
    /* Bumped from the SDK's 0x0100: hosts (Windows especially) cache a
     * device's interface layout by VID/PID/bcdDevice, and this layout is
     * different from the plain serial-only one. */
    .bcdDevice = 0x0201, /* 0x0201: printer port added */
    .iManufacturer = USBD_STR_MANUF,
    .iProduct = USBD_STR_PRODUCT,
    .iSerialNumber = USBD_STR_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t usbd_desc_cfg[USBD_DESC_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, USBD_ITF_MAX, USBD_STR_0, USBD_DESC_LEN, 0, 250),
    TUD_CDC_DESCRIPTOR(USBD_ITF_CDC, USBD_STR_CDC, USBD_CDC_EP_CMD, 8, USBD_CDC_EP_OUT, USBD_CDC_EP_IN, 64),
    TUD_MSC_DESCRIPTOR(USBD_ITF_MSC, USBD_STR_MSC, USBD_MSC_EP_OUT, USBD_MSC_EP_IN, 64),
    TUD_RPI_RESET_DESCRIPTOR(USBD_ITF_RPI_RESET, USBD_STR_RPI_RESET),
    TUD_CDC_DESCRIPTOR(USBD_ITF_PRINTER, USBD_STR_PRINTER, USBD_PRN_EP_CMD, 8, USBD_PRN_EP_OUT, USBD_PRN_EP_IN, 64),
};

static char usbd_serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

static const char *const usbd_desc_str[] = {
    [USBD_STR_MANUF] = "PX-42",
    [USBD_STR_PRODUCT] = "PX-42F",
    [USBD_STR_SERIAL] = usbd_serial_str,
    [USBD_STR_CDC] = "PX-42F Console",
    [USBD_STR_MSC] = "PX-42F State Disk",
    [USBD_STR_RPI_RESET] = "Reset",
    [USBD_STR_PRINTER] = "PX-42F Printer",
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *) &usbd_desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return usbd_desc_cfg;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    static uint16_t desc_str[32];

    if (!usbd_serial_str[0])
        pico_get_unique_board_id_string(usbd_serial_str, sizeof(usbd_serial_str));

    uint8_t len;
    if (index == 0) {
        desc_str[1] = 0x0409; /* English */
        len = 1;
    } else {
        if (index >= sizeof(usbd_desc_str) / sizeof(usbd_desc_str[0]))
            return NULL;
        const char *str = usbd_desc_str[index];
        for (len = 0; len < 31 && str[len]; ++len)
            desc_str[1 + len] = (uint16_t) str[len];
    }
    desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc_str;
}
