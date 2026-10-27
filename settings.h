#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PX-42 settings (the SHIFT+OFF menu, see settings_menu.c), kept in the
 * last sector of flash so they survive power loss. */
/* Longest splash image name kept (file name in /SPLASH without ".png"). */
#define SPLASH_NAME_MAX 28
/* Longest key sound name kept (file name in /SOUNDS/KEY without ".wav"). */
#define KEY_SOUND_NAME_MAX 28
/* Longest font name kept (file name in /FONTS/SMALL or LARGE without ".png"). */
#define FONT_SETTING_MAX 28

/* What the keys type in ALPHA (free42_app.cc), as on the DM42: [ ] the
 * HP-42S's own keys, letters from the ALPHA menu; [a] / [A] the letter
 * printed by each key, lower or upper case. SHIFT then an arrow switches. */
typedef enum { ALPHA_MODE_HP42S, ALPHA_MODE_LOWER, ALPHA_MODE_UPPER, ALPHA_MODES } alpha_mode_t;

typedef struct {
    uint16_t sleep_minutes;  /* auto power-off after this long idle; 0 = never */
    uint16_t splash_minutes; /* after OFF, show the splash this long, then blank; 0 = blank right away */
    char splash_image[SPLASH_NAME_MAX + 1]; /* /SPLASH/<name>.png; "" = the built-in image */
    uint8_t printer;         /* printer_target_t (printer.h): where Free42 prints */
    char key_sound[KEY_SOUND_NAME_MAX + 1]; /* sound on every keypress: /SOUNDS/KEY/<name>.wav
                                             * or the built-in of that name; "" = off */
    bool unused;             /* was Boot Sound: BOOT.wav plays at every start now. Kept so
                              * the older versions' records still line up. */
    bool on_sound;           /* /SOUNDS/ON.wav when turning on with ON */
    bool off_sound;          /* /SOUNDS/OFF.wav when turning off */
    /* Date & Time (rtc.h). The formats are Free42's own too (DMY/MDY/YMD,
     * CLK12/CLK24) -- these keep them for when Free42 isn't running. */
    uint8_t date_format;     /* rtc_date_format_t */
    bool clock_24h;
    int16_t time_trim;       /* tenths of a second per day added to the clock; + = it ran slow */
    int16_t cal_drift;       /* last measured: tenths of a second per day the clock gained; - = lost */
    uint16_t cal_span_h;     /* ... over this many hours; 0 = nothing measured */
    uint64_t cal_ref_ms;     /* clock time when it was last set exactly; 0 = none */
    /* The calculator screen's fonts (fonts.h); "" = the built-in font. */
    char small_font[FONT_SETTING_MAX + 1]; /* /FONTS/SMALL/<name>.png */
    char large_font[FONT_SETTING_MAX + 1]; /* /FONTS/LARGE/<name>.png */
    char mini_font[FONT_SETTING_MAX + 1];  /* /FONTS/MINI/<name>.png */
    uint8_t alpha_mode;      /* alpha_mode_t: the last one used, the next ALPHA starts in it */
} px_settings_t;

extern px_settings_t px_settings;

/* Loads the saved settings, or the defaults if flash holds none (first
 * boot, or a layout from an older firmware). Call once at startup. */
void settings_load(void);

/* Writes px_settings to flash if it differs from what's stored. Takes the
 * other core out of flash for the duration (see flash_safe_execute()),
 * and pauses the watchdog -- an erase can outlast its window. */
void settings_save(void);

/* For changes that come often or from programs (a Free42 SETIME or DMY in
 * a loop must not wear the flash out): remembered, and saved by
 * settings_save_pending() -- at OFF. */
void settings_save_later(void);
void settings_save_pending(void);

#ifdef __cplusplus
}
#endif

#endif
