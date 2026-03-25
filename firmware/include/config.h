#pragma once

#include "board_config/esp32_s3_devkitc1_pins.h"

/*
 * TFT_eSPI is configured through a custom setup header passed in build flags.
 */
#define TFT_ESPI_SETUP_HEADER "TFT_eSPI_Setup.h"

/*
 * Generic motion and UART timing configuration for the focuser driver stack.
 */
#define TMC_UART_BAUDRATE 115200
#define TMC_DRIVER_ADDRESS 0b00
#define TMC_MAX_SPEED 1000
#define TMC_MAX_ACCELERATION 7500
#define IDLE_JOG_SPEED_STEPS_PER_SEC 600

/*
 * TMC2209 electrical and mode settings used during driver initialization.
 */
#define TMC_RMS_CURRENT 700
#define TMC_MICROSTEPS 16
#define TMC_SPREAD_CYCLE true

/*
 * User-facing device defaults used by the menu/UI layer.
 */
#define DEFAULT_BRIGHTNESS 75

/*
 * Astrophotography UI theme (RGB565): red-dominant to preserve dark adaptation.
 * Tune these values to restyle the full menu system from one place.
 */
#define MENU_COLOR_BG 0x0000
#define MENU_COLOR_TEXT 0xF800
#define MENU_COLOR_SELECTION_BORDER 0xF800
#define MENU_COLOR_SELECTION_FILL 0x4800
#define MENU_COLOR_SCROLLBAR 0xC800

/*
 * Idle screen component colors (RGB565).
 */
#define IDLE_COLOR_BG MENU_COLOR_BG
#define IDLE_COLOR_TEXT MENU_COLOR_TEXT
#define IDLE_COLOR_BOTTOM_BAR 0x2000
#define IDLE_COLOR_RIGHT_BAR 0x2800
#define IDLE_COLOR_RIGHT_BAR_BORDER MENU_COLOR_SELECTION_BORDER
#define IDLE_COLOR_SPACER 0x7800

