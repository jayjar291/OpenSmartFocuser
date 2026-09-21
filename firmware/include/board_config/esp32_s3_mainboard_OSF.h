#pragma once

/*
 * TMC2209 stepper driver control and single-wire UART pins.
 */
#define PIN_TMC_STEP                5
#define PIN_TMC_DIR                 6
#define PIN_TMC_ENABLE              7
#define PIN_TMC_UART_TX             8
#define PIN_TMC_UART_RX             9

/*
 * ST7789 display SPI and control pins.
 */
#define PIN_LCD_CS                  10
#define PIN_LCD_MOSI                11   
#define PIN_LCD_SCK                 12
#define PIN_LCD_DC                  13
#define PIN_LCD_RST                 14
#define PIN_LCD_BL                  15
#define PIN_LCD_MISO                -1

/*
 * On-board/status LED output.
 */
#define PIN_LED                     48

/*
 * Physical button inputs used by menu navigation and control.
 */
#define PIN_BUTTON_UP               16
#define PIN_BUTTON_DOWN             18
#define PIN_BUTTON_SELECT           17
#define PIN_BUTTON_ENDSTOP          21
#define PIN_BUTTON_BOOT              0


//addon pins definitions

/*
* Shutter servo control pin.
*/
#define PIN_SHUTTER_SERVO           1

/*
* Flat frame panel control pin.
*/
#define PIN_FLAT_FRAME_PANEL        2
