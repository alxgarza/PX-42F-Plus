#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#define CFG_TUSB_OS                 OPT_OS_PICO
#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_HOST
#define CFG_TUH_ENABLED             1
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB                 0
#define CFG_TUH_DEVICE_MAX          1
#define CFG_TUH_ENDPOINT_MAX        8
#define CFG_TUH_API_EDPT_XFER       1   /* tuh_edpt_xfer()'s callback: off, a finished bulk transfer reached no one */
#define CFG_TUSB_DEBUG              2
#define CFG_TUSB_DEBUG_PRINTF       log_printf
#define CFG_TUH_MEM_ALIGN           __attribute__((aligned(4)))

#endif
