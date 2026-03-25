#pragma once

// Mutable center coordinates used by idle sky rendering and LX200 handlers.
extern float idleMapCenterRaDeg;
extern float idleMapCenterDecDeg;

// Processes pending Meade LX200 frames from Serial.
void handleSerialLx200();
