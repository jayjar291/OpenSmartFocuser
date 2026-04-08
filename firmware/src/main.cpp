#include <Arduino.h>
#include <limits.h>
#include <FastAccelStepper.h>
#include <Preferences.h>
#include <TMCStepper.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "debug_serial.h"
#include "menu.h"
#include "movement.h"
#include "serial_command_handler.h"

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
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

static constexpr const char* kCurrentPositionNamespace = "CurrentPos";
static constexpr const char* kCurrentPositionKey = "steps";
static constexpr uint32_t kCurrentPositionSaveIntervalMs = 8000;
static uint32_t gLastCurrentPositionSaveMs = 0;
static int32_t gLastSavedCurrentPositionSteps = INT32_MIN;

static void clearPersistentCurrentPosition() {
  Preferences preferences;
  if (preferences.begin(kCurrentPositionNamespace, false)) {
    preferences.remove(kCurrentPositionKey);
    preferences.end();
  }
  gLastSavedCurrentPositionSteps = INT32_MIN;
}

static int32_t loadPersistentCurrentPosition() {
  Preferences preferences;
  int32_t steps = 0;
  if (preferences.begin(kCurrentPositionNamespace, true)) {
    steps = preferences.getLong(kCurrentPositionKey, 0);
    preferences.end();
  }
  return steps;
}

static void savePersistentCurrentPositionIfDue() {
  if (Movement::isBusy()) {
    return;
  }

  const uint32_t nowMs = millis();
  if (nowMs - gLastCurrentPositionSaveMs < kCurrentPositionSaveIntervalMs) {
    return;
  }

  const int32_t currentSteps = Movement::getCurrentPositionSteps();
  if (currentSteps == gLastSavedCurrentPositionSteps) {
    gLastCurrentPositionSaveMs = nowMs;
    return;
  }

  Preferences preferences;
  if (preferences.begin(kCurrentPositionNamespace, false)) {
    preferences.putLong(kCurrentPositionKey, currentSteps);
    preferences.end();
    gLastSavedCurrentPositionSteps = currentSteps;
  }
  gLastCurrentPositionSaveMs = nowMs;
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

  Movement::initializeDriver();
  Movement::setCurrentPositionSteps(loadPersistentCurrentPosition());

  // Core 0 handles motor/homing/endstop tasks. loop() remains on Core 1 for UI/menu.
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
  }
}

/*
 * Main runtime loop: services menu input/rendering and applies
 * current brightness setting to the display backlight PWM pin.
 */
void loop() {
  menu.loop();
  SerialCommandHandler::poll();
  savePersistentCurrentPositionIfDue();
  Movement::setSpeedSetting(getFocusSpeedSetting());
  analogWrite(PIN_LCD_BL, getBrightnessSetting() * 255 / 100);
}
