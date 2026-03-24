#pragma once

// --- Board Pinout ---
#include "board_config/esp32_s3_devkitc1_pins.h"

// --- TMC2209 Driver Configuration ---
#define TMC_UART_BAUDRATE           115200
#define TMC_RMS_CURRENT             700    // mA (typical range 200-1500)
#define TMC_MICROSTEPS              16     // 16 = finest resolution
#define TMC_STEALTHCHOP_ENABLED     0      // Silent mode
#define TMC_COOLSTEP_ENABLED        0      // Disable for focuser (not needed)

// --- Motor Movement ---
#define MOTOR_STEPS_PER_REV         200    // NEMA 17 standard
#define MOTOR_MIN_SPEED_HZ          10     // Minimum step frequency
#define MOTOR_MAX_SPEED_HZ          1000   // Maximum step frequency
#define MOTOR_ACCEL_HZ_PER_SEC      500    // Acceleration rate
#define MOTOR_DECEL_HZ_PER_SEC      500    // Deceleration rate

// --- Control Behavior ---
#define BUTTON_DEBOUNCE_MS          15     // Milliseconds
#define MOTOR_PULSE_WIDTH_US        5      // STEP pulse width in microseconds
#define MOVING_TIMEOUT_MS           3000   // Stop if no command for this long

