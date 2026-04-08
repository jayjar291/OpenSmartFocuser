#include <Arduino.h>
#include <FastAccelStepper.h>
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

volatile bool homingInProgress = false;
volatile bool homingReturnInProgress = false;
bool jogActive = false;

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

  focuserDriver.begin();
  focuserDriver.toff(4);
  focuserDriver.rms_current(TMC_RMS_CURRENT);
  focuserDriver.en_spreadCycle(TMC_SPREAD_CYCLE);
  focuserDriver.microsteps(TMC_MICROSTEPS);
  focuserDriver.pwm_autoscale(true);

  stepperEngine.init();
  focuserStepper = stepperEngine.stepperConnectToPin(PIN_TMC_STEP);
  focuserStepper->setDirectionPin(PIN_TMC_DIR);
  focuserStepper->setEnablePin(PIN_TMC_ENABLE, true); // active LOW: LOW = motor enabled
  focuserStepper->setSpeedInHz(TMC_MAX_SPEED);
  focuserStepper->setAcceleration(TMC_MAX_ACCELERATION);
  motorEnabled = false;
  focuserStepper->disableOutputs();
}


void toggleMotorEnabled() {
  motorEnabled = !motorEnabled;
  if (focuserStepper == nullptr) {
    return;
  }

  if (motorEnabled) {
    focuserStepper->enableOutputs();
  } else {
    jogActive = false;
    focuserStepper->stopMove();
    focuserStepper->disableOutputs();
  }
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
  focuserStepper->enableOutputs();
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
  focuserStepper->enableOutputs();
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
  focuserStepper->enableOutputs();
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
  focuserStepper->enableOutputs();
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

void abortHoming() {
  if (focuserStepper != nullptr) {
    focuserStepper->forceStop();
  }
  jogActive = false;
  homingReturnInProgress = false;
  homingInProgress = false;
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
    motorEnabled = true;
    focuserStepper->enableOutputs();
  }

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

  if (homingReturnInProgress) {
    if (!focuserStepper->isRunning()) {
      DebugSerial::printFramedValue("Homing complete. Position reset to ", FOCUSER_HOMING_RETURN_MM, " mm.");
      homingReturnInProgress = false;
      homingInProgress = false;
      focuserStepper->setSpeedInHz(speedHz);
      //respond with :HD# to indicate homing complete
      Serial.println(":HD#");
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
  }
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
  return homingInProgress;
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
