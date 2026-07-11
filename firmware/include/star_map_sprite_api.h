#pragma once

#include <TFT_eSPI.h>

namespace StarMap {

struct Viewport {
  int x;
  int y;
  int width;
  int height;
};

void begin(TFT_eSPI* display);

void setTarget(float raDeg, float decDeg, const char* name);
void clearTarget();
void getTarget(float& raDeg, float& decDeg, const char*& name);

TFT_eSprite* getSprite(const Viewport& viewport, float fovDeg);

}  // namespace StarMap
