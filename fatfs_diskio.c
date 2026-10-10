/* FatFs's disk I/O layer (third_party/fatfs/diskio.h), on top of flashdisk.
 * One drive, 512-byte sectors. */
#include <stddef.h>

#include "ff.h"
#include "diskio.h"

#include "flashdisk.h"
#include "rtc.h"

static DSTATUS status = STA_NOINIT;

DSTATUS disk_status(BYTE pdrv) {
    return pdrv == 0 ? status : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0)
        return STA_NOINIT;
    status = flashdisk_block_count() > 0 ? 0 : STA_NOINIT;
    return status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || status)
        return RES_NOTRDY;
    return flashdisk_read((uint32_t) sector * FLASHDISK_BLOCK_SIZE, buff, count * FLASHDISK_BLOCK_SIZE)
           ? RES_OK : RES_PARERR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || status)
        return RES_NOTRDY;
    return flashdisk_write((uint32_t) sector * FLASHDISK_BLOCK_SIZE, buff, count * FLASHDISK_BLOCK_SIZE, NULL)
           ? RES_OK : RES_PARERR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0 || status)
        return RES_NOTRDY;
    switch (cmd) {
        case CTRL_SYNC: /* a failed write here is what fclose() reports */
            return flashdisk_flush_all() ? RES_OK : RES_ERROR;
        case GET_SECTOR_COUNT:
            *(LBA_t *) buff = flashdisk_block_count();
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *) buff = FLASHDISK_BLOCK_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *) buff = 4096 / FLASHDISK_BLOCK_SIZE; /* flash erase sector, in blocks */
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

/* File timestamps, from the calculator clock. */
DWORD get_fattime(void) {
    rtc_datetime_t t;
    rtc_get_datetime(&t);
    return ((DWORD) (t.year - 1980) << 25) | ((DWORD) t.month << 21) | ((DWORD) t.day << 16)
           | ((DWORD) t.hour << 11) | ((DWORD) t.min << 5) | ((DWORD) t.sec >> 1);
}
