#pragma once
#include "config.h"

/*
 * TFT_eSPI driver selection for the connected ST7789 panel.
 */
#define ST7789_DRIVER

/*
 * Physical panel geometry and controller offset behavior.
 */
#define TFT_WIDTH 135
#define TFT_HEIGHT 240
#define CGRAM_OFFSET

/*
 * Display signal mapping to the active board pin definitions.
 */
#define TFT_MOSI PIN_LCD_MOSI
#define TFT_SCLK PIN_LCD_SCK
#define TFT_CS   PIN_LCD_CS
#define TFT_DC   PIN_LCD_DC
#define TFT_RST  PIN_LCD_RST
#define TOUCH_CS -1

/*
 * Font set selection to balance features and flash usage.
 */
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

/*
 * SPI bus speeds for display write/read operations.
 */
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000