#pragma once

#include <Arduino.h>

namespace SerialCommandHandler {

using CommandCallback = void (*)(HardwareSerial& serial, const char* command, size_t commandLength);

void begin(HardwareSerial& serial = Serial);
void poll();

void handleGetPosition(HardwareSerial& serial, const char* command, size_t commandLength);
void handleHome(HardwareSerial& serial, const char* command, size_t commandLength);
void handleGetTarget(HardwareSerial& serial, const char* command, size_t commandLength);
void handleSetTarget(HardwareSerial& serial, const char* command, size_t commandLength);
void handleGotoPreset(HardwareSerial& serial, const char* command, size_t commandLength);
void handleGetPreset(HardwareSerial& serial, const char* command, size_t commandLength);

// Example command callback declaration:
// void handleSetPosition(HardwareSerial& serial, const char* command, size_t commandLength);

} // namespace SerialCommandHandler
