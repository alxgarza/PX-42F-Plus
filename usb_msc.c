#include "usb_msc.h"

#include <string.h>

#include "tusb.h"
#include "flashdisk.h"

/* These callbacks run in TinyUSB's task, on core 0 (pico_stdio_usb's
 * background IRQ). They only ever touch the flashdisk cache, never the
 * flash itself: when the cache is full, write10 waits for core 1
 * (statefs_usb_poll()) to flush a sector.
 *
 * write10 must never return 0 ("busy, call again"): TinyUSB re-queues the
 * transfer and tud_task() processes it again straight away, so core 0
 * spins inside the USB task -- which is what used to freeze the
 * calculator in USB Disk mode. */

static volatile bool attached = false;
static volatile bool ejected = false;
static volatile bool sync_requested = false;

void usb_msc_attach(void) {
    ejected = false;
    attached = true;
}

void usb_msc_detach(void) {
    attached = false;
}

bool usb_msc_ejected(void) {
    return ejected;
}

bool usb_msc_take_sync_request(void) {
    bool r = sync_requested;
    sync_requested = false;
    return r;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void) lun;
    memcpy(vendor_id, "PX-42   ", 8);
    memcpy(product_id, "PX-42F State    ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (!attached) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00); /* medium not present */
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void) lun;
    *block_count = flashdisk_block_count();
    *block_size = FLASHDISK_BLOCK_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void) lun;
    (void) power_condition;
    if (load_eject && !start) {
        /* The computer ejected the drive: everything it wanted written has
         * been sent by now. */
        attached = false;
        ejected = true;
        sync_requested = true;
    }
    return true;
}

bool tud_msc_prevent_allow_medium_removal_cb(uint8_t lun, uint8_t prohibit_removal, uint8_t control) {
    (void) lun;
    (void) prohibit_removal;
    (void) control;
    return true;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void) lun;
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    (void) lun;
    if (!attached || !flashdisk_read(lba * FLASHDISK_BLOCK_SIZE + offset, buffer, bufsize))
        return -1;
    return (int32_t) bufsize;
}

static bool still_attached(void) {
    return attached;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void) lun;
    if (!attached)
        return -1;
    /* Waits (if the cache is full) until core 1 has flushed, or the disk
     * is taken back -- then fail the write. */
    return flashdisk_write(lba * FLASHDISK_BLOCK_SIZE + offset, buffer, bufsize, still_attached)
           ? (int32_t) bufsize : -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void) buffer;
    (void) bufsize;
    switch (scsi_cmd[0]) {
        case 0x35: /* SYNCHRONIZE CACHE (10) */
            sync_requested = true;
            return 0;
        default:
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00); /* invalid command */
            return -1;
    }
}
