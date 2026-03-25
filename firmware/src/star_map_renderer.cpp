#include "star_map_renderer.h"

#include <Arduino.h>
#include <math.h>
#include "config.h"
#include IDLE_STAR_CATALOG_HEADER

namespace {

static float wrapSignedDegrees(float deg) {
  while (deg > 180.0f) {
    deg -= 360.0f;
  }
  while (deg < -180.0f) {
    deg += 360.0f;
  }
  return deg;
}

static float clampDec(float decDeg) {
  if (decDeg > 90.0f) {
    return 90.0f;
  }
  if (decDeg < -90.0f) {
    return -90.0f;
  }
  return decDeg;
}

static uint16_t starColorFromMagnitudeTenths(int8_t magTenths) {
  uint8_t redBits = 8;
  if (magTenths <= 10) {
    redBits = 31;
  } else if (magTenths <= 20) {
    redBits = 24;
  } else if (magTenths <= 30) {
    redBits = 18;
  } else if (magTenths <= 40) {
    redBits = 13;
  }
  return (uint16_t)(redBits << 11);
}

static uint8_t starRadiusFromMagnitudeTenths(int8_t magTenths) {
  if (magTenths <= 10) {
    return 2;
  }
  if (magTenths <= 28) {
    return 1;
  }
  return 0;
}

}  // namespace

namespace StarMapRenderer {

void forEachProjectedStar(const Viewport& viewport,
                          float fovDeg,
                          float centerRaDeg,
                          float centerDecDeg,
                          StarPointCallback callback,
                          void* userData) {
  if (callback == nullptr || viewport.width <= 2 || viewport.height <= 2 || fovDeg <= 0.0f) {
    return;
  }

  const float dec = clampDec(centerDecDeg);
  const float halfFovDeg = fovDeg * 0.5f;
  const float invFov = 1.0f / fovDeg;
  const float cosDec = cosf(dec * 0.01745329252f);
  const float raScale = (fabsf(cosDec) < 0.1f) ? 0.1f : fabsf(cosDec);

  const size_t starCount = sizeof(g_stars) / sizeof(g_stars[0]);
  for (size_t i = 0; i < starCount; ++i) {
    StarRec star;
    memcpy_P(&star, &g_stars[i], sizeof(StarRec));

    const float raDeg = ((float)star.ra_u16 * 360.0f) / 65535.0f;
    const float starDec = ((float)star.dec_i16 * 90.0f) / 32767.0f;

    const float dRaDeg = wrapSignedDegrees(raDeg - centerRaDeg) * raScale;
    const float dDecDeg = starDec - dec;

    if (fabsf(dRaDeg) > halfFovDeg || fabsf(dDecDeg) > halfFovDeg) {
      continue;
    }

    const float nx = (dRaDeg + halfFovDeg) * invFov;
    const float ny = (halfFovDeg - dDecDeg) * invFov;

    StarPoint point;
    point.x = viewport.x + 1 + (int)(nx * (float)(viewport.width - 3));
    point.y = viewport.y + 1 + (int)(ny * (float)(viewport.height - 3));
    point.color = starColorFromMagnitudeTenths(star.mag_tenths);
    point.radius = starRadiusFromMagnitudeTenths(star.mag_tenths);
    callback(point, userData);
  }
}

int centerX(const Viewport& viewport) {
  return viewport.x + (viewport.width / 2);
}

int centerY(const Viewport& viewport) {
  return viewport.y + (viewport.height / 2);
}

}  // namespace StarMapRenderer
