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

void startHoming();
void abortHoming();
void updateHoming();
void setSpeedSetting(uint8_t speedSettingIndex);

void updateSoftEndstops();
bool isBusy();

} // namespace Movement
