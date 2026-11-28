#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

/* The 16 MB flash (W25Q128, boards/px42f.h), the same in every build:
 *
 *   0 .. 4 MB - 8 KB   this firmware (3.1 MB as of this writing)
 *   4 MB - 8 KB .. 4 MB settings, two sectors (settings.c)
 *   4 MB .. 11 MB      the drive, 7 MB (flashdisk.c, statefs.c)
 *   11 MB .. 16 MB     the CAS program (cas.h): Giac, a program of its own,
 *                      and in its last 128 KB the CAS's session (two 64 KB
 *                      slots written in turn: its settings, stored
 *                      variables, stack), kept through reflashes of either
 *
 * The drive was 12 MB, to the end, before the CAS: a volume formatted then
 * is bigger than the drive now, and is formatted again (statefs_init()).
 * The CAS's area is there whether or not the CAS is installed, so a build
 * without it keeps the same drive. */
#define FLASH_DISK_OFFSET (4u * 1024u * 1024u)
#define FLASH_CAS_OFFSET  (11u * 1024u * 1024u)
#define FLASH_CAS_SIZE    (5u * 1024u * 1024u)
#define FLASH_DISK_SIZE   (FLASH_CAS_OFFSET - FLASH_DISK_OFFSET)
#define FLASH_CAS_SESSION_SIZE   (128u * 1024u)
#define FLASH_CAS_SESSION_OFFSET (FLASH_CAS_OFFSET + FLASH_CAS_SIZE - FLASH_CAS_SESSION_SIZE)

#endif
