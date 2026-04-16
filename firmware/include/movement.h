#pragma once

#include <cstdint>

namespace Movement {

void initializeDriver();

void toggleMotorEnabled();
bool isMotorEnabled();

void jogForward();
void jogBackward();
void stopJog();

void moveToPosition(int32_t targetSteps);
void moveToPositionMm(float targetMm);
int32_t getCurrentPositionSteps();
void setCurrentPositionSteps(int32_t positionSteps);

void updatePositionPersistence();
void loadPersistentCurrentPosition();
void savePersistentCurrentPosition();
void clearPersistentCurrentPosition();

void startHoming();
void abortHoming();
void updateHoming();
void setSpeedSetting(uint8_t speedSettingIndex);

void updateSoftEndstops();
bool isBusy();
bool isUartConnected();
uint16_t getDriverMicrosteps();
uint16_t getDriverCurrentMa();

} // namespace Movement
