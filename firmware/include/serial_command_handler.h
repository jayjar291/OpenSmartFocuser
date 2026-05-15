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
void handleClearTarget(const char* parameters, size_t parametersLength);
void handleGotoPreset(const char* parameters, size_t parametersLength);
void handleGetPreset(const char* parameters, size_t parametersLength);
void handleListPresets(const char* parameters, size_t parametersLength);
void handleAddPreset(const char* parameters, size_t parametersLength);
void handleSetPreset(const char* parameters, size_t parametersLength);
void handleRemovePreset(const char* parameters, size_t parametersLength);
void handleGetCurrent(const char* parameters, size_t parametersLength);
void handleGetMicrosteps(const char* parameters, size_t parametersLength);
void handleGetMovement(const char* parameters, size_t parametersLength);
void handleGetSpeed(const char* parameters, size_t parametersLength);
void handleSetPosition(const char* parameters, size_t parametersLength);
void handleGetFirmwareVersion(const char* parameters, size_t parametersLength);
void handleGetLimits(const char* parameters, size_t parametersLength);
void handleHeartbeat(const char* parameters, size_t parametersLength);
void handleMoveAbsolute(const char* parameters, size_t parametersLength);
void handleMoveRelative(const char* parameters, size_t parametersLength);
void handleHalt(const char* parameters, size_t parametersLength);
void handleSetSpeed(const char* parameters, size_t parametersLength);
void handleReboot(const char* parameters, size_t parametersLength);
void handleToggleMotor(const char* parameters, size_t parametersLength);
void handleDisableMotor(const char* parameters, size_t parametersLength);
void handleEnableMotor(const char* parameters, size_t parametersLength);

} // namespace SerialCommandHandler
