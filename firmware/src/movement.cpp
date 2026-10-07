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
constexpr const char* kCalibrationNamespace = "Calib";
constexpr const char* kStepsPerMmKey = "stepsPerMm";

volatile bool homingInProgress = false;
volatile bool homingReturnInProgress = false;
volatile bool endstopInterruptPending = false;
volatile bool endstopTriggeredDuringHoming = false;
volatile bool calibInProgress = false;
volatile bool endstopTriggeredDuringCalib = false;
Movement::CalibrationState calibState = Movement::CalibrationState::Idle;
int32_t calibFirstContactSteps = 0;
int32_t calibSecondContactSteps = 0;
uint32_t calibComputedStepsPerMm = 0;
int32_t calibVerifyDeltaSteps = 0;
uint32_t stepsPerMmCached = FOCUSER_STEPS_PER_MM_DEFAULT;
uint16_t desiredMicrosteps = TMC_MICROSTEPS;
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

void loadStepsPerMm() {
  Preferences preferences;
  if (preferences.begin(kCalibrationNamespace, true)) {
    stepsPerMmCached = preferences.getULong(kStepsPerMmKey, FOCUSER_STEPS_PER_MM_DEFAULT);
    preferences.end();
  }
}

void configureTmcDriverRegisters() {
  focuserDriver.begin();
  focuserDriver.toff(4);
  focuserDriver.rms_current(TMC_RMS_CURRENT);
  focuserDriver.en_spreadCycle(TMC_SPREAD_CYCLE);
  focuserDriver.microsteps(desiredMicrosteps);
  focuserDriver.intpol(TMC_INTERPOLATE);

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
  if (calibInProgress) {
    endstopTriggeredDuringCalib = true;
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
  
  focuserStepper = stepperEngine.stepperConnectToPin(PIN_TMC_STEP, DRIVER_RMT);
  DebugSerial::printFramedValue("focuserStepper ptr: ", (uint32_t)focuserStepper, "");
  if (focuserStepper == nullptr) {
    DebugSerial::printFramed("stepperConnectToPin failed");
    return;
  }
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
  
  loadStepsPerMm();
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
  if (focuserStepper == nullptr || homingInProgress || homingReturnInProgress || calibInProgress) {
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
  if (focuserStepper == nullptr || homingInProgress || homingReturnInProgress || calibInProgress) {
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
  if (focuserStepper == nullptr || homingInProgress || homingReturnInProgress || calibInProgress) {
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
  int32_t targetSteps = static_cast<int32_t>(targetMm * stepsPerMmCached);
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
  focuserStepper->runBackward();
  //focuserStepper->moveTo(-100000); // Move a large distance backward to ensure we hit the endstop. The actual position will be reset to 0 when the endstop is triggered.
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

  //
  //focuserStepper->forceStopAndNewPosition(0);
  
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
    focuserStepper->moveTo(FOCUSER_HOMING_RETURN_MM * static_cast<int32_t>(stepsPerMmCached));
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
  return homingInProgress || jogActive || homingReturnInProgress || calibInProgress ||
         (focuserStepper != nullptr && focuserStepper->isRunning());
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
  return targetSteps >= FOCUSER_SOFT_MIN_STEPS && targetSteps <= Movement::getSoftMaxSteps();
}

void updateSoftEndstops() {
  if (focuserStepper == nullptr || homingInProgress || !motorEnabled) {
    return;
  }
  const int32_t pos = focuserStepper->getCurrentPosition();
  const int32_t softMaxSteps = Movement::getSoftMaxSteps();
  if (pos > softMaxSteps) {
    focuserStepper->forceStopAndNewPosition(softMaxSteps);
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

void getLimits(int32_t& minSteps, int32_t& maxSteps) {
  minSteps = FOCUSER_SOFT_MIN_STEPS;
  maxSteps = Movement::getSoftMaxSteps();
}

uint32_t getStepsPerMm() {
  return stepsPerMmCached;
}

bool setStepsPerMm(uint32_t stepsPerMm) {
  if (stepsPerMm == 0) {
    return false;
  }
  stepsPerMmCached = stepsPerMm;
  Preferences preferences;
  if (!preferences.begin(kCalibrationNamespace, false)) {
    return false;
  }
  preferences.putULong(kStepsPerMmKey, stepsPerMm);
  preferences.end();
  return true;
}

int32_t getSoftMaxSteps() {
  return static_cast<int32_t>(FOCUSER_SOFT_MAX_MM * stepsPerMmCached);
}

void startStepCalibration() {
  if (focuserStepper == nullptr || homingInProgress || homingReturnInProgress || isBusy() || !uartConnectedCached) {
    return;
  }
  calibFirstContactSteps = 0;
  calibSecondContactSteps = 0;
  calibComputedStepsPerMm = 0;
  calibVerifyDeltaSteps = 0;
  calibState = CalibrationState::AwaitingBlockInsert;
}

namespace {

// Starts a continuous move toward the endstop, used by all calibration contact moves.
void beginCalibrationContactMove(CalibrationState nextState) {
  if (focuserStepper == nullptr) {
    return;
  }
  calibState = nextState;
  noInterrupts();
  endstopTriggeredDuringCalib = false;
  interrupts();
  setMotorEnabledState(true);
  focuserStepper->setSpeedInHz(HOMING_SPEED_STEPS_PER_SEC);
  calibInProgress = true;
  touchMotorActivity();
  focuserStepper->runBackward();
}

} // namespace

void stepCalibrationContinue() {
  if (focuserStepper == nullptr) {
    return;
  }
  switch (calibState) {
    case CalibrationState::AwaitingBlockInsert:
      beginCalibrationContactMove(CalibrationState::MovingToBlockContact);
      break;
    case CalibrationState::AwaitingBlockRemoval:
      beginCalibrationContactMove(CalibrationState::MovingToEndstopContact);
      break;
    case CalibrationState::AwaitingConfirm:
      if (calibComputedStepsPerMm > 0) {
        setStepsPerMm(calibComputedStepsPerMm);
      }
      // Verify the new calibration by re-homing, backing off, and re-approaching the endstop.
      calibState = CalibrationState::VerifyHoming;
      startHoming();
      break;
    case CalibrationState::VerifyDone:
      calibState = CalibrationState::Idle;
      break;
    default:
      break;
  }
}

void abortStepCalibration() {
  if (homingInProgress || homingReturnInProgress) {
    abortHoming();
  }
  if (focuserStepper != nullptr) {
    focuserStepper->forceStop();
    focuserStepper->setSpeedInHz(speedHz);
  }
  calibInProgress = false;
  jogActive = false;
  calibState = CalibrationState::Idle;
  noInterrupts();
  endstopTriggeredDuringCalib = false;
  interrupts();
  touchMotorActivity();
}

void updateStepCalibration() {
  if (focuserStepper == nullptr) {
    return;
  }

  if (calibState == CalibrationState::VerifyHoming) {
    if (!homingInProgress && !homingReturnInProgress) {
      calibState = CalibrationState::VerifyBackoff;
    }
    return;
  }

  if (calibState == CalibrationState::VerifyBackoff) {
    if (!calibInProgress) {
      touchMotorActivity();
      focuserStepper->setSpeedInHz(HOMING_SPEED_STEPS_PER_SEC);
      focuserStepper->moveTo(getCurrentPositionSteps() +
          static_cast<int32_t>(FOCUSER_CALIBRATION_VERIFY_BACKOFF_MM * stepsPerMmCached));
      calibInProgress = true;
    } else if (!focuserStepper->isRunning()) {
      calibInProgress = false;
      beginCalibrationContactMove(CalibrationState::VerifyMovingToEndstop);
    }
    return;
  }

  if (!calibInProgress) {
    return;
  }

  if (!motorEnabled) {
    DebugSerial::printFramed("Motor disabled during step calibration. Aborting.");
    abortStepCalibration();
    return;
  }

  touchMotorActivity();

  bool triggered = false;
  noInterrupts();
  triggered = endstopTriggeredDuringCalib;
  if (triggered) {
    endstopTriggeredDuringCalib = false;
  }
  interrupts();

  if (!triggered && !isEndstopTriggered()) {
    return;
  }

  focuserStepper->forceStop();
  focuserStepper->setSpeedInHz(speedHz);
  calibInProgress = false;

  if (calibState == CalibrationState::MovingToBlockContact) {
    calibFirstContactSteps = getCurrentPositionSteps();
    calibState = CalibrationState::AwaitingBlockRemoval;
  } else if (calibState == CalibrationState::MovingToEndstopContact) {
    calibSecondContactSteps = getCurrentPositionSteps();
    const int32_t deltaSteps = abs(calibFirstContactSteps - calibSecondContactSteps);
    calibComputedStepsPerMm = (deltaSteps > 0)
        ? static_cast<uint32_t>(deltaSteps / FOCUSER_CALIBRATION_BLOCK_MM)
        : 0;
    calibState = CalibrationState::AwaitingConfirm;
  } else if (calibState == CalibrationState::VerifyMovingToEndstop) {
    calibVerifyDeltaSteps = getCurrentPositionSteps();
    calibState = CalibrationState::VerifyDone;
  }

  touchMotorActivity();
}

CalibrationState getStepCalibrationState() {
  return calibState;
}

uint32_t getStepCalibrationResultStepsPerMm() {
  return calibComputedStepsPerMm;
}

int32_t getStepCalibrationVerifyDeltaSteps() {
  return calibVerifyDeltaSteps;
}

void setMicrosteps(uint16_t microsteps) {
  if (microsteps == 0) {
    return;
  }
  desiredMicrosteps = microsteps;
  if (focuserStepper != nullptr) {
    focuserDriver.microsteps(microsteps);
    driverMicrostepsCached = focuserDriver.microsteps();
  }
}

} // namespace Movement
