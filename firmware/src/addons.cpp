#include "addons.h"
#include "debug_serial.h"

#include <Arduino.h>
#if HAS_SHUTTER
#include <ServoEasing.hpp>
#endif

#include "config.h"

namespace Addons {

namespace {

// Shutter servo hardware mapping.
constexpr uint16_t kShutterPulseClosedUs = 500;
constexpr uint16_t kShutterPulseOpenUs = 2500;
constexpr uint16_t kShutterMinAngle = 0;
constexpr uint16_t kShutterMaxAngle = 270;

// ServoEasing behavior configuration.
constexpr uint16_t kShutterInitialAngle = 0;
constexpr uint_fast8_t kShutterEasingType = EASE_CUBIC_IN_OUT;
constexpr uint16_t kShutterCloseCruiseSpeedDegPerSec = 180;
constexpr uint16_t kShutterCloseLandingSpeedDegPerSec = 45;
constexpr uint16_t kShutterOpenCruiseSpeedDegPerSec = 180;
constexpr uint16_t kShutterOpenLandingSpeedDegPerSec = 50;
constexpr uint16_t kShutterCloseTransitionAngle = 45;
constexpr uint16_t kShutterOpenTransitionAngle = 210;

// Keep servo powered briefly after movement to prevent detach/attach chatter.
constexpr uint32_t kShutterDetachDelayMs = 350;

uint32_t gLastShutterMoveTickMs = 0;

bool gAddonsInitialized = false;
ServoEasing gShutterServo;
bool gShutterServoAttached = false;
uint16_t gShutterTargetPosition = 0;
bool gShutterHasPendingStage = false;
uint16_t gShutterPendingTargetPosition = 0;
uint16_t gShutterPendingSpeedDegPerSec = 0;


uint16_t clampShutterPosition(uint16_t position) {
  return static_cast<uint16_t>(constrain(static_cast<int32_t>(position), static_cast<int32_t>(kShutterMinAngle), static_cast<int32_t>(kShutterMaxAngle)));
}

bool attachShutterServoIfNeeded(uint16_t initialAngle) {
  if (gShutterServoAttached) {
    return true;
  }

  const uint16_t clampedInitialAngle = clampShutterPosition(initialAngle);
  if (gShutterServo.attach(PIN_SHUTTER_SERVO, clampedInitialAngle, kShutterPulseClosedUs, kShutterPulseOpenUs) == INVALID_SERVO) {
    DebugSerial::printFramed("attachShutterServoIfNeeded: failed to attach shutter servo");
    return false;
  }

  gShutterServo.setEasingType(kShutterEasingType);
  gShutterServoAttached = true;
  return true;
}

void startShutterStageMove(uint16_t targetPosition, uint16_t speedDegPerSec) {
  gShutterServo.setEasingType(kShutterEasingType);
  gShutterServo.startEaseTo(static_cast<int>(targetPosition), speedDegPerSec, START_UPDATE_BY_INTERRUPT);
  gShutterTargetPosition = targetPosition;
  gLastShutterMoveTickMs = millis();
}

void startShutterMove(uint16_t requestedPosition) {
  const uint16_t targetPosition = clampShutterPosition(requestedPosition);
  const uint32_t now = millis();

  if (!attachShutterServoIfNeeded(gShutterTargetPosition)) {
    return;
  }

  if (targetPosition == gShutterTargetPosition && !gShutterServo.isMoving()) {
    gShutterServo.write(targetPosition);
    gShutterTargetPosition = targetPosition;
    gShutterHasPendingStage = false;
    gLastShutterMoveTickMs = now;
    return;
  }

  gShutterHasPendingStage = false;

  // Two-stage close profile for full close: fast cruise then softer landing.
  if (targetPosition == kShutterMinAngle && gShutterTargetPosition > kShutterCloseTransitionAngle) {
    const uint16_t stage1Target = kShutterCloseTransitionAngle;
    gShutterHasPendingStage = true;
    gShutterPendingTargetPosition = kShutterMinAngle;
    gShutterPendingSpeedDegPerSec = kShutterCloseLandingSpeedDegPerSec;
    startShutterStageMove(stage1Target, kShutterCloseCruiseSpeedDegPerSec);
    return;
  }

  // Two-stage open profile for full open: fast cruise then softer landing.
  if (targetPosition == kShutterMaxAngle && gShutterTargetPosition < kShutterOpenTransitionAngle) {
    const uint16_t stage1Target = kShutterOpenTransitionAngle;
    gShutterHasPendingStage = true;
    gShutterPendingTargetPosition = kShutterMaxAngle;
    gShutterPendingSpeedDegPerSec = kShutterOpenLandingSpeedDegPerSec;
    startShutterStageMove(stage1Target, kShutterOpenCruiseSpeedDegPerSec);
    return;
  }

  const bool isClosing = targetPosition < gShutterTargetPosition;
  uint16_t speedDegPerSec = isClosing ? kShutterCloseCruiseSpeedDegPerSec : kShutterOpenCruiseSpeedDegPerSec;

  if (isClosing && targetPosition <= kShutterCloseTransitionAngle) {
    speedDegPerSec = kShutterCloseLandingSpeedDegPerSec;
  } else if (!isClosing && targetPosition >= kShutterOpenTransitionAngle) {
    speedDegPerSec = kShutterOpenLandingSpeedDegPerSec;
  }

  startShutterStageMove(targetPosition, speedDegPerSec);
}


void applyFlatPanelBrightness(uint8_t brightness) {
  ledcWrite(6, brightness); // Write the brightness value to PWM channel 6 for flat panel control
  DebugSerial::printFramedValue("applyFlatPanelBrightness: brightness ", brightness, "");
}

} // namespace


bool isEnabled() {
  return HAS_SHUTTER || HAS_FLAT_FRAME_PANEL;
}

bool isInitialized() {
  return gAddonsInitialized;
}

bool hasAddon(AddonType type) {
  switch (type) {
    case AddonType::Shutter:
      return HAS_SHUTTER;
    case AddonType::FlatPanel:
      return HAS_FLAT_FRAME_PANEL;
    default:
      return false;
  }
}

void initializeAddons() {
  if (gAddonsInitialized) {
    return;
  }

  #if HAS_SHUTTER
  ESP32PWM::allocateTimer(0); // Allocate PWM timer 0 for shutter servo
  ESP32PWM::allocateTimer(1); // Allocate PWM timer 1 for shutter servo
  gShutterTargetPosition = kShutterInitialAngle;
  if (attachShutterServoIfNeeded(gShutterTargetPosition)) {
    gShutterServo.write(gShutterTargetPosition);
  }
  #endif

  #if HAS_FLAT_FRAME_PANEL
  ledcSetup(6, 5000, 8); // Set up PWM on channel 6 with 5 kHz frequency and 8-bit resolution for flat panel brightness control
  ledcAttachPin(PIN_FLAT_FRAME_PANEL, 6); // Attach the flat
  applyFlatPanelBrightness(0);
  #endif

  gAddonsInitialized = true;
  #if HAS_SHUTTER
  if (gShutterServoAttached) {
    gShutterServo.detach(); // Ensure servo is not powered until explicitly commanded
    gShutterServoAttached = false;
  }
  #endif
}

void setFlatPanelBrightness(uint8_t brightness) {
  if (!HAS_FLAT_FRAME_PANEL || !gAddonsInitialized) {
    return;
  }
  applyFlatPanelBrightness(brightness);
}

void setShutterPosition(uint16_t position) {
  if (!HAS_SHUTTER || !gAddonsInitialized) {
    return;
  }

  startShutterMove(position);
  DebugSerial::printFramedValue("setShutterPosition: target ", clampShutterPosition(position), "");
}

void detachServos() {
  if (HAS_SHUTTER && gAddonsInitialized && gShutterServoAttached) {
    if (gShutterServo.isMoving()) {
      gLastShutterMoveTickMs = millis();
      return;
    }

    if (gShutterHasPendingStage) {
      gShutterHasPendingStage = false;
      startShutterStageMove(gShutterPendingTargetPosition, gShutterPendingSpeedDegPerSec);
      return;
    }

    if ((millis() - gLastShutterMoveTickMs) >= kShutterDetachDelayMs) { // Only detach if it's been a while since the last move command, to avoid unnecessary detach/attach cycles
      gShutterServo.detach();
      gShutterServoAttached = false;
      DebugSerial::printFramed("detachServos: shutter servo detached to reduce power and prevent jitter");
    }
  }
}

void toggleFlatPanel() {
  if (!HAS_FLAT_FRAME_PANEL || !gAddonsInitialized) {
    return;
  }

  static bool isOn = false;
  isOn = !isOn;
  applyFlatPanelBrightness(isOn ? 255 : 0);
}

void toggleShutter() {
  if (!HAS_SHUTTER || !gAddonsInitialized) {
    return;
  }
  static bool isOpen = false;
  isOpen = !isOpen;
  setShutterPosition(static_cast<uint16_t>(isOpen ? 270 : 0));
}

} // namespace Addons