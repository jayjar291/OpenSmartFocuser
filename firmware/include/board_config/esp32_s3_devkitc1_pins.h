#pragma once

/*
 * TMC2209 stepper driver control and single-wire UART pins.
 */
#define PIN_TMC_STEP                4
#define PIN_TMC_DIR                 5
#define PIN_TMC_ENABLE              6
#define PIN_TMC_UART_TX             15
#define PIN_TMC_UART_RX             16

/*
 * ST7789 display SPI and control pins.
 */
#define PIN_LCD_SCK                 12
#define PIN_LCD_MOSI                11
#define PIN_LCD_MISO                -1
#define PIN_LCD_CS                  10
#define PIN_LCD_DC                  9
#define PIN_LCD_RST                 8
#define PIN_LCD_BL                  17

/*
 * On-board/status LED output.
 */
#define PIN_LED                     48

/*
 * Physical button inputs used by menu navigation and control.
 */
#define PIN_BUTTON_UP               35
#define PIN_BUTTON_DOWN             36
#define PIN_BUTTON_SELECT           37
#define PIN_BUTTON_ENDSTOP          38
#define PIN_BUTTON_BOOT              0