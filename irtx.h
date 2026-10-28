#ifndef IRTX_H
#define IRTX_H

#include "px_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The infrared LED, on GP14 through a transistor: high = on. Kept low
 * (off) except while sending. */
#define IR_LED_PIN 14

/* Sends one transmission (px_app.h: an app encodes it): waits out its gap,
 * counted from the end of the last one, then switches the LED at the
 * carrier for its pulses, with PIO0. Blocking, feeds the watchdog; core 1,
 * at full speed. */
void irtx_send(const px_ir_burst_t *burst);

#ifdef __cplusplus
}
#endif

#endif
