#pragma once

#include <stdint.h>

namespace StarMapRenderer {

struct Viewport {
  int x;
  int y;
  int width;
  int height;
};

struct StarPoint {
  int x;
  int y;
  uint16_t color;
  uint8_t radius;
};

typedef void (*StarPointCallback)(const StarPoint& point, void* userData);

void forEachProjectedStar(const Viewport& viewport,
                          float fovDeg,
                          float centerRaDeg,
                          float centerDecDeg,
                          StarPointCallback callback,
                          void* userData);

int centerX(const Viewport& viewport);
int centerY(const Viewport& viewport);

}  // namespace StarMapRenderer
