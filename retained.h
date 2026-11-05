#ifndef RETAINED_H
#define RETAINED_H

/* RAM kept through the deep power-off: the top of SRAM bank 1 (0x20040000
 * up), which stays powered (power.c), outside everything the linker
 * places -- so the startup code never clears it either. The heap ends at
 * RETAINED_BASE (HEAP_LIMIT in CMakeLists.txt, which must match). */
#define RETAINED_BASE       0x2005F000u

/* The display's framebuffer (display.c), 4KB: the splash image is still in
 * it after a wake to update the splash clock (power.c). */
#define RETAINED_DISPLAY_FB 0x2005F000u

/* When the next alarm comes due, for the wakes while off (power.c): after the
 * framebuffer's 3904 bytes. */
#define RETAINED_NEXT_ALARM 0x2005FFF8u

/* The calculator's memory while off (ramstate.c), the top 128KB. */
#define RETAINED_RAMSTATE   0x20060000u

#endif
