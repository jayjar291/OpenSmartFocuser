#pragma once

// Set to -1 for signals that are not connected on your build.

// --- TMC2209 Stepper Driver (STEP/DIR + UART) ---
#define PIN_TMC_STEP                4
#define PIN_TMC_DIR                 5
#define PIN_TMC_ENABLE              6
// TMC2209 PDN_UART is a single-wire bus. MCU TX goes through a resistor to
// the PDN line and MCU RX senses the same PDN line directly.
#define PIN_TMC_UART_TX             15
#define PIN_TMC_UART_RX             16

// --- ST7789V LCD (SPI) ---
#define PIN_LCD_SCK                 12
#define PIN_LCD_MOSI                11
#define PIN_LCD_MISO                -1
#define PIN_LCD_CS                  10
#define PIN_LCD_DC                  9
#define PIN_LCD_RST                 8
#define PIN_LCD_BL                  17

// --- Buttons (UP / DOWN / SELECT / ENDSTOP) ---
#define PIN_BUTTON_UP               35
#define PIN_BUTTON_DOWN             36
#define PIN_BUTTON_SELECT           37
#define PIN_BUTTON_ENDSTOP          38
