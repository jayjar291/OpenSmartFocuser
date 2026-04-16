#include <Arduino.h>
#include <FastAccelStepper.h>
#include <TMCStepper.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "debug_serial.h"
#include "menu.h"
#include "movement.h"
#include "serial_command_handler.h"
#include "preset.h"

/*
 * Hardware interface constants for TMC2209 UART control.
 */
static constexpr float TMC_R_SENSE = 0.11f;
static constexpr uint8_t TMC_STEPPER_UART_INDEX = 1;
// globlal variables for idle star map center coordinates
float idleMapCenterRaDeg = IDLE_STAR_MAP_CENTER_RA_DEG;
float idleMapCenterDecDeg = IDLE_STAR_MAP_CENTER_DEC_DEG;

/*
 * Global hardware driver instances used during initialization and runtime.
 */
HardwareSerial tmcSerial(TMC_STEPPER_UART_INDEX);
FastAccelStepperEngine stepperEngine;
FastAccelStepper* focuserStepper = nullptr;
TMC2209Stepper focuserDriver(&tmcSerial, TMC_R_SENSE, TMC_DRIVER_ADDRESS);

/*
 * Global motor enable state controlled from the main menu.
 */
bool motorEnabled = false;

static TaskHandle_t motorTaskHandle = nullptr;

static void motorTask(void* /*param*/) {
  for (;;) {
    Movement::updateHoming();
    Movement::updateSoftEndstops();
    Movement::updatePositionPersistence();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

/*
 * One-time startup sequence for serial logging, UI initialization,
 * and TMC driver bring-up.
 */
void setup() {
  Serial.begin(115200);
  delay(100);
  SerialCommandHandler::begin(Serial);

  initMenu();

  DebugSerial::printFramed("Setup: initializeDriver");
  Movement::initializeDriver();

  DebugSerial::printFramed("Setup: preset begin");
  preset::begin();

  // Core 0 handles motor/homing/endstop tasks. loop() remains on Core 1 for UI/menu.
  DebugSerial::printFramed("Setup: create motor task");
  BaseType_t taskCreated = xTaskCreatePinnedToCore(
      motorTask,
      "motorTask",
      4096,
      nullptr,
      2,
      &motorTaskHandle,
      0);
  if (taskCreated != pdPASS) {
    DebugSerial::printFramed("Failed to start motor task on Core 0");
    return;
  }

  DebugSerial::printFramed("Setup: done");
}

/*
 * Main runtime loop: services menu input/rendering and applies
 * current brightness setting to the display backlight PWM pin.
 */
void loop() {
  menu.loop();
  SerialCommandHandler::poll();
  Movement::setSpeedSetting(getFocusSpeedSetting());
  analogWrite(PIN_LCD_BL, getBrightnessSetting() * 255 / 100);
}
