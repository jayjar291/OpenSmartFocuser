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
volatile bool endstopInterruptPending = false;
volatile bool endstopTriggeredDuringHoming = false;
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

bool isEndstopTriggered() {
  return digitalRead(PIN_BUTTON_ENDSTOP) == LOW;
}

void IRAM_ATTR onEndstopInterrupt() {
  endstopInterruptPending = true;
  if (homingInProgress || homingReturnInProgress) {
    endstopTriggeredDuringHoming = true;
  }
}

} // namespace

namespace Movement {


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


void initializeDriver() {
  pinMode(PIN_TMC_STEP, OUTPUT);
  digitalWrite(PIN_TMC_STEP, LOW);
  pinMode(PIN_TMC_DIR, OUTPUT);
  digitalWrite(PIN_TMC_DIR, LOW);
  pinMode(PIN_TMC_ENABLE, OUTPUT);
  digitalWrite(PIN_TMC_ENABLE, HIGH);
  pinMode(PIN_BUTTON_ENDSTOP, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_ENDSTOP), onEndstopInterrupt, FALLING);

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
    setMotorEnabledState(true);
  }
  // When jogging forward, we want to prevent moving further if we are already at or beyond the soft endstop limit.
  if (!checkSoftEndstops(focuserStepper->getCurrentPosition() + 250)) {
    DebugSerial::printFramedValue("target position is" , focuserStepper->getCurrentPosition() + 250,"target position is outside of soft endstop limits. Jog movement ignored.");
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
    setMotorEnabledState(true);
  }
  if (isEndstopTriggered())
  {
    DebugSerial::printFramed("Endstop is triggered. Jog backward ignored.");
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

void halt() {
  if (focuserStepper == nullptr) {
    return;
  }
  focuserStepper->forceStop();
  jogActive = false;
  homingReturnInProgress = false;
  homingInProgress = false;
  setMotorEnabledState(false);

  noInterrupts();
  endstopInterruptPending = false;
  endstopTriggeredDuringHoming = false;
  interrupts();

  touchMotorActivity();
}

void moveToPositionMm(float targetMm) {
  if (focuserStepper == nullptr) {
    return;
  }
  if (!motorEnabled) {
    setMotorEnabledState(true);
  }
  jogActive = false;
  touchMotorActivity();
  setMotorEnabledState(true);
  int32_t targetSteps = static_cast<int32_t>(targetMm * FOCUSER_STEPS_PER_MM);
  if (!checkSoftEndstops(targetSteps)) {
    DebugSerial::printFramedValue("Target position ", targetSteps, " steps is outside of soft endstop limits. movement ignored.");
    return;
  }
  focuserStepper->moveTo(targetSteps);
}

void moveToPosition(int32_t targetSteps) {
  if (focuserStepper == nullptr) {
    return;
  }
  if (!motorEnabled) {
    setMotorEnabledState(true);
  }
  if (!checkSoftEndstops(targetSteps)) {
    DebugSerial::printFramedValue("Target position ", targetSteps, " steps is outside of soft endstop limits. movement ignored.");
    return;
  }
  jogActive = false;
  touchMotorActivity();
  setMotorEnabledState(true);
  focuserStepper->moveTo(targetSteps);
}

void moveRelative(int32_t relativeSteps) {
  if (focuserStepper == nullptr) {
    return;
  }
  if (!motorEnabled) {
    setMotorEnabledState(true);
  }
  int32_t currentSteps = focuserStepper->getCurrentPosition();
  int32_t targetSteps = currentSteps + relativeSteps;
  if (!checkSoftEndstops(targetSteps)) {
    DebugSerial::printFramedValue("Target position ", targetSteps, " steps is outside of soft endstop limits. movement ignored.");
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
  noInterrupts();
  endstopTriggeredDuringHoming = false;
  interrupts();
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
  
  
  noInterrupts();
  endstopInterruptPending = false;
  endstopTriggeredDuringHoming = false;
  interrupts();

  
  touchMotorActivity();
  focuserStepper->stopMove();
  
  setMotorEnabledState(true);

  focuserStepper->setSpeedInHz(HOMING_SPEED_STEPS_PER_SEC);
  DebugSerial::printFramed("About to call runBackward()...");
  focuserStepper->moveTo(-100000); // Move a large distance backward to ensure we hit the endstop. The actual position will be reset to 0 when the endstop is triggered.
  DebugSerial::printFramedValue("Motor is running after runBackward(): ", focuserStepper->isRunning(), "");
}

void processEndstopEvent() {
  bool hasInterruptEvent = false;
  noInterrupts();
  hasInterruptEvent = endstopInterruptPending;
  if (hasInterruptEvent) {
    endstopInterruptPending = false;
  }
  interrupts();

  if (!hasInterruptEvent) {
    return;
  }

  if (homingInProgress || homingReturnInProgress) {
    noInterrupts();
    endstopTriggeredDuringHoming = true;
    interrupts();
    return;
  }

  if (focuserStepper == nullptr) {
    return;
  }
  focuserStepper->forceStopAndNewPosition(0);
  jogActive = false;
  touchMotorActivity();
  DebugSerial::printFramed("Endstop interrupt triggered. Movement halted.");
}

void updateHoming() {
  if (!homingInProgress || focuserStepper == nullptr) {
    return;
  }

  if (!motorEnabled) {
    DebugSerial::printFramed("Motor disabled during homing. Aborting homing.");
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

  bool triggeredDuringHoming = false;
  noInterrupts();
  triggeredDuringHoming = endstopTriggeredDuringHoming;
  if (triggeredDuringHoming) {
    DebugSerial::printFramed("Endstop was triggered during homing. 451");
    endstopTriggeredDuringHoming = false;
  }
  interrupts();

  if (triggeredDuringHoming || isEndstopTriggered()) {
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

uint8_t getSpeedSetting() {
  if (speedHz == FINE_FOCUS_SPEED_STEPS_PER_SEC) {
    return 0;
  } else if (speedHz == SLOW_FOCUS_SPEED_STEPS_PER_SEC) {
    return 1;
  } else if (speedHz == MEDIUM_FOCUS_SPEED_STEPS_PER_SEC) {
    return 2;
  } else if (speedHz == FAST_FOCUS_SPEED_STEPS_PER_SEC) {
    return 3;
  } else if (speedHz == MAX_FOCUS_SPEED_STEPS_PER_SEC) {
    return 4;
  } else {
    return -1; // Invalid speed setting
  }
}

bool checkSoftEndstops(int32_t targetSteps) {
  return targetSteps >= FOCUSER_SOFT_MIN_STEPS && targetSteps <= FOCUSER_SOFT_MAX_STEPS;
}

void updateSoftEndstops() {
  if (focuserStepper == nullptr || homingInProgress || !motorEnabled) {
    return;
  }
  const int32_t pos = focuserStepper->getCurrentPosition();
  if (pos > FOCUSER_SOFT_MAX_STEPS) {
    focuserStepper->forceStopAndNewPosition(FOCUSER_SOFT_MAX_STEPS);
    DebugSerial::printFramed("Soft endstop triggered. Stopping movement.");
    jogActive = false;
    touchMotorActivity();
    return;
  }
}


MovementStatus getMovementStatus() {
  if (homingInProgress || homingReturnInProgress) {
    return MovementStatus::Homing;
  }
  if (isBusy()) {
    return MovementStatus::Moving;
  }
  return MovementStatus::Idle;
}

} // namespace Movement
