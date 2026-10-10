#ifndef FLASHDISK_H
#define FLASHDISK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The state-file disk: a region of the on-board flash, presented as 512-byte
 * blocks -- to FatFs on the calculator (fatfs_diskio.c), and to a computer
 * as a USB drive (usb_msc.c), never both at once.
 *
 * Flash can only be erased in 4KB sectors, so writes go through a small
 * write-back cache of whole sectors; flashdisk_flush_*() erase and program
 * them. Only core 1 ever erases or programs flash (core 0 is parked in RAM
 * meanwhile, see flash_safe_execute()), but reads and cached writes are
 * also safe from core 0 -- the USB drive's callbacks run there. */

#define FLASHDISK_BLOCK_SIZE 512

/* The disk is 12MB, flash 4MB..16MB (flashdisk.c's layout). Addresses
 * below are relative to the start of the disk. */

/* Checks that the program image ends below the disk region, and sets up
 * the cache. Returns false (and every other call then fails) if the image
 * has grown into the settings or disk region -- they must never overwrite
 * code. */
bool flashdisk_init(void);

uint32_t flashdisk_block_count(void);

/* Byte-addressed reads/writes within the disk; may span sectors. */
bool flashdisk_read(uint32_t addr, void *buf, uint32_t len);

/* keep_waiting == NULL (core 1 only): when the cache is full, flushes a
 * sector itself; fails only if a flush fails.
 *
 * keep_waiting != NULL (core 0, the USB drive, which must not touch flash):
 * when the cache is full, flags that room is needed (flashdisk_room_needed())
 * and waits for core 1 to flush, for as long as keep_waiting() returns true;
 * false if it gave up. The wait polls a counter without taking the cache
 * lock, so it can't starve core 1 of it -- and it must run at a priority
 * core 1's flash lockout can interrupt (see main.c). */
bool flashdisk_write(uint32_t addr, const void *buf, uint32_t len, bool (*keep_waiting)(void));

/* True while a core-0 write is waiting for core 1 to flush. */
bool flashdisk_room_needed(void);

/* Core 1 only. flush_one writes back the oldest dirty sector, if any, and
 * returns whether it did (false also if the write failed); flush_all
 * writes back everything it can, and returns false if a write failed
 * (that sector stays cached and dirty). Each sector takes tens of ms and
 * pauses the watchdog for its duration. */
bool flashdisk_flush_one(void);
bool flashdisk_flush_all(void);

bool flashdisk_dirty(void);

/* Flash writes that failed so far (flash_safe_execute() refused); their
 * data stays cached and dirty. */
uint32_t flashdisk_error_count(void);

/* Time of the most recent write, for flushing once the host goes quiet. */
uint64_t flashdisk_last_write_us(void);

#ifdef __cplusplus
}
#endif

#endif
