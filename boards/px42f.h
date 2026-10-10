/*
 * PX-42F calculator board (KiCad: PX42_TH_V1.1).
 *
 * A custom RP2350A design, not a Pico module:
 *   - RP2350A, QFN-60 (U4), core supply from its own switching regulator
 *     (L1 3.3uH)
 *   - 12 MHz crystal, Abracon ABM8-272-T3 (Y1) -- same as the Pico boards
 *   - 16 MB QSPI flash, Winbond W25Q128JVS (U3)
 *   - PSRAM footprint (U1, APS6404L) not fitted; its chip select GP8 still
 *     has R3 (10k) to +3V3
 *   - no wireless chip, no on-board LED, no VBUS/VSYS sense pins
 *   - 3V3 from an RT9080-33 LDO (U6) fed by the battery (via Q1) or USB
 *     (via D1); battery charger TP4057 (U5); battery sense on GP28 (ADC2)
 *     through a 1M/1M divider
 *
 * Pin use (see PINOUT.html for the full table):
 *   GP0-7    keyboard rows        GP9-13   keyboard columns
 *   GP14     IR LED (via Q3)      GP15     speaker (via Q2)
 *   GP16/17  debug UART0          GP18/19  display SPI0 SCK / TX
 *   GP20-22  display RS/RST/CS    GP26/27  I2C1 (planned)
 *   GP28     battery sense
 */

#ifndef _BOARDS_PX42F_H
#define _BOARDS_PX42F_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// For board detection
#define PX42F_BOARD

// --- RP2350 VARIANT ---
#define PICO_RP2350A 1

// --- UART ---
// The debug UART (UART0 on GP16/GP17). stdio itself goes over USB.
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 16
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 17
#endif

// --- LED ---
// no PICO_DEFAULT_LED_PIN -- there is no user LED (LED1 is the charger's)
// no PICO_DEFAULT_WS2812_PIN

// --- I2C ---
// I2C1 on GP26/GP27, reserved for future use
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 1
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 26
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 27
#endif

// --- SPI ---
// SPI0 drives the ST7305 display: write-only, no MISO
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 22
#endif

// --- FLASH ---
// W25Q128JV: the W25Q080 boot settings suit the whole W25Q family
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// --- CRYSTAL ---
// 12 MHz (Y1, ABM8-272-T3) -- the SDK default, stated for completeness
#ifndef XOSC_HZ
#define XOSC_HZ 12000000u
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
