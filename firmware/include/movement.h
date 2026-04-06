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

void startHoming();
void abortHoming();
void updateHoming();

void updateSoftEndstops();
bool isBusy();

} // namespace Movement
