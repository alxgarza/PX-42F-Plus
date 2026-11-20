#ifndef PX_APP_H
#define PX_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PX-42F native apps and games: .bin files in /BIN/APPS and /BIN/GAMES on
 * the drive, started from Settings -> Games & Apps. This header is the
 * whole contract between the firmware and an app. The firmware's copy
 * (PX-42F-Plus/px_app.h) is the original; each app project carries a copy
 * (PX-42F-App-Template is where a new one starts).
 *
 * A .bin is Cortex-M33 code (Thumb, -mfloat-abi=softfp, like the firmware)
 * linked to run at PX_APP_BASE: the file is copied there whole and its
 * entry called on core 1 with the table below. Returning ends the app:
 * PX_EXIT_OFF turns the calculator off, anything else goes back to the
 * menu.
 *
 *  - RAM: PX_APP_RAM bytes from PX_APP_BASE hold the file, its zeroed bss,
 *    and what's left for malloc(). Its top 128 KB is the RAM that keeps
 *    the calculator's memory while off, so the firmware first saves that
 *    memory to the drive: an app that crashes or hangs loses nothing. An
 *    app built for API 1 to 8 has the old 128 KB at 0x20060000, the top of
 *    this one, and runs there unchanged.
 *  - The stack is the firmware's: core 1's 32 KB, part of it already in
 *    use. Keep big arrays static, not local.
 *  - The watchdog restarts the calculator if 100 ms pass without a call
 *    into this table: a game waits in key() or sleep() between frames.
 *  - ON (the EXIT key) ends the app, after asking: the firmware shows
 *    "Quit NAME?" (QUIT / STAY) over the app's screen; on QUIT key()
 *    returns PX_KEY_QUIT, on STAY nothing reaches the app (its screen
 *    comes back, millis() stood still). The Sleep Timer, an empty battery
 *    and ON held 2 s give PX_KEY_QUIT without asking: then return. An
 *    app with its own way out can have ON as an ordinary key instead
 *    (on_key()), and ask itself (ui_dialog()).
 *  - The table only grows. A firmware adds functions at the end and raises
 *    PX_APP_API_VERSION; an app built for version n runs on any firmware
 *    from n up, and an older firmware refuses it. */

#define PX_APP_MAGIC       0x50415850u /* "PXAP" */
#define PX_APP_API_VERSION 11 /* 2: decimal numbers; 3: on_key(); 4: MIDI; 5: shapes; 6: alarms; 7: printing; 8: sprites; 9: 160 KB; 10: ui_*(); 11: ui_dialog() with an icon and buttons, ui_view_leave() */
#define PX_APP_BASE        0x20058000u
#define PX_APP_RAM         0x28000u /* 160 KB, to the end of RAM */

typedef struct {
    uint32_t magic;       /* PX_APP_MAGIC */
    uint32_t api_version; /* the table it was built for */
    uint32_t image_size;  /* the whole file: this header, code and data */
    uint32_t bss_size;    /* zeroed RAM right after the image */
    uint32_t entry;       /* int entry(const px_api_t *), Thumb bit set */
    uint32_t crc32;       /* zlib's CRC-32 of the file after this header */
} px_app_header_t;

/* The app's icon in the menu, if it has one: right after the header, at
 * PX_APP_BASE + sizeof(px_app_header_t) (the template's linker script puts
 * the .px_icon section there; its Makefile makes it from icon.txt). A file
 * without PX_APP_ICON_MAGIC there gets a generic icon. It's part of the
 * image, so a firmware that doesn't show icons still runs the app. */
#define PX_APP_ICON_MAGIC 0x43495850u /* "PXIC" */
#define PX_APP_ICON_SIZE  32 /* pixels square: rows of 4 bytes, the leftmost pixel in the top bit, 1 = black */
typedef struct {
    uint32_t magic; /* PX_APP_ICON_MAGIC */
    uint8_t bits[PX_APP_ICON_SIZE * PX_APP_ICON_SIZE / 8];
} px_app_icon_t;

/* What the entry returns. */
#define PX_EXIT_MENU 0 /* back to Settings -> Games & Apps */
#define PX_EXIT_OFF  1 /* turn the calculator off */

/* ----- the screen -----
 * 1 bit a pixel, 1 = black, rows of PX_SCREEN_STRIDE bytes, the leftmost
 * pixel in a byte's top bit. Drawing changes it in RAM (also directly,
 * through `screen`); show() sends it to the panel. */
#define PX_SCREEN_W      250
#define PX_SCREEN_H      122
#define PX_SCREEN_STRIDE 32

/* Fonts for text(): the calculator's built-in three, or the ones chosen in
 * Settings -> Fonts (the calculator screen's; the same sizes). Character
 * codes are the HP-42S's: 0-31 are its symbols (0 divide, 1 times, 2
 * square root, 5 Sigma, 13 enter arrow, 19 degree...). */
#define PX_FONT_SMALL       0 /* 8 x 12, 8 pixels a character */
#define PX_FONT_LARGE       1 /* 10 x 16, 11 pixels a character */
#define PX_FONT_MINI        2 /* 6 x 8, 6 pixels a character */
#define PX_FONT_CHOSEN_SMALL 3
#define PX_FONT_CHOSEN_LARGE 4
#define PX_FONT_CHOSEN_MINI  5

/* A corner of polygon(), which takes at most PX_POLY_MAX. */
#define PX_POLY_MAX 64
typedef struct {
    int16_t x, y;
} px_point_t;

/* ----- sprites (version 8) ----- sprite()'s mode: what its pixels (where
 * the mask has a 1) do to the screen */
#define PX_SPRITE_OR     0 /* 1 bits black, 0 bits leave the screen as it was */
#define PX_SPRITE_XOR    1 /* 1 bits turn the pixel over: the same call again takes it away */
#define PX_SPRITE_MASKED 2 /* copied, white and black: with a mask, a shape with its own white */
/* and its flags, either or both: mirrored */
#define PX_SPRITE_FLIP_X 1 /* left to right */
#define PX_SPRITE_FLIP_Y 2 /* top to bottom */

/* ----- keys -----
 * By where they sit, as IR remote files name them: row 0-6 from the top,
 * column 0-5 from the left (the top two rows have 6 keys, the others 5;
 * ENTER is R2C0). */
#define PX_KEY(row, col) ((row) << 3 | (col))
#define PX_KEY_ROW(key)  ((key) >> 3 & 7)
#define PX_KEY_COL(key)  ((key) & 7)
#define PX_KEY_SIGMA     PX_KEY(0, 0) /* top left */
#define PX_KEY_XEQ       PX_KEY(0, 5) /* top right */
#define PX_KEY_ENTER     PX_KEY(2, 0)
#define PX_KEY_BACKSPACE PX_KEY(2, 4)
#define PX_KEY_ARROW_UP  PX_KEY(3, 0)
#define PX_KEY_ARROW_DN  PX_KEY(4, 0)
#define PX_KEY_SHIFT     PX_KEY(5, 0)
#define PX_KEY_RS        PX_KEY(6, 3)
#define PX_KEY_ON        PX_KEY(6, 0) /* only after on_key(1) */

/* What key() returns: a key that went down, PX_KEY(r, c); one let go,
 * PX_KEY(r, c) | PX_KEY_RELEASED; or one of these. */
#define PX_KEY_RELEASED 0x100
#define PX_KEY_NONE     (-1) /* the wait ran out */
#define PX_KEY_QUIT     (-2) /* return now: ON, the Sleep Timer, an empty battery */

/* ----- time ----- */
typedef struct {
    int32_t year;    /* 2026 */
    int32_t month;   /* 1-12 */
    int32_t day;     /* 1-31 */
    int32_t hour;    /* 0-23 */
    int32_t min;     /* 0-59 */
    int32_t sec;     /* 0-59 */
    int32_t ms;      /* 0-999 */
    int32_t weekday; /* 0 = Sunday */
} px_datetime_t;

/* ----- files -----
 * A path is on the calculator's drive (FAT; the computer sees the same
 * files in USB Drive mode): "/SOUNDS/ON.wav" from the top, or "BEST" in
 * the app's own folder -- /BIN/GAMES/SNAKE/ for /BIN/GAMES/SNAKE.bin, made
 * the first time something is written there. At most PX_PATH_MAX
 * characters. Writing is to flash: save a high score, not every frame. */
#define PX_PATH_MAX      96
#define PX_FILE_NAME_MAX 28 /* a name list() returns */
#define PX_FILES_OPEN    4  /* at once; any still open are closed when the app ends */

/* ----- printing (version 7) ----- where a print goes: Settings -> Printer */
#define PX_PRINTER_OFF   0  /* Off, or the USB port with no terminal open: nothing prints */
#define PX_PRINTER_USB   1  /* "PX-42F Printer", the second USB serial port */
#define PX_PRINTER_IR    2  /* the HP 82240B, by infrared */
#define PX_PRINTER_THERMAL 3 /* a USB thermal receipt printer (ESC/POS), on the USB port (a firmware
                              * of Nov 2026 on; older ones never say it) */
#define PX_PRINT_COLUMNS 24 /* characters on a line of the HP 82240B's paper */

/* ----- infrared -----
 * One transmission: microseconds, alternately off and on, starting with
 * the gap before it (off, counted from the end of the last one) and ending
 * with the last pulse (on). The LED blinks at carrier_hz while on. */
#define PX_IR_MAX_TIMINGS 112
typedef struct {
    uint32_t carrier_hz;
    int32_t count;
    uint32_t us[PX_IR_MAX_TIMINGS];
} px_ir_burst_t;

/* ----- decimal numbers -----
 * Plus42's own numbers: IEEE 754 decimal128 (Intel's decimal library, the
 * one the calculator computes with), 34 significant digits, exponents to
 * +-6144. 0.1 + 0.2 is exactly 0.3. Results too big are +-Inf; ones with
 * no answer (SQRT(-1), 0/0) NaN: dec_class() says which. Angles are in
 * radians. */
typedef struct {
    uint32_t w[4];
} __attribute__((aligned(16))) px_dec_t;

/* The operations for dec(): r = a op b for the first eight, op(a) for the
 * rest (b unused). */
enum {
    PX_DEC_ADD, PX_DEC_SUB, PX_DEC_MUL, PX_DEC_DIV,
    PX_DEC_POW,   /* a to the power b */
    PX_DEC_REM,   /* IEEE remainder: a - b * n, n = a/b rounded to the nearest */
    PX_DEC_ATAN2, /* the angle of the point (b, a): atan2(a, b) */
    PX_DEC_HYPOT, /* sqrt(a^2 + b^2) */
    PX_DEC_NEG, PX_DEC_ABS, PX_DEC_SQRT,
    PX_DEC_EXP, PX_DEC_LN, PX_DEC_LOG10, PX_DEC_EXPM1, PX_DEC_LN1P,
    PX_DEC_SIN, PX_DEC_COS, PX_DEC_TAN, PX_DEC_ASIN, PX_DEC_ACOS, PX_DEC_ATAN,
    PX_DEC_SINH, PX_DEC_COSH, PX_DEC_TANH, PX_DEC_ASINH, PX_DEC_ACOSH, PX_DEC_ATANH,
    PX_DEC_GAMMA, /* Gamma(a): (a-1)! */
    PX_DEC_FLOOR, PX_DEC_CEIL, PX_DEC_TRUNC, /* to a whole number: down, up, toward 0 */
    PX_DEC_OPS
};

/* dec_class() */
#define PX_DEC_FINITE 0
#define PX_DEC_INF    1
#define PX_DEC_NAN    2

/* dec_to_string()'s longest: sign, 34 digits, "E", sign, 4 digits, 0. */
#define PX_DEC_STRING_MAX 48

/* ----- MIDI ----- midi_play() */
#define PX_MIDI_NOT_FOUND (-1) /* no such file, or it can't be read */
#define PX_MIDI_NOT_MIDI  (-2) /* not a Standard MIDI File the synth can play */
#define PX_MIDI_TOO_BIG   (-3) /* over 128 KB */
#define PX_MIDI_NO_MEMORY (-4)

/* midi_status(): all 0 when no song plays. */
typedef struct {
    uint32_t length_ms;  /* to the song's last event */
    uint32_t elapsed_ms;
    uint32_t channels;   /* bit n = MIDI channel n+1 has a note sounding */
    uint32_t voices;     /* notes sounding (at most 24) */
} px_midi_status_t;

/* ----- alarms ----- alarm_set() */
#define PX_ALARM_TEXT_MAX 44

/* ----- the standard screens ----- ui_*()
 * The settings menu's own, for apps to look and work the same: a title bar,
 * rows in the small font, and soft-key tabs along the bottom for the top
 * row of keys. Each takes the screen and the keys until it's done (redraw
 * yours after). Everywhere: ENTER does the obvious thing, <- (and ON for an
 * app with on_key(1)) goes back, ▲ ▼ move (8 2 too), and so do a "^" or
 * "\x0e" tab. The tabs' places are fixed: the first names what ENTER
 * does, ▲ ▼ are on the fourth and fifth, EXIT on the sixth. A list or form
 * puts ▲ ▼ there by itself when it has more than one row and they're free,
 * and EXIT if the sixth is free; an "EXIT" tab anywhere does what <- does
 * (PX_UI_BACK). A "" tab is no tab: that key does nothing. Text entry has
 * no tabs: the top row of keys types A-F there.
 * They return: */
#define PX_UI_ENTER 6    /* ENTER, or done (an editor's ENTER tab); 0-5: that soft key's tab */
#define PX_UI_BACK  (-1)
#define PX_UI_QUIT  (-2) /* return now, as key() returning PX_KEY_QUIT */

/* The right column of a row: at most size - 1 characters. */
typedef void (*px_ui_value_fn)(int item, char *buf, int size, void *ctx);

typedef struct {
    const char *title;
    const char *const *items;
    int count;
    px_ui_value_fn value; /* NULL: no right column */
    void *ctx;
    const char *empty;    /* shown with no items */
    const char *tabs[6];
    int radio;            /* 1: radio buttons, the one at `mark` on; ENTER or the first tab moves it there */
    int mark;
    int selected;         /* the highlighted row, in and out */
} px_ui_list_t;

/* text(): which characters typing gives. Letters are the keys' own
 * letters; SHIFT then a digit key types the digit. */
#define PX_UI_CHARS_NAME  0 /* A-Z 0-9 - : a file name */
#define PX_UI_CHARS_LABEL 1 /* A-Z 0-9: a program label */
#define PX_UI_CHARS_TEXT  2 /* also the space and : , ? _ */

/* time(): what it holds. */
#define PX_UI_TIME_SECONDS  1 /* hours, minutes and seconds (else no seconds) */
#define PX_UI_TIME_12H      2 /* 1-12 and AM / PM */
#define PX_UI_TIME_DURATION 4 /* a length of time: 0-9999 hours, no AM / PM */

/* ui_title() and ui_tabs() leave the rows from PX_UI_TOP to PX_UI_BOTTOM - 1. */
#define PX_UI_TOP    13
#define PX_UI_BOTTOM 108

/* ui_dialog()'s icons. */
#define PX_UI_ICON_NONE    0
#define PX_UI_ICON_STOP    1 /* it can't be done, or it can't be undone */
#define PX_UI_ICON_NOTE    2 /* for information */
#define PX_UI_ICON_CAUTION 3 /* think first */

/* form(): its fields. */
#define PX_UI_CHOICE 0 /* *value: which of items; ENTER pulls the list down */
#define PX_UI_TOGGLE 1 /* *value: 0 or 1, a check box; ENTER flips it */
#define PX_UI_TEXT   2 /* text, at most max characters (PX_UI_CHARS_* in flags) */
#define PX_UI_DATE   3 /* value[0..2]: year, month, day; the calendar */
#define PX_UI_TIME   4 /* value[0..2]: hours, minutes, seconds; PX_UI_TIME_* in flags */
#define PX_UI_NUMBER 5 /* *value, from min to max */
#define PX_UI_BUTTON 6 /* ENTER ends the form: it returns PX_UI_ENTER, the row in selected */

typedef struct {
    const char *label;
    int kind;             /* PX_UI_CHOICE ... */
    int hidden;           /* 1: not shown */
    int *value;
    const char *const *items; /* PX_UI_CHOICE */
    int count;
    char *text;           /* PX_UI_TEXT: max + 1 bytes */
    int min, max;         /* PX_UI_NUMBER's range; PX_UI_TEXT's length */
    int flags;
    const char *shown;    /* shown in place of the value (NULL: the value itself) */
} px_ui_field_t;

typedef struct px_ui_form {
    const char *title;
    px_ui_field_t *fields;
    int count;
    const char *tabs[6];  /* e.g. SAVE: a tab ends the form (▲ ▼ EXIT come by themselves) */
    /* After a field changed, before the form is drawn again: it may change
     * the fields (hide one, show what was chosen, open another screen).
     * NULL: none. Returning PX_UI_QUIT ends the form. */
    int (*changed)(struct px_ui_form *form, int field, void *ctx);
    void *ctx;
    int selected;         /* the highlighted field, in and out */
} px_ui_form_t;

typedef struct px_api {
    uint32_t version; /* the firmware's PX_APP_API_VERSION */
    uint8_t *screen;

    /* ----- the screen ----- Off-screen parts are left out. */
    void (*show)(void); /* the screen to the panel, about 10 ms */
    void (*clear)(void);
    void (*pixel)(int x, int y, int black);
    int (*get_pixel)(int x, int y); /* 1 = black; 0 off the screen */
    void (*fill)(int x, int y, int w, int h, int black);
    void (*line)(int x0, int y0, int x1, int y1, int black);
    /* A 1-bit picture (the screen's layout: rows of stride bytes, top bit
     * leftmost): its 1 bits black, its 0 bits left as they are. */
    void (*bitmap)(int x, int y, int w, int h, const uint8_t *bits, int stride);
    /* Black text with its top left at (x, y), or white on black (each
     * character's whole cell); returns the x after it. */
    int (*text)(int x, int y, const char *s, int font, int inverse);
    int (*text_width)(const char *s, int font);
    int (*font_height)(int font);

    /* ----- keys ----- The next key event (above), waiting at most wait_ms
     * for it (< 0: as long as it takes). Asleep while waiting: on battery,
     * waiting here between frames keeps a game's current low. */
    int (*key)(int wait_ms);

    /* ----- time ----- */
    uint32_t (*millis)(void); /* since the calculator started */
    /* Asleep for ms, as key() waits, but leaving key events for key(): a
     * key pressed and let go within it is missed. */
    void (*sleep)(int ms);
    void (*datetime)(px_datetime_t *now); /* the calculator's clock */
    uint32_t (*random)(void);

    /* ----- sound -----
     * The speaker plays one sound at a time: starting one stops the one
     * playing. tone() and play() return when it has played; the _async
     * ones as soon as it starts, and the app goes on meanwhile (the second
     * processor plays it). A sound still playing stops when the app ends. */
    void (*tone)(int tone); /* TONE 0-9 (/SOUNDS/TONES), 10 = the error sound */
    /* A WAV file (PCM 8-32 bit or float, 1-8 channels, 4-192 kHz): 0, or -1
     * if it can't be played. */
    int (*play)(const char *path);
    void (*tone_async)(int tone);
    int (*play_async)(const char *path);
    int (*sound_playing)(void); /* 1 while one plays */
    void (*sound_stop)(void);

    /* ----- files (above) ----- */
    /* The whole file into data: the bytes read (at most size), or -1. */
    int (*load)(const char *path, void *data, int size);
    /* data as the whole file: 0, or -1. */
    int (*save)(const char *path, const void *data, int size);
    /* mode "r" or "w" (a new, empty file): a number for the calls below,
     * or -1. */
    int (*open)(const char *path, const char *mode);
    int (*read)(int file, void *data, int size);        /* bytes read, 0 at the end, -1 */
    int (*write)(int file, const void *data, int size); /* bytes written, -1 */
    int (*close)(int file);                             /* 0, or -1 if what was written is lost */
    int (*remove)(const char *path);                    /* a file or an empty folder: 0 or -1 */
    int (*mkdir)(const char *path);                     /* 0, also if it's there; -1 */
    /* The files in folder dir ("" = the app's) whose names end in ext
     * (".ir"; "" = all), sorted, without ext. Returns how many (<= max). */
    int (*list)(const char *dir, const char *ext, char (*names)[PX_FILE_NAME_MAX + 1], int max);

    /* ----- serial: the USB console ("PX-42F Console") ----- printf() goes
     * here too. Nothing is sent without a computer listening. */
    int (*serial_write)(const void *data, int size);
    int (*serial_read)(void *data, int size); /* what has come in, at most size; 0 if nothing */

    /* ----- infrared ----- Sends one transmission (above) on the LED;
     * returns when it's out. A count outside 2..PX_IR_MAX_TIMINGS or a
     * carrier outside 10-100 kHz sends nothing. */
    void (*ir_send)(const px_ir_burst_t *burst);

    /* ----- power ----- */
    int (*battery_mv)(void);
    int (*usb_connected)(void); /* 1 = on a computer's USB (charging, no Sleep Timer) */

    /* ----- decimal numbers (above) ----- version 2 */
    /* r = a op b, or op(a): 0, or -1 for an op this firmware doesn't have
     * (r untouched). r may be a or b. */
    int (*dec)(int op, px_dec_t *r, const px_dec_t *a, const px_dec_t *b);
    void (*dec_from_int)(px_dec_t *r, int32_t v);
    /* Toward 0: 0, or -1 if it's out of range or not a number. */
    int (*dec_to_int)(int32_t *r, const px_dec_t *a);
    /* "-1.5E-3", "12", ".5": 0, or -1 if s isn't a number (r is NaN). */
    int (*dec_from_string)(px_dec_t *r, const char *s);
    /* The exact value as the library writes it: a sign, every digit, then
     * the exponent -- "+15E-4" is 0.0015; "+Inf", "+NaN". */
    void (*dec_to_string)(char buf[PX_DEC_STRING_MAX], const px_dec_t *a);
    /* -1 (a < b), 0 or 1; 2 if either is NaN. */
    int (*dec_cmp)(const px_dec_t *a, const px_dec_t *b);
    int (*dec_class)(const px_dec_t *a);

    /* ----- version 3 ----- */
    /* on_is_key 1: a short press of ON comes to key() as PX_KEY_ON (down,
     * then up) instead of ending the app, which then needs a way out of
     * its own -- returning. Held 2 seconds, ON still turns the calculator
     * off (PX_KEY_QUIT), and the Sleep Timer and an empty battery still
     * end the app. 0 (as every app starts): ON ends it, after the firmware
     * asks "Quit NAME?". */
    void (*on_key)(int on_is_key);

    /* ----- version 4: MIDI -----
     * Plays a Standard MIDI File (.mid, format 0 or 1, up to 128 KB) on the
     * speaker -- the firmware's synthesizer: 24 notes at once, 16 channels,
     * channel 10 the drums -- and returns at once, like play_async(): the
     * app goes on, sound_playing() says when it's over and sound_stop()
     * stops it, as with any sound. Songs live in /MIDI. 0, or
     * PX_MIDI_*. */
    int (*midi_play)(const char *path);
    void (*midi_status)(px_midi_status_t *status);

    /* ----- version 5: shapes -----
     * t is the thickness: 0 filled, 1 one pixel, n pixels. box() and
     * ellipse() grow inward, keeping their outer size; a circle is an
     * ellipse with rx = ry. A thick line and a polygon's edges are centred
     * on the line, with round ends and corners; thick_line() takes t 0 as 1.
     * polygon() closes itself; where it crosses itself, a filled one has
     * holes. Above 64, t counts as 64 in lines and polygons. */
    void (*box)(int x, int y, int w, int h, int t, int black);
    void (*ellipse)(int x, int y, int rx, int ry, int t, int black);
    void (*thick_line)(int x0, int y0, int x1, int y1, int t, int black);
    void (*polygon)(const px_point_t *points, int n, int t, int black);

    /* ----- version 6: alarms -----
     * The calculator's own alarms (the Time Module's; Settings -> Alarms
     * lists them): when one comes, the calculator shows its text and rings
     * until a key is pressed or 20 s pass -- also when it's off, turning on
     * for it and off again after. Alarms ring outside apps only: one that
     * comes while an app runs rings when the app ends. An app's alarms
     * stay its own: alarm_clear() takes back every one this app set (by
     * its name) and no others. The table holds 32, the calculator's own
     * alarms counted. Written to the drive at OFF, like the calculator's.
     *
     * when: the clock's date and time to the second (ms and weekday
     * unused). text: the characters text() draws (the HP-42S's), 1 to
     * PX_ALARM_TEXT_MAX, not starting with 94 (the up arrow: that runs a
     * program). 0, or -1 if the table is full, `when` isn't a date or has
     * gone, or the text can't be. */
    int (*alarm_set)(const px_datetime_t *when, const char *text);
    int (*alarm_clear)(void); /* how many went */

    /* ----- version 7: printing -----
     * To the printer Settings -> Printer chooses, as Plus42 prints: the HP
     * 82240B by infrared, or the second USB serial port, "PX-42F Printer",
     * in a terminal on the computer. Text is the characters text() draws
     * (the HP-42S's); the USB port gets them as UTF-8, a line a print.
     * On the HP 82240B a line holds PX_PRINT_COLUMNS characters, and a
     * longer one goes on to the next. The printer can't answer, so each
     * line is reckoned to take DELAY's time (1.8 s, Plus42's DELAY), and
     * print_text() waits while the printer's buffer is full: print_ready()
     * says whether it would, so an app can read its keys meanwhile. */
    int (*printer)(void);     /* PX_PRINTER_* */
    int (*print_ready)(void); /* 1: print_text() would send at once (always, but on the HP 82240B
                               * and the thermal printer while it's being found or is busy) */
    /* 0, or -1 if nothing prints (PX_PRINTER_OFF). */
    int (*print_text)(const char *text, int length);
    /* The screen. On the HP 82240B turned a quarter (the paper is 166 dots
     * across), 8 of its columns a line: 32 lines, about a minute; stop() is
     * asked while it waits and between lines (NULL: never) -- a line once
     * begun goes out whole. On the USB port, Unicode blocks, 2 x 2 pixels a
     * character. 0 printed, 1 stopped, -1 if nothing prints. */
    int (*print_screen)(int (*stop)(void));

    /* ----- version 8: sprites -----
     * A 1-bit picture as bitmap() takes it, 8 pixels at a time (bitmap()
     * goes pixel by pixel). mask: the same size and stride, 1 = the
     * sprite's pixel, 0 = leave the screen as it is (NULL: the whole
     * rectangle). mode PX_SPRITE_*, flags PX_SPRITE_FLIP_* (0: as drawn):
     * a figure facing right is also the one facing left. */
    void (*sprite)(int x, int y, int w, int h, const uint8_t *bits, const uint8_t *mask, int stride,
                   int mode, int flags);

    /* ----- version 10: the standard screens (px_ui_*, above) ----- */
    /* lines of text, and tabs (NULL: none; ENTER and <- still answer). */
    int (*ui_message)(const char *title, const char *const *lines, int count, const char *const *tabs);
    int (*ui_list)(px_ui_list_t *list);
    int (*ui_form)(px_ui_form_t *form);
    /* A list of choices that opens under the rows y to y + h - 1 of what's
     * on the screen (over them if there's no room below), at the right:
     * the rest stays drawn. *choice in and out; ENTER chooses. */
    int (*ui_pulldown)(int y, int h, const char *const *items, int count, int *choice);
    int (*ui_text)(const char *title, const char *prompt, char *text, int max, int chars);
    int (*ui_number)(const char *title, const char *prompt, int *value, int min, int max);
    /* A month's calendar: the arrows move a day or a week, the tabs a month or a year. */
    int (*ui_date)(const char *title, int *year, int *month, int *day);
    int (*ui_time)(const char *title, const char *prompt, int *hours, int *minutes, int *seconds, int flags);
    /* For a screen of the app's own with the same look: the title bar
     * (right: its right end, NULL: the time) and the tabs (6, "" or NULL
     * none). They only draw: show() puts them up, and the tabs' keys come
     * from key() as PX_KEY(0, 0..5). The app's own drawing goes between,
     * from PX_UI_TOP to PX_UI_BOTTOM - 1. */
    void (*ui_title)(const char *title, const char *right);
    void (*ui_tabs)(const char *const *tabs);
    /* Long text to read: wrapped at its spaces (\n starts a line), 7 lines
     * of 30 characters at a time, the page (1/3) in the title bar. The
     * arrows and the ▲ ▼ tabs move a page (SHIFT first: to the start or
     * the end), EXIT returns PX_UI_BACK. The text isn't copied. */
    int (*ui_view)(const char *title, const char *text);
    /* An alert, as the classic black-and-white desktops drew one: the
     * screen dimmed, a box with a double border, an icon (PX_UI_ICON_*),
     * the text (4 lines at most, wrapped; \n starts a line) and two
     * buttons at the bottom right: left, the default (ringed; NULL:
     * "ENTER"), and right (NULL: none). The arrows move the ring, ENTER
     * presses the ringed one; F1 is the left, <- (or F6, and EXIT after
     * on_key(1)) the right. The left returns PX_UI_ENTER, the right
     * PX_UI_BACK. The screen under it comes back by itself, and millis()
     * stands still while it's up (a game doesn't jump on). */
    int (*ui_dialog)(int icon, const char *text, const char *left, const char *right);
    /* ----- version 11 ----- ui_dialog() above has its icon and two buttons
     * from here on (a version 10 firmware's took the text only). */
    /* ui_view() with `leave` for the tab that goes back (NULL: "EXIT"):
     * "BACK" for help that returns to the app's own screen. */
    int (*ui_view_leave)(const char *title, const char *text, const char *leave);
} px_api_t;

#ifdef __cplusplus
}
#endif

#endif
