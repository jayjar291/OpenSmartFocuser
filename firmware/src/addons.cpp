#include "addons.h"
#include "debug_serial.h"

#include <Arduino.h>
#if HAS_SHUTTER
#include <ESP32Servo.h>
#endif

#include "config.h"

namespace Addons {

namespace {

constexpr uint16_t kShutterPulseClosedUs = 500;
constexpr uint16_t kShutterPulseOpenUs = 2500;
constexpr uint8_t kFlatPanelPwmChannel = 6;
constexpr uint16_t kFlatPanelPwmFrequencyHz = 5000;
constexpr uint8_t kFlatPanelPwmResolutionBits = 8;
constexpr uint8_t kShutterClosedDegrees = 0;
constexpr uint8_t kShutterOpenDegrees = 180;
constexpr uint32_t kShutterServoDetachDelayMs = 350;

uint32_t lastShutterMoveMillis = 0;

bool gAddonsInitialized = false;
Servo gShutterServo;


void applyFlatPanelBrightness(uint8_t brightness) {
  // Write the requested flat panel brightness to the shared PWM channel.
  ledcWrite(kFlatPanelPwmChannel, brightness);
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
  // Resolve addon availability from compile-time feature flags.
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
  // Guard initialization so hardware resources are configured once.
  if (gAddonsInitialized) {
    return;
  }

  #if HAS_SHUTTER
  // Configure the shutter servo PWM timers and drive it to the closed position.
  ESP32PWM::allocateTimer(0); // Allocate PWM timer 0 for shutter servo
  ESP32PWM::allocateTimer(1); // Allocate PWM timer 1 for shutter servo
  gShutterServo.setPeriodHertz(50);
  gShutterServo.attach(PIN_SHUTTER_SERVO, kShutterPulseClosedUs, kShutterPulseOpenUs);
  gShutterServo.write(kShutterClosedDegrees);
  #endif

  #if HAS_FLAT_FRAME_PANEL
  // Configure the flat panel PWM output and start with the panel off.
  ledcSetup(kFlatPanelPwmChannel, kFlatPanelPwmFrequencyHz, kFlatPanelPwmResolutionBits);
  ledcAttachPin(PIN_FLAT_FRAME_PANEL, kFlatPanelPwmChannel);
  applyFlatPanelBrightness(0);
  #endif

  gAddonsInitialized = true;
  #if HAS_SHUTTER
  // Release the servo after startup so it is only powered while moving.
  gShutterServo.detach(); // Ensure servo is not powered until explicitly commanded
  #endif
}

void setFlatPanelBrightness(uint8_t brightness) {
  // Ignore unsupported or uninitialized flat panel requests.
  if (!HAS_FLAT_FRAME_PANEL || !gAddonsInitialized) {
    return;
  }
  applyFlatPanelBrightness(brightness);
}

void setShutterPosition(uint8_t position) {
  // Ignore unsupported or uninitialized shutter requests.
  if (!HAS_SHUTTER || !gAddonsInitialized) {
    return;
  }
  // Reattach before movement so the servo receives a fresh position command.
  lastShutterMoveMillis = millis();
  gShutterServo.attach(PIN_SHUTTER_SERVO, kShutterPulseClosedUs, kShutterPulseOpenUs);
  gShutterServo.write(position);
  DebugSerial::printFramedValue("setShutterPosition: position ", position, "");
}

void detachServos() {
  // Drop servo holding torque after a short idle delay to reduce jitter and power draw.
  if (HAS_SHUTTER && gAddonsInitialized && gShutterServo.attached()) {
    if (millis() - lastShutterMoveMillis >= kShutterServoDetachDelayMs) {
      gShutterServo.detach();
      DebugSerial::printFramed("detachServos: shutter servo detached to reduce power and prevent jitter");
    }
  }
}

void toggleFlatPanel() {
  // Toggle the flat panel between off and fully-on brightness levels.
  if (!HAS_FLAT_FRAME_PANEL || !gAddonsInitialized) {
    return;
  }

  static bool isOn = false;
  isOn = !isOn;
  applyFlatPanelBrightness(isOn ? 255 : 0);
}

void toggleShutter() {
  // Toggle the shutter between fully open and fully closed positions.
  if (!HAS_SHUTTER || !gAddonsInitialized) {
    return;
  }
  static bool isOpen = false;
  isOpen = !isOpen;
  setShutterPosition(isOpen ? kShutterOpenDegrees : kShutterClosedDegrees);
}

} // namespace Addons