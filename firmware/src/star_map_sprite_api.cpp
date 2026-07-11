#include "star_map_sprite_api.h"

#include <Arduino.h>
#include <cstring>

namespace StarMap {
namespace {

TFT_eSPI* gDisplay = nullptr;
TFT_eSprite* gSprite = nullptr;
int gSpriteWidth = 0;
int gSpriteHeight = 0;
float gRaDeg = 0.0f;
float gDecDeg = 0.0f;
char gTargetName[32] = {0};

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

  const uint16_t bg = TFT_BLACK;
  const uint16_t border = TFT_DARKGREY;
  const uint16_t accent = TFT_CYAN;
  const uint16_t text = TFT_WHITE;

  gSprite->fillSprite(bg);
  gSprite->drawRect(0, 0, viewport.width, viewport.height, border);

  // Placeholder rendering until real sky projection is wired in.
  const int cx = viewport.width / 2;
  const int cy = viewport.height / 2;
  gSprite->drawFastHLine(0, cy, viewport.width, border);
  gSprite->drawFastVLine(cx, 0, viewport.height, border);
  gSprite->fillCircle(cx, cy, 2, accent);

  char line1[32];
  char line2[32];
  char line3[32];
  snprintf(line1, sizeof(line1), "RA %.2f", gRaDeg);
  snprintf(line2, sizeof(line2), "DEC %.2f", gDecDeg);
  snprintf(line3, sizeof(line3), "FOV %.1f", fovDeg);

  gSprite->setTextColor(text, bg);
  gSprite->setTextSize(1);
  gSprite->drawString("Star Map Placeholder", 4, 4, 1);
  gSprite->drawString(line1, 4, 18, 1);
  gSprite->drawString(line2, 4, 30, 1);
  gSprite->drawString(line3, 4, 42, 1);

  if (gTargetName[0] != '\0') {
    gSprite->drawString(gTargetName, 4, 54, 1);
  }

  return gSprite;
}

}  // namespace StarMap
