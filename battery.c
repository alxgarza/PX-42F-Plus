#include "battery.h"

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "speaker.h"

#define BATTERY_PIN       28
#define BATTERY_ADC_INPUT 2    /* GP26-29 are ADC inputs 0-3 */

/* ADC full scale is ADC_AVDD (3.3V on the Pico 2 W), and the divider
 * halves the cell voltage, so 4.2V (full) reads as 2.1V -- in range. If
 * the shown voltage turns out consistently off against a multimeter,
 * adjust BATTERY_CAL_PERMILLE (1000 = no correction). */
#define ADC_VREF_MV          3300
#define DIVIDER_RATIO        2
#define BATTERY_CAL_PERMILLE 1000

#define BATTERY_SAMPLES 32

/* Level n (1-4) needs at least level_mv[n - 1]; below level_mv[0] is
 * level 0. Rough lithium-ion discharge points under light load -- tune to
 * taste. */
static const int level_mv[4] = {3550, 3700, 3800, 3950};
#define LEVEL_HYSTERESIS_MV 25
#define BATTERY_LOW_MV      3550

static int current_level = -1; /* -1 = no reading yet */
static bool last_low = false;

/* The speaker draws enough to sag the battery voltage by hundreds of mV,
 * and it takes a moment to come back -- BEEP's tones read as an empty
 * battery. For this long after a tone, keep the last level instead. */
#define AFTER_TONE_US 1000000

static bool sagging_after_tone(void) {
    uint64_t t = speaker_last_tone_us();
    return t != 0 && time_us_64() - t < AFTER_TONE_US;
}

void battery_init(void) {
    adc_init();
    adc_gpio_init(BATTERY_PIN); /* disables the digital input and pulls -- they'd load the 500k divider */
    adc_hw->cs = 0; /* off until a reading -- see battery_read_mv() */
}

int battery_read_mv(void) {
    /* The ADC is only switched on for the reading: its clock stops
     * whenever both cores sleep (cpuclock.h), which an enabled ADC doesn't
     * survive, and off it draws nothing. Enabling runs its internal
     * start-up; READY says it's done. */
    adc_hw->cs = ADC_CS_EN_BITS;
    while (!(adc_hw->cs & ADC_CS_READY_BITS))
        tight_loop_contents();
    adc_select_input(BATTERY_ADC_INPUT);
    busy_wait_us(50); /* the sampling capacitor charges through the divider's 500k */

    /* The divider's 500k source impedance is high for the ADC's sampling
     * capacitor, so throw away the first conversion after switching input
     * and average the rest. */
    (void) adc_read();
    uint32_t sum = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++)
        sum += adc_read();
    adc_hw->cs = 0;

    uint32_t mv = sum * ADC_VREF_MV * DIVIDER_RATIO / (4096u * BATTERY_SAMPLES);
    return (int) (mv * BATTERY_CAL_PERMILLE / 1000);
}

int battery_level(void) {
    if (current_level >= 0 && sagging_after_tone())
        return current_level;
    int mv = battery_read_mv();

    if (current_level < 0) {
        current_level = 0;
        while (current_level < 4 && mv >= level_mv[current_level])
            current_level++;
        return current_level;
    }

    while (current_level < 4 && mv >= level_mv[current_level] + LEVEL_HYSTERESIS_MV)
        current_level++;
    while (current_level > 0 && mv < level_mv[current_level - 1] - LEVEL_HYSTERESIS_MV)
        current_level--;
    return current_level;
}

/* Below this, power off. The reading can't go much under ~3.35V whatever
 * the cell does: the ADC measures against the 3.3V rail, which sinks with
 * the cell once the regulator runs out of headroom -- so the threshold has
 * to sit above that, where readings are still true. */
#define BATTERY_EMPTY_MV 3450

bool battery_empty(void) {
    static int low_readings = 0;
    if (sagging_after_tone())
        return false;
    low_readings = battery_read_mv() < BATTERY_EMPTY_MV ? low_readings + 1 : 0;
    return low_readings >= 3;
}

bool battery_low(void) {
    if (!sagging_after_tone())
        last_low = battery_read_mv() < BATTERY_LOW_MV;
    return last_low;
}
