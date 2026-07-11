#include "StarMap.h"
#include "config.h"
#include <Arduino.h>
#include <cstring>
#include <math.h>

#include "lucide28.h"

#include IDLE_STAR_CATALOG_HEADER

namespace StarMap {
namespace {

TFT_eSPI* gDisplay = nullptr;
TFT_eSprite* gSprite = nullptr;
int gSpriteWidth = 0;
int gSpriteHeight = 0;
float gRaDeg = 0.0f;
float gDecDeg = 0.0f;
char gTargetName[32] = {0};
static constexpr const char* kIconTarget = "\xEF\x86\xBC";

float wrapSignedDegrees(float deg) {
  while (deg > 180.0f) {
    deg -= 360.0f;
  }
  while (deg < -180.0f) {
    deg += 360.0f;
  }
  return deg;
}

float clampDec(float decDeg) {
  if (decDeg > 90.0f) {
    return 90.0f;
  }
  if (decDeg < -90.0f) {
    return -90.0f;
  }
  return decDeg;
}

uint16_t starColorFromMagnitudeTenths(int8_t magTenths) {
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

uint8_t starRadiusFromMagnitudeTenths(int8_t magTenths) {
  if (magTenths <= 10) {
    return 2;
  }
  if (magTenths <= 28) {
    return 1;
  }
  return 0;
}

bool ensureSpriteSize(int width, int height) {
  if (gDisplay == nullptr || width <= 0 || height <= 0) {
    return false;
  }

  if (gSprite != nullptr && gSpriteWidth == width && gSpriteHeight == height) {
    return true;
  }

  if (gSprite != nullptr) {
    gSprite->deleteSprite();
    delete gSprite;
    gSprite = nullptr;
  }

  gSprite = new TFT_eSprite(gDisplay);
  if (gSprite == nullptr) {
    gSpriteWidth = 0;
    gSpriteHeight = 0;
    return false;
  }

  gSprite->setColorDepth(16);
  if (gSprite->createSprite(width, height) == nullptr) {
    gSprite->deleteSprite();
    delete gSprite;
    gSprite = nullptr;
    gSpriteWidth = 0;
    gSpriteHeight = 0;
    return false;
  }

  gSpriteWidth = width;
  gSpriteHeight = height;
  return true;
}

}  // namespace

void begin(TFT_eSPI* display) {
  gDisplay = display;
}

void setTarget(float raDeg, float decDeg, const char* name) {
  gRaDeg = raDeg;
  gDecDeg = decDeg;

  if (name == nullptr) {
    gTargetName[0] = '\0';
    return;
  }

  strncpy(gTargetName, name, sizeof(gTargetName) - 1);
  gTargetName[sizeof(gTargetName) - 1] = '\0';
}

void clearTarget() {
  gRaDeg = 0.0f;
  gDecDeg = 0.0f;
  gTargetName[0] = '\0';
}

void getTarget(float& raDeg, float& decDeg, const char*& name) {
  raDeg = gRaDeg;
  decDeg = gDecDeg;
  name = gTargetName;
}

TFT_eSprite* getSprite(const Viewport& viewport, float fovDeg) {
  if (!ensureSpriteSize(viewport.width, viewport.height)) {
    return nullptr;
  }

  if (fovDeg <= 0.0f) {
    return gSprite;
  }

  const uint16_t bg = TFT_BLACK;
  const uint16_t targetColor = TFT_RED;

  gSprite->fillSprite(bg);

  const float centerDec = clampDec(gDecDeg);
  const float halfFovDeg = fovDeg * 0.5f;
  const float invFov = 1.0f / fovDeg;
  const float cosDec = cosf(centerDec * 0.01745329252f);
  const float raScale = (fabsf(cosDec) < 0.1f) ? 0.1f : fabsf(cosDec);

  const int innerLeft = 1;
  const int innerTop = 1;
  const int innerW = viewport.width - 2;
  const int innerH = viewport.height - 2;
  if (innerW <= 0 || innerH <= 0) {
    return gSprite;
  }

  const size_t starCount = sizeof(g_stars) / sizeof(g_stars[0]);
  for (size_t i = 0; i < starCount; ++i) {
    StarRec star;
    memcpy_P(&star, &g_stars[i], sizeof(StarRec));

    const float raDeg = ((float)star.ra_u16 * 360.0f) / 65535.0f;
    const float decDeg = ((float)star.dec_i16 * 90.0f) / 32767.0f;

    const float dRaDeg = wrapSignedDegrees(raDeg - gRaDeg) * raScale;
    const float dDecDeg = decDeg - centerDec;
    if (fabsf(dRaDeg) > halfFovDeg || fabsf(dDecDeg) > halfFovDeg) {
      continue;
    }

    const float nx = (dRaDeg + halfFovDeg) * invFov;
    const float ny = (halfFovDeg - dDecDeg) * invFov;

    const int x = innerLeft + (int)(nx * (float)(innerW - 1));
    const int y = innerTop + (int)(ny * (float)(innerH - 1));

    const uint16_t color = starColorFromMagnitudeTenths(star.mag_tenths);
    const uint8_t radius = starRadiusFromMagnitudeTenths(star.mag_tenths);
    if (radius > 0) {
      gSprite->fillCircle(x, y, radius, color);
    } else {
      gSprite->drawPixel(x, y, color);
    }
  }

  const int cx = viewport.width / 2;
  const int cy = viewport.height / 2;
  gSprite->setTextColor(targetColor, bg);
  gSprite->loadFont(lucide28);
  gSprite->drawString(kIconTarget, cx - 8, cy - 9);
  gSprite->unloadFont();

  return gSprite;
}

}  // namespace StarMap
