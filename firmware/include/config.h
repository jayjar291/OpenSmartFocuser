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

