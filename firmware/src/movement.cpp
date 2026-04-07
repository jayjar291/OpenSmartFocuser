#include <Arduino.h>
#include <FastAccelStepper.h>
#include <TMCStepper.h>
#include "config.h"
#include "movement.h"

extern FastAccelStepper* focuserStepper;
extern TMC2209Stepper focuserDriver;
extern bool motorEnabled;
extern HardwareSerial tmcSerial;
extern FastAccelStepperEngine stepperEngine;

namespace {

volatile bool homingInProgress = false;
volatile bool homingReturnInProgress = false;

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
  focuserStepper->setSpeedInHz(IDLE_JOG_SPEED_STEPS_PER_SEC);
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
  focuserStepper->setSpeedInHz(IDLE_JOG_SPEED_STEPS_PER_SEC);
  focuserStepper->runBackward();
}

void stopJog() {
  if (focuserStepper == nullptr || homingInProgress || homingReturnInProgress) {
    return;
  }
  focuserStepper->stopMove();
}


void moveToPositionMm(float targetMm) {
  if (focuserStepper == nullptr) {
    return;
  }
  if (!motorEnabled) {
    return;
  }
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
  homingReturnInProgress = false;
  homingInProgress = false;
}

void startHoming() {
  if (focuserStepper == nullptr) {
    return;
  }

  Serial.println("Starting homing sequence...");
  Serial.print("Current position (steps): ");
  Serial.println(getCurrentPositionSteps());

  if (!motorEnabled) {
    motorEnabled = true;
    focuserStepper->enableOutputs();
  }

  focuserStepper->stopMove();
  focuserStepper->setSpeedInHz(HOMING_SPEED_STEPS_PER_SEC);
  focuserStepper->runBackward();
  homingReturnInProgress = false;
  homingInProgress = true;
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
      Serial.print("Homing complete. Position reset to ");
      Serial.print(FOCUSER_HOMING_RETURN_MM);
      Serial.println(" mm.");
      homingReturnInProgress = false;
      homingInProgress = false;
    }
    return;
  }

  if (isEndstopTriggered()) {
    Serial.println("Endstop triggered during homing.");
    Serial.print("Position at endstop trigger (steps): ");
    Serial.println(getCurrentPositionSteps());
    Serial.println("setting position to 0 and starting return move...");
    focuserStepper->forceStopAndNewPosition(0);

    focuserStepper->forceStop();
    Serial.println("moving back to return position...");
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
    Serial.println("Soft endstop triggered. Stopping movement.");
  }
}

bool isBusy() {
  return homingInProgress;
}

} // namespace Movement
