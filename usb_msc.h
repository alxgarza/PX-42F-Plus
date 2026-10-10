#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The state-file drive, as seen by a computer over USB. Like the DM42's USB
 * disk mode, the drive only has a disk in it while the calculator is in
 * USB Disk mode (settings menu > Communications) -- the computer and the
 * calculator's own FatFs must never write the volume at the same time. */

/* Insert the disk: the computer sees the drive appear. The caller has
 * unmounted FatFs and flushed the cache first. */
void usb_msc_attach(void);

/* Remove the disk: the computer sees the media go away. */
void usb_msc_detach(void);

/* True once the computer has ejected the drive (since usb_msc_attach()). */
bool usb_msc_ejected(void);

/* True (once) after the computer asked for its writes to be committed
 * (SCSI SYNCHRONIZE CACHE): flush now rather than after an idle delay. */
bool usb_msc_take_sync_request(void);

#ifdef __cplusplus
}
#endif

#endif
