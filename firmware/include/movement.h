#pragma once

#include <cstdint>

namespace Movement {

enum class MovementStatus {
  Idle,
  Moving,
  Homing,
  Error
};

MovementStatus getMovementStatus();

void initializeDriver();
void healthCheck();

void setMotorEnabledState(bool enabled);
void toggleMotorEnabled();
bool isMotorEnabled();


void jogForward();
void jogBackward();
void stopJog();

void halt();

void moveToPosition(int32_t targetSteps);
void moveToPositionMm(float targetMm);
void moveRelative(int32_t relativeSteps);

int32_t getCurrentPositionSteps();
void setCurrentPositionSteps(int32_t positionSteps);

void updatePositionPersistence();
void loadPersistentCurrentPosition();
void savePersistentCurrentPosition();
void clearPersistentCurrentPosition();

void startHoming();
void abortHoming();
void updateHoming();

void updateMotorIdleTimeout();
void setSpeedSetting(uint8_t speedSettingIndex);
uint8_t getSpeedSetting();

bool isBusy();
bool isUartConnected();

uint16_t getDriverMicrosteps();
uint16_t getDriverCurrentMa();

void processEndstopEvent();
void updateSoftEndstops();
bool checkSoftEndstops(int32_t targetSteps);
void getLimits(int32_t& minSteps, int32_t& maxSteps);

} // namespace Movement
