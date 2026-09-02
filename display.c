#include "display.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

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

static void display_write_command(uint8_t cmd) {
    /* With no CS pin, RS's state at the SPI clock edges is the only thing
     * telling the panel command from data -- settle time on both sides of
     * the transition matters. Timing matches the working AVR driver. */
    gpio_put(DISP_RS_PIN, 0); /* command mode */
    sleep_us(10);
    spi_write_blocking(DISP_SPI, &cmd, 1);
    sleep_us(50);
    gpio_put(DISP_RS_PIN, 1); /* back to data mode for any following bytes */
}

static void display_write_data(const uint8_t *data, size_t len) {
    gpio_put(DISP_RS_PIN, 1); /* data mode */
    spi_write_blocking(DISP_SPI, data, len);
}

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
        0x02, 0xB2, 0x02,                                           /* Frame Rate Control: LPM 1Hz, HPM 16Hz */
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
        /* Start in Low Power Mode (1Hz) -- display_flush() switches to
         * High Power Mode (0x38, 16Hz) on demand for the brief window
         * needed to cleanly show new content, then display_tick() drops
         * back to LPM once idle. See the disp_high_power state machine
         * below for why: with LPM, SPI transfer speed only affects how
         * fast we can WRITE into the panel's GRAM, not how often it
         * actually scans that GRAM out to the visible screen, so rows
         * written on either side of a 1Hz refresh boundary visibly update
         * up to ~1s apart -- that's what looked like "not displayed at
         * the same time" before HPM was introduced. */
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
        display_write_command(*seq++);
        if (len > 1)
            display_write_data(seq, len - 1);
        seq += len - 1;
    }
}

void display_init(void) {
    /* Raised from the original 1 MHz bring-up value now that the driver is
     * proven stable -- 8, 16, and 32 MHz all confirmed clean, now probing
     * higher. With no CS line, RS's state at the clock edges is the only
     * framing (see display_write_command()) -- there's no hardware margin
     * for a signal-integrity glitch on the wiring to be masked, so if the
     * display starts showing corruption/garbage at this speed, that's the
     * real ceiling for the current wiring; come back down to 32 MHz. Note
     * this is also approaching the RP2350 SPI peripheral's own practical
     * ceiling (a fraction of the ~150 MHz system clock), separate from
     * whatever the wiring can tolerate -- spi_init() returns the actual
     * achieved rate, which may not land exactly on the requested value
     * once the clock divider can't hit it precisely. */
    uint actual_baud = spi_init(DISP_SPI, 64 * 1000 * 1000); /* 64 MHz requested */
    printf("Display SPI: requested 64 MHz, got %u Hz\n", actual_baud);
    /* Pin down the format explicitly rather than rely on spi_init()'s
     * default -- confirmed to match the AVR reference (SPI_MODE_0_gc,
     * i.e. CPOL=0/CPHA=0), MSB first. With no CS line at all, the panel
     * has zero margin for a bit-order/polarity mismatch to be masked. */
    spi_set_format(DISP_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(DISP_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(DISP_MOSI_PIN, GPIO_FUNC_SPI);

    gpio_init(DISP_RS_PIN);
    gpio_set_dir(DISP_RS_PIN, GPIO_OUT);

    gpio_init(DISP_RST_PIN);
    gpio_set_dir(DISP_RST_PIN, GPIO_OUT);
    gpio_put(DISP_RST_PIN, 1);

    display_reset();

    display_run_init_sequence(st7305_init_sequence);
}

/* Canonical framebuffer: plain row-major 1bpp, hardware-independent. One
 * bit per pixel, MSB-first within each row byte, no relation to the
 * ST7305's tile memory layout -- that mapping lives entirely in
 * display_flush() (see pack_tile_byte() below), so drawing code here never
 * needs to know about it. This is what makes display_set_pixel() (and
 * everything built on it) work at any (x, y), not just positions aligned
 * to some hardware block size. */
#define FB_STRIDE ((DISPLAY_WIDTH + 7) / 8) /* 32 bytes/row */
static uint8_t disp_fb[DISPLAY_HEIGHT][FB_STRIDE];

void display_clear(void) {
    memset(disp_fb, 0, sizeof(disp_fb));
}

void display_fill(uint8_t value) {
    memset(disp_fb, value, sizeof(disp_fb));
}

void display_set_pixel(uint8_t x, uint8_t y, bool on) {
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT)
        return;
    uint8_t mask = (uint8_t) (0x80 >> (x & 7));
    if (on)
        disp_fb[y][x >> 3] |= mask;
    else
        disp_fb[y][x >> 3] &= (uint8_t) ~mask;
}

/* Bounds-checked read, used only by the flush-time tile conversion (rows/
 * columns near the panel's bottom/right edge legitimately fall outside the
 * buffer -- see pack_tile_byte()) -- reads as "off" rather than needing a
 * separate edge case there. */
static bool display_get_pixel(int x, int y) {
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT)
        return false;
    return (disp_fb[y][x >> 3] & (0x80 >> (x & 7))) != 0;
}

uint8_t display_draw_char(const display_font_t *font, char c, uint8_t x, uint8_t y, uint8_t color) {
    if ((uint8_t) c < font->first_char || (uint8_t) c > font->last_char)
        return (uint8_t) (x + font->width + 1); /* not in the font -- skip but still advance */

    const uint8_t *glyph = font->data + ((uint8_t) c - font->first_char) * font->bytes_per_char;
    bool invert = color != 0;
    int bit_index = 0;

    for (uint8_t row = 0; row < font->height; row++) {
        for (uint8_t col = 0; col < font->width; col++) {
            uint8_t byte = glyph[bit_index >> 3];
            bool set = ((byte >> (7 - (bit_index & 7))) & 1) != 0;
            bit_index++;

            /* Compute the target position in a wider type before handing
             * it to display_set_pixel(): x/y are uint8_t, and near the
             * right/bottom edge x + col can exceed 255 (e.g. x=249,
             * col=15), which would silently wrap back on-screen instead
             * of clipping if added directly in uint8_t. */
            int px = (int) x + col;
            int py = (int) y + row;
            if (px >= 0 && px < DISPLAY_WIDTH && py >= 0 && py < DISPLAY_HEIGHT)
                display_set_pixel((uint8_t) px, (uint8_t) py, set != invert);
        }
    }

    return (uint8_t) (x + font->width + 1);
}

uint8_t display_draw_string(const display_font_t *font, const char *s, uint8_t x, uint8_t y, uint8_t color) {
    while (*s)
        x = display_draw_char(font, *s++, x, y, color);
    return x;
}

void display_draw_dotted_hline(uint8_t x0, uint8_t x1, uint8_t y) {
    for (int x = x0; x <= (int) x1; x += 2)
        display_set_pixel((uint8_t) x, y, true);
}

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

    display_write_command(0x2A); /* CASET: our block-row axis, +25 offset */
    display_write_data(caset, sizeof(caset));

    display_write_command(0x2B); /* RASET: our block-column axis, 0-based */
    display_write_data(raset, sizeof(raset));
}

/* Power-mode state machine: the panel starts in Low Power Mode (1Hz
 * refresh, set by the init sequence) to save power at rest. display_flush()
 * wakes it to High Power Mode (16Hz) on demand so new content shows up
 * promptly and uniformly across all rows, and display_tick() drops it back
 * to LPM once it's been idle for a bit. DISP_HPM_IDLE_MS must be at least
 * one full HPM refresh period (1/16Hz = 62.5ms) -- dropping back to LPM
 * before the panel has actually scanned out the new GRAM contents at least
 * once would reintroduce the same up-to-1s staggered-update problem HPM
 * was added to fix, just shifted to whichever rows lost the race back to
 * LPM. 150ms gives that a couple of refresh cycles of margin. */
#define DISP_HPM_IDLE_MS 150

static bool disp_high_power = false; /* matches the init sequence's LPM start state */
static absolute_time_t disp_last_flush_time;

void display_flush(void) {
    if (!disp_high_power) {
        display_write_command(0x38); /* High Power Mode: 16Hz refresh */
        disp_high_power = true;
    }

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

        display_write_command(0x2C); /* RAMWR: memory write */
        display_write_data((const uint8_t *) chunk, (size_t) rows * TILE_COLS * 3);
    }

    disp_last_flush_time = get_absolute_time();
}

void display_tick(void) {
    if (!disp_high_power)
        return;
    if (absolute_time_diff_us(disp_last_flush_time, get_absolute_time()) / 1000 >= DISP_HPM_IDLE_MS) {
        display_write_command(0x39); /* Low Power Mode: 1Hz refresh, saves power while idle */
        disp_high_power = false;
    }
}
