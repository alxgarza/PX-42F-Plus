#include "display.h"
#include "display_fb.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "retained.h"

static void display_reset(void) {
    /* Generic hardware reset timing for this class of controller: hold
     * RST low briefly, then wait for the controller's internal reset/
     * power-on routine to finish before sending any commands. */
    gpio_put(DISP_RST_PIN, 1);
    sleep_ms(10);
    gpio_put(DISP_RST_PIN, 0);
    sleep_ms(10);
    gpio_put(DISP_RST_PIN, 1);
    sleep_ms(120);
}

/* Writes one instruction: a command byte, optionally followed by its
 * parameter/data bytes, with CS held low for the whole instruction and
 * released between instructions -- the standard framing for this class of
 * panel. RS still distinguishes command from data on each byte the way it
 * always has (see the RS timing below, matched to the working AVR driver);
 * CS is what tells the panel where one instruction ends and the next
 * begins, so a multi-byte parameter write (e.g. CASET) must not have CS
 * bounce in the middle of it. Pass data == NULL / len == 0 for a
 * command with no parameters. */
static void display_write_instruction(uint8_t cmd, const uint8_t *data, size_t len) {
    gpio_put(DISP_CS_PIN, 0); /* select */
    sleep_us(1);

    gpio_put(DISP_RS_PIN, 0); /* command mode */
    sleep_us(10);
    spi_write_blocking(DISP_SPI, &cmd, 1);
    sleep_us(50);
    gpio_put(DISP_RS_PIN, 1); /* data mode, for the parameter bytes below (if any) and to leave the panel in its default state */

    if (len > 0)
        spi_write_blocking(DISP_SPI, data, len);

    sleep_us(1);
    gpio_put(DISP_CS_PIN, 1); /* deselect */
}

/* Frame Rate Control (0xB2) values, from the PX-41CX's notes on this panel:
 * 0x05 = HPM 16Hz / LPM 8Hz, 0x02 = HPM 16Hz / LPM 1Hz. The panel only ever
 * runs in LPM here, so these pick its refresh rate. */
#define FRAME_RATE_ON  0x05 /* LPM 8Hz: calculator on */
#define FRAME_RATE_OFF 0x02 /* LPM 1Hz: off, splash showing */

/* ST7305 power-on init sequence. Each entry is { length, command,
 * data... }, where length counts the command byte plus its data bytes.
 * length == 0xFF means "delay ~100ms"; length == 0x00 ends the sequence.
 * Values as used in the working AVR driver for this panel. */
static const uint8_t st7305_init_sequence[] = {
        0x03, 0xD6, 0x17, 0x02,                                     /* NVM Load Control */
        0x02, 0xD1, 0x01,                                           /* Booster Enable */
        0x03, 0xC0, 0x08, 0x0A,                                     /* Gate Voltage Setting: VGH 12V, VGL -10V */
        0x05, 0xC1, 0x19, 0x19, 0x19, 0x19,                         /* VSHP Setting 4.8V */
        0x05, 0xC2, 0x31, 0x31, 0x31, 0x31,                         /* VSLP Setting 0.98V */
        0x05, 0xC4, 0x19, 0x19, 0x19, 0x19,                         /* VSHN Setting -3.6V */
        0x05, 0xC5, 0x27, 0x27, 0x27, 0x27,                         /* VSLN Setting 0.22V */
        0x02, 0xB2, FRAME_RATE_ON,                                  /* Frame Rate Control: LPM 8Hz (HPM 16Hz, unused) */
        0x09, 0xB4, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45, /* Update Period Gate EQ Control (LPM) */
        0x02, 0xB7, 0x13,                                           /* Source EQ Enable */
        0x02, 0xB0, 0x3F,                                           /* Gate Line Setting: 252 lines */
        0x02, 0xC9, 0x00,                                           /* Source Voltage Select */
        0x01, 0x11,                                                 /* Sleep out */
        0xFF,                                                       /* delay */
        0x02, 0x36, 0x20,                                           /* Memory Data Access Control: MX=0, DO=0 */
        0x02, 0x3A, 0x11,                                           /* Data Format Select: 3 writes / 24 bits */
        0x02, 0xB9, 0x20,                                           /* Gamma Mode Setting: mono */
        0x02, 0xB8, 0x29,                                           /* Panel Setting: 1-dot/frame inversion, one-line interlace */
        0x02, 0xD0, 0xFF,                                           /* Auto power down: on */
        /* Low Power Mode, permanently -- same as the PX-41CX, whose ST7305
         * driver this init sequence comes from. High Power Mode's 16Hz drive
         * makes the image visibly blink, and switching between the modes
         * flashes the whole panel, so the mode never changes; only LPM's
         * frame rate does (0xB2 above, display_set_slow_refresh()): 8Hz while
         * the calculator is on, so a redraw shows within ~125ms, and 1Hz
         * while it's off. */
        0x01, 0x39,                                                 /* Low Power Mode */
        0x01, 0x29,                                                 /* Display on */
        0xFF,                                                       /* delay */
        0x00                                                        /* end */
};

static void display_run_init_sequence(const uint8_t *seq) {
    uint8_t len;
    while ((len = *seq++) != 0x00) {
        if (len == 0xFF) {
            sleep_ms(100);
            continue;
        }
        uint8_t cmd = *seq++;
        display_write_instruction(cmd, len > 1 ? seq : NULL, len > 1 ? len - 1 : 0);
        seq += len - 1;
    }
}

/* A control line driven high, without a low glitch on the way: the level
 * and direction are set while the pin is still not under software control
 * -- after a deep power-off, its pad is still isolated, holding the level
 * from before, and selecting the function is what ends that. RST must
 * never dip (it'd reset the panel and lose the splash image); the default
 * pull-down would waste ~60uA against the high level. */
static void output_high(uint pin) {
    gpio_put(pin, 1);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_disable_pulls(pin);
    gpio_set_function(pin, GPIO_FUNC_SIO);
}

static void setup_bus(void) {
    /* Backed down from 64 MHz: that was an experimental probe past the
     * last *confirmed*-clean speed, and the display going blank is
     * exactly the failure this comment predicted. That test predates CS
     * being wired up (it was RS-only framing at the time), so CS now
     * gives the panel a clean instruction boundary that wasn't there
     * before -- but 64 MHz hasn't been re-tried since, so still requesting
     * the last confirmed-clean speed here rather than assuming CS buys
     * back the headroom. 32 MHz is the highest speed actually confirmed
     * clean (8 and 16 also fine) -- raised from the original 1 MHz
     * bring-up value once the driver was proven stable. If blanking
     * recurs even at 32 MHz, that points at the wiring itself (length,
     * routing, connector) rather than the clock rate.
     *
     * Now 24 MHz: the peripheral clock is a fixed 48 MHz (cpuclock.h),
     * and the SPI clock is at most half of it. (At the old 150 MHz
     * peripheral clock, a 32 MHz request actually got 25 MHz.) */
    spi_init(DISP_SPI, 24 * 1000 * 1000);
    /* Pin down the format explicitly rather than rely on spi_init()'s
     * default -- confirmed to match the AVR reference (SPI_MODE_0_gc,
     * i.e. CPOL=0/CPHA=0), MSB first. */
    spi_set_format(DISP_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(DISP_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(DISP_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_disable_pulls(DISP_SCK_PIN);
    gpio_disable_pulls(DISP_MOSI_PIN);

    /* RS, CS and RST: all idle high. CS deselected before anything else
     * touches the bus -- SPI0's SCK/MOSI are shared with no other device
     * here, but CS floating low between now and display_reset() below
     * could otherwise latch the reset pulse as a stray instruction. */
    output_high(DISP_RS_PIN);
    output_high(DISP_CS_PIN);
    output_high(DISP_RST_PIN);
}

void display_init(void) {
    setup_bus();
    display_reset();
    display_run_init_sequence(st7305_init_sequence);
}

void display_attach(void) {
    setup_bus();
}

/* In RAM that survives a deep power-off (retained.h): a wake just to
 * update the splash's clock redraws only that, over the image still here.
 * The drawing into it is display_draw.c's. */
uint8_t (*const display_fb)[DISPLAY_STRIDE] = (uint8_t (*)[DISPLAY_STRIDE]) RETAINED_DISPLAY_FB;
_Static_assert(RETAINED_DISPLAY_FB + FB_SIZE <= RETAINED_RAMSTATE, "framebuffer overlaps the RAM state");

/* The ST7305's "3 writes / 24 bit" GRAM format (see the Data Format Select
 * command in the init sequence) packs pixels into 2-column x 12-row tiles,
 * 3 bytes each -- top/mid/bot, each byte 4 rows x 2 columns, 1 bit/pixel.
 * Confirmed bit layout for one tile byte: bit pairs 7:6, 5:4, 3:2, 1:0 are
 * rows 0-3 of that byte's 4-row group, each pair's high bit = left pixel,
 * low bit = right pixel. TILE_COLS/TILE_ROWS round up: the last tile-row's
 * bottom 10 rows fall off the bottom of a 122-row panel and are never
 * shown (display_get_pixel() reads those as "off"). This tile geometry is
 * purely a flush-time concern -- nothing above this point in the file
 * knows about it. */
#define TILE_COLS ((DISPLAY_WIDTH + 1) / 2)   /* 125 */
#define TILE_ROWS ((DISPLAY_HEIGHT + 11) / 12) /* 11 */

typedef union {
    uint8_t arr[3];
    struct {
        uint8_t top; /* rows 0-3 of the tile's 2 columns */
        uint8_t mid; /* rows 4-7 */
        uint8_t bot; /* rows 8-11 */
    };
} disp_tile_t;

static uint8_t pack_tile_byte(int col0, int row0) {
    uint8_t v = 0;
    for (int p = 0; p < 4; p++) {
        int row = row0 + p;
        if (display_get_pixel(col0, row))
            v |= (uint8_t) (1 << (7 - 2 * p));
        if (display_get_pixel(col0 + 1, row))
            v |= (uint8_t) (1 << (6 - 2 * p));
    }
    return v;
}

static void pack_tile(disp_tile_t *t, int block_col, int block_row) {
    int col0 = block_col * 2;
    int row0 = block_row * 12;
    t->top = pack_tile_byte(col0, row0);
    t->mid = pack_tile_byte(col0, row0 + 4);
    t->bot = pack_tile_byte(col0, row0 + 8);
}

/* Sets the write window for block-rows [start_row, start_row+num_rows).
 * Confirmed against the working AVR reference (buffRows_2_disp): despite
 * the command names, CASET addresses our block-ROW axis (offset by a
 * fixed +25 -- panel-specific, not derived, just matching known-good
 * values) and RASET addresses our block-COLUMN axis, 0-based, full width
 * every time. This is the opposite pairing of the generic MIPI-DBI
 * convention this code guessed at before -- that mismatch, not a timing
 * issue, was the actual cause of the corrupted output. */
static void display_set_window(uint8_t start_row, uint8_t num_rows) {
    uint8_t caset[2] = {(uint8_t) (25 + start_row), (uint8_t) (25 + start_row + num_rows)};
    uint8_t raset[2] = {0x00, TILE_COLS - 1};

    display_write_instruction(0x2A, caset, sizeof(caset)); /* CASET: our block-row axis, +25 offset */
    display_write_instruction(0x2B, raset, sizeof(raset)); /* RASET: our block-column axis, 0-based */
}

void display_flush(void) {
    /* The panel can't take the whole frame in one window/RAMWR burst --
     * the working AVR reference (buffRows_2_disp) writes 2 block-rows at
     * a time, each as its own complete CASET/RASET/RAMWR transaction, not
     * one giant window over the whole frame. Match that chunk size here.
     * The tile bytes for each chunk are converted from the canonical
     * framebuffer right before sending -- no full-frame tile buffer is
     * kept around between flushes. */
    disp_tile_t chunk[2][TILE_COLS];

    for (uint8_t start_row = 0; start_row < TILE_ROWS; start_row += 2) {
        uint8_t rows = (TILE_ROWS - start_row < 2) ? 1 : 2;

        for (uint8_t r = 0; r < rows; r++)
            for (int block_col = 0; block_col < TILE_COLS; block_col++)
                pack_tile(&chunk[r][block_col], block_col, start_row + r);

        display_set_window(start_row, rows);

        /* RAMWR: memory write */
        display_write_instruction(0x2C, (const uint8_t *) chunk, (size_t) rows * TILE_COLS * 3);
    }
}

void display_set_slow_refresh(bool slow) {
    uint8_t rate = slow ? FRAME_RATE_OFF : FRAME_RATE_ON;
    display_write_instruction(0xB2, &rate, 1); /* Frame Rate Control */
}

void display_sleep(void) {
    /* A blank frame first: the reflective panel goes on showing its last
     * picture after the controller sleeps (the splash, when its time ran
     * out). At the normal refresh rate, and given a few frames, so it's
     * really on the glass before the controller stops. */
    display_set_slow_refresh(false);
    display_clear();
    display_flush();
    sleep_ms(300);
    display_write_instruction(0x28, NULL, 0); /* Display off */
    display_write_instruction(0x10, NULL, 0); /* Sleep in */
    sleep_ms(100);
}

