/* Host stand-in for the Pico SDK's watchdog (tools/menu_shots). */
#ifndef HARDWARE_WATCHDOG_H
#define HARDWARE_WATCHDOG_H
#include <stdint.h>
typedef struct { uint32_t scratch[8]; } watchdog_hw_t;
extern watchdog_hw_t host_watchdog;
#define watchdog_hw (&host_watchdog)
static inline void watchdog_update(void) {}
#endif
