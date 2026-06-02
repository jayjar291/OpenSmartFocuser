#include "addons.h"
#include "debug_serial.h"

#include <Arduino.h>
#if HAS_SHUTTER
#include <ESP32Servo.h>
#endif
#if Flat_Frame_Neopixel
#define NEO_RMT_CHANNEL 1
#define FASTLED_RMT_BUILTIN_DRIVER 0
#include <FastLED.h>

#endif
#include "config.h"

namespace Addons {

namespace {

constexpr uint16_t kShutterPulseClosedUs = 500;
constexpr uint16_t kShutterPulseOpenUs = 2500;
long lastmoveMills = 0;

bool gApiInitialized = false;
bool gAddonsInitialized = false;
Servo gShutterServo;

CRGB leds[Flat_Frame_Neopixel_Count];
//Adafruit_NeoPixel gFlatPanelPixels(Flat_Frame_Neopixel_Count, PIN_FLAT_FRAME_PANEL, NEO_GRB + NEO_KHZ800);

void applyFlatPanelBrightness(uint8_t brightness) {
  if (Flat_Frame_Neopixel) {
    fill_solid(leds, Flat_Frame_Neopixel_Count, CRGB(brightness, brightness, brightness));
    FastLED.show();
    return;
  }
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

  #if HAS_SHUTTER
  ESP32PWM::allocateTimer(0); // Allocate PWM timer 0 for shutter servo
  ESP32PWM::allocateTimer(1); // Allocate PWM timer 1 for shutter servo
  gShutterServo.setPeriodHertz(50);
  gShutterServo.attach(PIN_SHUTTER_SERVO, kShutterPulseClosedUs, kShutterPulseOpenUs);
  gShutterServo.write(0);
  #endif

  #if HAS_FLAT_FRAME_PANEL
    if (Flat_Frame_Neopixel) {
      FastLED.addLeds<                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       , PIN_FLAT_FRAME_PANEL, GRB>(leds, Flat_Frame_Neopixel_Count);
      fill_solid(leds, Flat_Frame_Neopixel_Count, CRGB::Black);
      FastLED.show();
    } else {
      pinMode(PIN_FLAT_FRAME_PANEL, OUTPUT);
    }
    applyFlatPanelBrightness(0);
  #endif

  gAddonsInitialized = true;
  #if HAS_SHUTTER
  gShutterServo.detach(); // Ensure servo is not powered until explicitly commanded
  #endif
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
  lastmoveMills = millis();
  gShutterServo.attach(PIN_SHUTTER_SERVO, kShutterPulseClosedUs, kShutterPulseOpenUs);
  gShutterServo.write(position);
  DebugSerial::printFramedValue("SetShutterPosition: position ", position, "");
}

void detachServos() {
  if (HAS_SHUTTER && gAddonsInitialized && gShutterServo.attached()) {
    if(millis() - lastmoveMills >= 350) { // Only detach if it's been a while since the last move command, to avoid unnecessary detach/attach cycles
      gShutterServo.detach();
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
  SetShutterPosition(isOpen ? 180 : 0);  
}

} // namespace Addons