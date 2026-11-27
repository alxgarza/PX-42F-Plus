#include "flashdisk.h"

#include <string.h>

#include "pico/critical_section.h"
#include "pico/flash.h"
#include "pico/time.h"
#include "hardware/timer.h"
#include "hardware/flash.h"
#include "watchdog_cfg.h"
#include "crash_report.h"
#include "debug_checkpoint.h"
#include "flash_layout.h"

/* The flash layout: flash_layout.h. */
#define DISK_OFFSET FLASH_DISK_OFFSET
#define DISK_SIZE   FLASH_DISK_SIZE
#define IMAGE_LIMIT (DISK_OFFSET - 2 * FLASH_SECTOR_SIZE) /* code must end below the settings sectors */

#define CACHE_SLOTS 4

typedef struct {
    int32_t sector;     /* flash sector held here (absolute: offset / 4KB), -1 = empty */
    bool dirty;         /* differs from flash */
    bool flushing;      /* being written back right now -- don't evict */
    uint32_t last_use;  /* for LRU */
    uint8_t data[FLASH_SECTOR_SIZE];
} cache_slot_t;

static cache_slot_t slots[CACHE_SLOTS];
static uint8_t flush_buf[FLASH_SECTOR_SIZE];
static critical_section_t cache_lock;
static uint32_t use_counter;
static volatile uint64_t last_write_us;
static bool usable;
static volatile uint32_t error_count;
static volatile uint32_t flush_generation; /* bumped after every flush attempt */
static volatile bool room_needed;

extern char __flash_binary_end;

static const uint8_t *flash_ptr(int32_t sector) {
    return (const uint8_t *) (XIP_BASE + (uint32_t) sector * FLASH_SECTOR_SIZE);
}

bool flashdisk_init(void) {
    uint32_t image_end = (uint32_t) (uintptr_t) &__flash_binary_end - XIP_BASE;
    usable = image_end <= IMAGE_LIMIT;
    critical_section_init(&cache_lock);
    for (int i = 0; i < CACHE_SLOTS; i++)
        slots[i].sector = -1;
    return usable;
}

uint32_t flashdisk_block_count(void) {
    return usable ? DISK_SIZE / FLASHDISK_BLOCK_SIZE : 0;
}

/* Checks a disk-relative range, and turns addr into an absolute flash
 * offset. */
static bool to_flash(uint32_t *addr, uint32_t len) {
    if (!usable || *addr > DISK_SIZE || len > DISK_SIZE - *addr)
        return false;
    *addr += DISK_OFFSET;
    return true;
}

/* Callers hold cache_lock. */
static cache_slot_t *find_slot(int32_t sector) {
    for (int i = 0; i < CACHE_SLOTS; i++)
        if (slots[i].sector == sector)
            return &slots[i];
    return NULL;
}

/* Callers hold cache_lock. An empty slot, else the least recently used
 * clean one; NULL if every slot is dirty or being flushed. */
static cache_slot_t *free_slot(void) {
    cache_slot_t *best = NULL;
    for (int i = 0; i < CACHE_SLOTS; i++) {
        cache_slot_t *s = &slots[i];
        if (s->sector < 0)
            return s;
        if (!s->dirty && !s->flushing && (best == NULL || s->last_use < best->last_use))
            best = s;
    }
    return best;
}

bool flashdisk_read(uint32_t addr, void *buf, uint32_t len) {
    if (!to_flash(&addr, len))
        return false;
    uint8_t *out = (uint8_t *) buf;
    while (len > 0) {
        int32_t sector = (int32_t) (addr / FLASH_SECTOR_SIZE);
        uint32_t off = addr % FLASH_SECTOR_SIZE;
        uint32_t n = FLASH_SECTOR_SIZE - off < len ? FLASH_SECTOR_SIZE - off : len;

        critical_section_enter_blocking(&cache_lock);
        cache_slot_t *s = find_slot(sector);
        memcpy(out, s != NULL ? s->data + off : flash_ptr(sector) + off, n);
        critical_section_exit(&cache_lock);

        out += n;
        addr += n;
        len -= n;
    }
    return true;
}

bool flashdisk_write(uint32_t addr, const void *buf, uint32_t len, bool (*keep_waiting)(void)) {
    if (!to_flash(&addr, len))
        return false;
    const uint8_t *in = (const uint8_t *) buf;
    while (len > 0) {
        int32_t sector = (int32_t) (addr / FLASH_SECTOR_SIZE);
        uint32_t off = addr % FLASH_SECTOR_SIZE;
        uint32_t n = FLASH_SECTOR_SIZE - off < len ? FLASH_SECTOR_SIZE - off : len;

        critical_section_enter_blocking(&cache_lock);
        cache_slot_t *s = find_slot(sector);
        if (s == NULL) {
            s = free_slot();
            if (s == NULL) {
                /* Every slot dirty (or mid-flush). */
                uint32_t generation = flush_generation;
                critical_section_exit(&cache_lock);
                if (keep_waiting == NULL) {
                    if (!flashdisk_flush_one())
                        return false; /* the flush failed (see crash_note_flash_error()) */
                    continue;
                }
                room_needed = true;
                while (flush_generation == generation) {
                    if (!keep_waiting()) {
                        room_needed = false;
                        return false;
                    }
                    busy_wait_us(100);
                }
                room_needed = false;
                continue;
            }
            s->sector = sector;
            s->dirty = false;
            memcpy(s->data, flash_ptr(sector), FLASH_SECTOR_SIZE);
        }
        memcpy(s->data + off, in, n);
        s->dirty = true;
        s->last_use = ++use_counter;
        last_write_us = time_us_64();
        critical_section_exit(&cache_lock);

        in += n;
        addr += n;
        len -= n;
    }
    return true;
}

static uint32_t flush_target;

static void program_sector(void *param) {
    (void) param;
    flash_range_erase(flush_target, FLASH_SECTOR_SIZE);
    flash_range_program(flush_target, flush_buf, FLASH_SECTOR_SIZE);
}

bool flashdisk_flush_one(void) {
    /* Take a snapshot of the oldest dirty slot and mark it clean, then
     * write the snapshot. The slot stays pinned (flushing) until the flash
     * holds the new data: if it were evicted in between, a write to the
     * same sector would reload the *old* contents from flash and the
     * snapshot's changes would be lost. A write landing during the flush
     * just marks the slot dirty again. */
    critical_section_enter_blocking(&cache_lock);
    cache_slot_t *s = NULL;
    for (int i = 0; i < CACHE_SLOTS; i++)
        if (slots[i].dirty && !slots[i].flushing && (s == NULL || slots[i].last_use < s->last_use))
            s = &slots[i];
    if (s == NULL) {
        critical_section_exit(&cache_lock);
        return false;
    }
    memcpy(flush_buf, s->data, FLASH_SECTOR_SIZE);
    flush_target = (uint32_t) s->sector * FLASH_SECTOR_SIZE;
    s->dirty = false;
    s->flushing = true;
    critical_section_exit(&cache_lock);

    /* Skip the erase/program if flash already matches (e.g. a host
     * rewriting identical FAT sectors) -- saves time and wear. */
    int rc = PICO_OK;
    if (memcmp(flush_buf, (const void *) (XIP_BASE + flush_target), FLASH_SECTOR_SIZE) != 0) {
        debug_checkpoint(CKPT_FLASH_WRITE, flush_target);
        watchdog_pause();
        rc = flash_safe_execute(program_sector, NULL, 1000);
        watchdog_resume();
        debug_checkpoint(CKPT_FLASH_DONE, (uint32_t) rc);
    }

    critical_section_enter_blocking(&cache_lock);
    s->flushing = false;
    if (rc != PICO_OK)
        s->dirty = true; /* not written after all -- keep it */
    flush_generation++;
    critical_section_exit(&cache_lock);

    if (rc != PICO_OK) {
        /* flash_safe_execute() skips the write when it can't park the
         * other core; never treat that as written. */
        error_count++;
        crash_note_flash_error(rc, flush_target);
        return false;
    }
    return true;
}

bool flashdisk_flush_all(void) {
    uint32_t errors_before = error_count;
    while (flashdisk_flush_one())
        ;
    return error_count == errors_before;
}

bool flashdisk_dirty(void) {
    bool dirty = false;
    critical_section_enter_blocking(&cache_lock);
    for (int i = 0; i < CACHE_SLOTS; i++)
        dirty |= slots[i].dirty;
    critical_section_exit(&cache_lock);
    return dirty;
}

bool flashdisk_room_needed(void) {
    return room_needed;
}

uint32_t flashdisk_error_count(void) {
    return error_count;
}

uint64_t flashdisk_last_write_us(void) {
    return last_write_us;
}
