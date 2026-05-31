#include "addons.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ESP32Servo.h>

#include "config.h"

namespace Addons {

namespace {

constexpr uint16_t kShutterPulseClosedUs = 500;
constexpr uint16_t kShutterPulseOpenUs = 2500;

bool gApiInitialized = false;
bool gAddonsInitialized = false;
Servo gShutterServo;
Adafruit_NeoPixel gFlatPanelPixels(Flat_Frame_Neopixel_Count, PIN_FLAT_FRAME_PANEL, NEO_GRB + NEO_KHZ800);

void applyFlatPanelBrightness(uint8_t brightness) {
  if (Flat_Frame_Neopixel) {
    const uint32_t color = gFlatPanelPixels.Color(brightness, brightness, brightness);
    for (uint16_t index = 0; index < Flat_Frame_Neopixel_Count; ++index) {
      gFlatPanelPixels.setPixelColor(index, color);
    }
    gFlatPanelPixels.show();
    return;
  }

  analogWrite(PIN_FLAT_FRAME_PANEL, brightness);
}

} // namespace

void begin() {
  gApiInitialized = true;
}

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
  if (!gApiInitialized || gAddonsInitialized) {
    return;
  }

  if (HAS_SHUTTER) {
    gShutterServo.setPeriodHertz(50);
    gShutterServo.attach(PIN_SHUTTER_SERVO, kShutterPulseClosedUs, kShutterPulseOpenUs);
    gShutterServo.write(0);
  }

  if (HAS_FLAT_FRAME_PANEL) {
    if (Flat_Frame_Neopixel) {
      gFlatPanelPixels.begin();
      gFlatPanelPixels.clear();
      gFlatPanelPixels.show();
    } else {
      pinMode(PIN_FLAT_FRAME_PANEL, OUTPUT);
    }
    applyFlatPanelBrightness(0);
  }

  gAddonsInitialized = true;
}

void SetFlatPanelBrightness(uint8_t brightness) {
  if (!HAS_FLAT_FRAME_PANEL || !gAddonsInitialized) {
    return;
  }

  applyFlatPanelBrightness(brightness);
}

void SetShutterPosition(uint8_t position) {
  if (!HAS_SHUTTER || !gAddonsInitialized) {
    return;
  }
  gShutterServo.write(position);
}

} // namespace Addons