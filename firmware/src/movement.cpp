#include <Arduino.h>
#include <FastAccelStepper.h>
#include <Preferences.h>
#include <TMCStepper.h>
#include "config.h"
#include "debug_serial.h"
#include "movement.h"

extern FastAccelStepper* focuserStepper;
extern TMC2209Stepper focuserDriver;
extern bool motorEnabled;
extern HardwareSerial tmcSerial;
extern FastAccelStepperEngine stepperEngine;

uint32_t speedHz = SLOW_FOCUS_SPEED_STEPS_PER_SEC;

namespace {

constexpr const char* kCurrentPositionNamespace = "CurrentPos";
constexpr const char* kCurrentPositionKey = "steps";

volatile bool homingInProgress = false;
volatile bool homingReturnInProgress = false;
bool jogActive = false;
uint32_t lastMotorActivityMs = 0;
bool uartConnectionLost = false;
uint32_t lastUartReconnectAttemptMs = 0;
bool uartConnectedCached = false;
uint16_t driverCurrentCachedMa = 0;
uint16_t driverMicrostepsCached = 0;

constexpr uint32_t kUartReconnectAttemptIntervalMs = 500;

bool wasMoving = false;

void touchMotorActivity() {
  lastMotorActivityMs = millis();
}

void configureTmcDriverRegisters() {
  focuserDriver.begin();
  focuserDriver.toff(4);
  focuserDriver.rms_current(TMC_RMS_CURRENT);
  focuserDriver.en_spreadCycle(TMC_SPREAD_CYCLE);
  focuserDriver.microsteps(TMC_MICROSTEPS);
  focuserDriver.pwm_autoscale(true);
}

void setMotorEnabledState(bool enabled) {
  if (focuserStepper == nullptr) {
    motorEnabled = enabled;
    return;
  }

  motorEnabled = enabled;
  touchMotorActivity();

  if (enabled) {
    focuserStepper->enableOutputs();
    return;
  }

  jogActive = false;
  focuserStepper->stopMove();
  focuserStepper->disableOutputs();
}

bool isEndstopTriggered() {
  return digitalRead(PIN_BUTTON_ENDSTOP) == LOW;
}

} // namespace

namespace Movement {

void initializeDriver() {
  pinMode(PIN_TMC_STEP, OUTPUT);
  digitalWrite(PIN_TMC_STEP, LOW);
  pinMode(PIN_TMC_DIR, OUTPUT);
  digitalWrite(PIN_TMC_DIR, LOW);
  pinMode(PIN_TMC_ENABLE, OUTPUT);
  digitalWrite(PIN_TMC_ENABLE, HIGH);
  pinMode(PIN_BUTTON_ENDSTOP, INPUT_PULLUP);

  tmcSerial.begin(TMC_UART_BAUDRATE, SERIAL_8N1, PIN_TMC_UART_RX, PIN_TMC_UART_TX);

  configureTmcDriverRegisters();

  stepperEngine.init();
  focuserStepper = stepperEngine.stepperConnectToPin(PIN_TMC_STEP);
  focuserStepper->setDirectionPin(PIN_TMC_DIR);
  focuserStepper->setEnablePin(PIN_TMC_ENABLE, true); // active LOW: LOW = motor enabled
  focuserStepper->setSpeedInHz(TMC_MAX_SPEED);
  focuserStepper->setAcceleration(TMC_MAX_ACCELERATION);
  setMotorEnabledState(false);
  uartConnectedCached = (focuserDriver.test_connection() == 0);
  if (uartConnectedCached) {
    driverCurrentCachedMa = focuserDriver.rms_current();
    driverMicrostepsCached = focuserDriver.microsteps();
  } else {
    driverCurrentCachedMa = 0;
    driverMicrostepsCached = 0;
  }
  uartConnectionLost = false;
  lastUartReconnectAttemptMs = millis();
  loadPersistentCurrentPosition();
}

void healthCheck() {
  if (focuserStepper == nullptr) {
    return;
  }

  const bool uartConnected = (focuserDriver.test_connection() == 0);
  if (uartConnected) {
    if (uartConnectionLost) {
      DebugSerial::printFramed("TMC UART connection restored. Reinitializing registers.");
      configureTmcDriverRegisters();
      uartConnectionLost = false;
      lastUartReconnectAttemptMs = millis();
      DebugSerial::printFramed("TMC driver registers reloaded after reconnect.");
    }
    uartConnectedCached = true;
    driverCurrentCachedMa = focuserDriver.rms_current();
    driverMicrostepsCached = focuserDriver.microsteps();
    return;
  }

  uartConnectedCached = false;
  driverCurrentCachedMa = 0;
  driverMicrostepsCached = 0;

  if (!uartConnectionLost) {
    uartConnectionLost = true;
    DebugSerial::printFramed("TMC UART connection lost. Entering reconnect mode.");
    setMotorEnabledState(false);
    DebugSerial::printFramed("Motor disabled while UART is disconnected.");
  }

  const uint32_t now = millis();
  if (now - lastUartReconnectAttemptMs < kUartReconnectAttemptIntervalMs) {
    return;
  }

  lastUartReconnectAttemptMs = now;
  tmcSerial.end();
  tmcSerial.begin(TMC_UART_BAUDRATE, SERIAL_8N1, PIN_TMC_UART_RX, PIN_TMC_UART_TX);
  DebugSerial::printFramed("Attempting TMC UART reconnect...");
}

void toggleMotorEnabled() {
  setMotorEnabledState(!motorEnabled);
}

bool isMotorEnabled() {
  return motorEnabled;
}

void jogForward() {
  if (focuserStepper == nullptr || homingInProgress || homingReturnInProgress) {
    return;
  }
  if (!motorEnabled) {
    focuserStepper->stopMove();
    return;
  }
  touchMotorActivity();
  setMotorEnabledState(true);
  jogActive = true;
  focuserStepper->runForward();
}

void jogBackward() {
  if (focuserStepper == nullptr || homingInProgress || homingReturnInProgress) {
    return;
  }
  if (!motorEnabled) {
    focuserStepper->stopMove();
    return;
  }
  touchMotorActivity();
  setMotorEnabledState(true);
  jogActive = true;
  focuserStepper->runBackward();
}

void stopJog() {
  if (focuserStepper == nullptr || homingInProgress || homingReturnInProgress) {
    return;
  }
  if (jogActive) {
    focuserStepper->stopMove();
    jogActive = false;
    touchMotorActivity();
  }
}


void moveToPositionMm(float targetMm) {
  if (focuserStepper == nullptr) {
    return;
  }
  if (!motorEnabled) {
    return;
  }
  jogActive = false;
  touchMotorActivity();
  setMotorEnabledState(true);
  int32_t targetSteps = static_cast<int32_t>(targetMm * FOCUSER_STEPS_PER_MM);
  focuserStepper->moveTo(targetSteps);
}

void moveToPosition(int32_t targetSteps) {
  if (focuserStepper == nullptr) {
    return;
  }
  if (!motorEnabled) {
    return;
  }
  jogActive = false;
  touchMotorActivity();
  setMotorEnabledState(true);
  focuserStepper->moveTo(targetSteps);
}

int32_t getCurrentPositionSteps() {
  if (focuserStepper == nullptr) {
    return 0;
  }
  return focuserStepper->getCurrentPosition();
}

void setCurrentPositionSteps(int32_t positionSteps) {
  if (focuserStepper == nullptr) {
    return;
  }
  focuserStepper->forceStopAndNewPosition(positionSteps);
}

void loadPersistentCurrentPosition() {
  Preferences preferences;
  int32_t steps = 0;
  if (preferences.begin(kCurrentPositionNamespace, true)) {
    steps = preferences.getLong(kCurrentPositionKey, 0);
    preferences.end();
  }
  focuserStepper->forceStopAndNewPosition(steps);
}

void savePersistentCurrentPosition() {
  Preferences preferences;
  if (preferences.begin(kCurrentPositionNamespace, false)) {
    preferences.putLong(kCurrentPositionKey, getCurrentPositionSteps());
    preferences.end();
  }
}

void updatePositionPersistence() {
  if (focuserStepper == nullptr) {
    return;
  }
  // Homing has its own explicit completion handling.
  if (homingInProgress || homingReturnInProgress) {
    wasMoving = true;
    return;
  }

  const bool isRunning = focuserStepper->isRunning();
  if (wasMoving && !isRunning) {
    savePersistentCurrentPosition();
  }

  wasMoving = isRunning;
}

void clearPersistentCurrentPosition() {
  Preferences preferences;
  if (preferences.begin(kCurrentPositionNamespace, false)) {
    preferences.remove(kCurrentPositionKey);
    preferences.end();
  }
}

void abortHoming() {
  if (focuserStepper != nullptr) {
    focuserStepper->forceStop();
  }
  jogActive = false;
  homingReturnInProgress = false;
  homingInProgress = false;
  touchMotorActivity();
}

void startHoming() {
  if (focuserStepper == nullptr) {
    return;
  }

  DebugSerial::printFramed("Starting homing sequence...");
  DebugSerial::printFramedValue("Current position (steps): ", getCurrentPositionSteps(), "");
  
  homingReturnInProgress = false;
  homingInProgress = true;
  jogActive = false;

  if (!motorEnabled) {
    setMotorEnabledState(true);
  }

  touchMotorActivity();
  focuserStepper->stopMove();
  focuserStepper->setSpeedInHz(HOMING_SPEED_STEPS_PER_SEC);
  focuserStepper->runBackward();
}

void updateHoming() {
  if (!homingInProgress || focuserStepper == nullptr) {
    return;
  }

  if (!motorEnabled) {
    abortHoming();
    return;
  }

  touchMotorActivity();

  if (homingReturnInProgress) {
    if (!focuserStepper->isRunning()) {
      DebugSerial::printFramedValue("Homing complete. Position reset to ", FOCUSER_HOMING_RETURN_MM, " mm.");
      homingReturnInProgress = false;
      homingInProgress = false;
      focuserStepper->setSpeedInHz(speedHz);
      //respond with :HD# to indicate homing complete
      savePersistentCurrentPosition();
      Serial.println(":HD#");
      touchMotorActivity();
    }
    return;
  }

  if (isEndstopTriggered()) {
    DebugSerial::printFramed("Endstop triggered during homing.");
    DebugSerial::printFramedValue("Position at endstop trigger (steps): ", getCurrentPositionSteps(), "");
    DebugSerial::printFramed("setting position to 0 and starting return move...");
    focuserStepper->forceStopAndNewPosition(0);

    focuserStepper->forceStop();
    DebugSerial::printFramed("moving back to return position...");
    focuserStepper->setSpeedInHz(HOMING_SPEED_STEPS_PER_SEC);
    focuserStepper->moveTo(FOCUSER_STEPS_PER_MM * FOCUSER_HOMING_RETURN_MM);
    homingReturnInProgress = true;
    touchMotorActivity();
  }
}

void updateMotorIdleTimeout() {
  if (focuserStepper == nullptr || !motorEnabled || homingInProgress || homingReturnInProgress) {
    return;
  }

  if (jogActive || focuserStepper->isRunning()) {
    touchMotorActivity();
    return;
  }

  const uint32_t now = millis();
  if (now - lastMotorActivityMs < MOTOR_IDLE_TIMEOUT_MS) {
    return;
  }

  DebugSerial::printFramed("Motor idle timeout reached. Disabling motor.");
  setMotorEnabledState(false);
}

void updateSoftEndstops() {
  if (focuserStepper == nullptr || homingInProgress || !motorEnabled) {
    return;
  }

  const int32_t pos = focuserStepper->getCurrentPosition();
  if (pos < FOCUSER_SOFT_MIN_STEPS || pos > FOCUSER_SOFT_MAX_STEPS) {
    focuserStepper->stopMove();
    DebugSerial::printFramed("Soft endstop triggered. Stopping movement.");
  }
}

bool isBusy() {
  return homingInProgress || jogActive || homingReturnInProgress || (focuserStepper != nullptr && focuserStepper->isRunning());
}

bool isUartConnected() {
  return focuserStepper != nullptr && uartConnectedCached;
}

uint16_t getDriverMicrosteps() {
  return driverMicrostepsCached;
}

uint16_t getDriverCurrentMa() {
  return driverCurrentCachedMa;
}

void setSpeedSetting(uint8_t speedSettingIndex) {
  switch (speedSettingIndex) {
    case 0:
      speedHz = FINE_FOCUS_SPEED_STEPS_PER_SEC;
      break;
    case 1:
      speedHz = SLOW_FOCUS_SPEED_STEPS_PER_SEC;
      break;
    case 2:
      speedHz = MEDIUM_FOCUS_SPEED_STEPS_PER_SEC;
      break;
    case 3:
      speedHz = FAST_FOCUS_SPEED_STEPS_PER_SEC;
      break;
    case 4:
      speedHz = MAX_FOCUS_SPEED_STEPS_PER_SEC;
      break;
    default:
      break;
  }
  if (focuserStepper != nullptr && !homingInProgress && !jogActive && !homingReturnInProgress) {
    focuserStepper->setSpeedInHz(speedHz);
  }
}

} // namespace Movement
