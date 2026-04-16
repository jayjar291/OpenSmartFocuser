#pragma once

#include "board_config/esp32_s3_devkitc1_pins.h"

/*
 * TFT_eSPI is configured through a custom setup header passed in build flags.
 */
#define TFT_ESPI_SETUP_HEADER "TFT_eSPI_Setup.h"

/*
 * Generic motion and UART timing configuration for the focuser driver stack.
 * System now runs in full-step mode (1 microstep) for all operations.
 */
#define TMC_UART_BAUDRATE 115200
#define TMC_DRIVER_ADDRESS 0b00
#define TMC_MAX_SPEED 30000
#define TMC_MAX_ACCELERATION 50000
// Speed settings for different focus jog modes and preset saving.
#define FINE_FOCUS_SPEED_STEPS_PER_SEC 3000
#define SLOW_FOCUS_SPEED_STEPS_PER_SEC 6000
#define MEDIUM_FOCUS_SPEED_STEPS_PER_SEC 9000
#define FAST_FOCUS_SPEED_STEPS_PER_SEC 12000
#define MAX_FOCUS_SPEED_STEPS_PER_SEC 15000
#define HOMING_SPEED_STEPS_PER_SEC 20000

/*
 * Enables framed debug text output on Serial as !{message}*.
 * Protocol responses remain unchanged.
 */
#define SERIAL_DEBUG_ENABLED true

/*
 * TMC2209 electrical and mode settings used during driver initialization.
 */
#define TMC_RMS_CURRENT 650
#define TMC_MICROSTEPS 16
#define TMC_SPREAD_CYCLE true

/*
 * Focuser travel calibration.
 * Measured: 184000 full steps (HOMING_MICROSTEPS=1) = 4.74 mm.
 * FOCUSER_STEPS_PER_MM is in runtime step units (full-step mode).
 */
#define FOCUSER_STEPS_PER_MM 38819

/*
 * Software travel limits (enforced in normal operating mode).
 */
#define FOCUSER_SOFT_MIN_MM    0
#define FOCUSER_SOFT_MAX_MM    55
#define FOCUSER_SOFT_MIN_STEPS 0
#define FOCUSER_SOFT_MAX_STEPS (FOCUSER_SOFT_MAX_MM * FOCUSER_STEPS_PER_MM)
#define FOCUSER_HOMING_RETURN_MM 1

/*
 * User-facing device defaults used by the menu/UI layer.
 */
#define DEFAULT_BRIGHTNESS 75

/*
 * Enables PSRAM initialization during startup when hardware supports it.
 * Set to true to attempt PSRAM init, false to skip.
 */
#define ENABLE_PSRAM true

/*
 * Hard-coded preset names exposed in the menu when adding a new preset.
 * Keep this list at or below preset::kMaxPresets entries.
 */
#define PRESET_NAME_OPTIONS { \
	"Fine Focus",              \
	"Clear",              \
	"Dark",              \
	"Red",              \
	"Green",              \
	"Blue",              \
	"Luminance",              \
	"Ha",              \
	"O-III",              \
	"S-II",             \
	"UV",             \
	"IR"              \
}

/*
 * Idle star-map center (J2000-like sky coordinates, degrees).
 * Right ascension wraps 0..360; declination is clamped to -90..90.
 */
#define IDLE_STAR_MAP_CENTER_RA_DEG 90.0f
#define IDLE_STAR_MAP_CENTER_DEC_DEG 20.0f

/*
 * Select which star catalog is compiled into the idle sky map.
 * Options in this repo: "stars.h", "stars_large.h", "stars_6.5.h"
 */
#define IDLE_STAR_CATALOG_HEADER "stars_6.5.h"

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

