#pragma once

#include <Arduino.h>

namespace SerialCommandHandler {

using CommandCallback = void (*)(const char* parameters, size_t parametersLength);

void begin(HardwareSerial& serial = Serial);
void poll();

void handleGetPosition(const char* parameters, size_t parametersLength);
void handleHome(const char* parameters, size_t parametersLength);
void handleGetTarget(const char* parameters, size_t parametersLength);
void handleSetTarget(const char* parameters, size_t parametersLength);
void handleGotoPreset(const char* parameters, size_t parametersLength);
void handleGetPreset(const char* parameters, size_t parametersLength);


} // namespace SerialCommandHandler
