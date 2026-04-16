#pragma once

#include <OpenMenuOS.h>
#include <FastAccelStepper.h>
#include <cstring>
#include <stdio.h>
#include "config.h"
#include "lucide28.h"
#include "movement.h"
#include "preset.h"
#include "star_map_renderer.h"

// OpenMenuOS library globals used by built-in screen input handlers.
extern ScreenManager screenManager;
extern MenuScreen mainMenu;
extern long savedPresetPositionSteps;
extern int buttonVoltage;
extern int BUTTON_UP_PIN;
extern int BUTTON_DOWN_PIN;
extern int BUTTON_SELECT_PIN;
extern int prevSelectState;
extern float idleMapCenterRaDeg;
extern float idleMapCenterDecDeg;
void notifyPresetMenuDataChanged();

/*
 * Custom OpenMenuOS screens used as extension points for the focuser UI.
 */
namespace CustomScreens {
static constexpr int kShortPressTimeMs = 300;
static constexpr int kLongPressTimeMs = 500;
static constexpr int kSelectLongPressMs = 300;
static constexpr uint16_t kIdleStatusIconColor = 0x07E0;  // Green
// Lucide unicode icons provided by user font.
static constexpr const char* kIconTelescope = "\xEF\x8F\xAF";
static constexpr const char* kIconTarget = "\xEF\x86\xBC";

/*
 * Idle screen shown when no active menu interaction is needed.
 */
class IdleScreen : public Screen {
 public:
  explicit IdleScreen(const char *title = "Idle")
      : title_(title) {
  }

  void draw() override {
    static constexpr int kBottomBarHeight = 20;
    static constexpr int kRightBarWidth = 22;
    static constexpr int kTextPaddingX = 4;
    static constexpr float kMapFovDeg = 60.0f;

    int screenWidth = canvas.width();
    int screenHeight = canvas.height();
    int bottomBarY = screenHeight - kBottomBarHeight;
    int rightBarX = screenWidth - kRightBarWidth;
    int mapWidth = rightBarX;
    int mapHeight = bottomBarY;

    // Clear the full screen first so non-bar regions stay blank.
    canvas.fillScreen(IDLE_COLOR_BG);

    canvas.loadFont(lucide28);
    drawStarMap(0, 0, mapWidth, mapHeight, kMapFovDeg);
    canvas.unloadFont();

    // Right-side vertical bar for focuser position visualization.
    canvas.fillRect(rightBarX, 0, kRightBarWidth, mapHeight, IDLE_COLOR_RIGHT_BAR);
    canvas.drawRect(rightBarX, 0, kRightBarWidth, mapHeight, IDLE_COLOR_RIGHT_BAR_BORDER);

    // Bottom status bar must span the full width, including under the right bar area.
    canvas.fillRect(0, bottomBarY, screenWidth, kBottomBarHeight, IDLE_COLOR_BOTTOM_BAR);

    // Draw focuser position marker icon inside the right bar.
    int32_t pos = Movement::getCurrentPositionSteps();
    if (pos < FOCUSER_SOFT_MIN_STEPS) {
      pos = FOCUSER_SOFT_MIN_STEPS;
    } else if (pos > FOCUSER_SOFT_MAX_STEPS) {
      pos = FOCUSER_SOFT_MAX_STEPS;
    }

    const int barTop = 1;
    const int barBottom = bottomBarY - 2;
    const int barHeight = barBottom - barTop;
    const int32_t range = FOCUSER_SOFT_MAX_STEPS - FOCUSER_SOFT_MIN_STEPS;

    int markerY = barBottom;
    if (range > 0) {
      // 0 mm at bottom, max mm at top.
      markerY = barBottom - static_cast<int>(
          (static_cast<int64_t>(pos - FOCUSER_SOFT_MIN_STEPS) * barHeight) / range);
    }

    const int indicatorY = constrain(markerY, barTop + 2, barBottom - 2);
    const int indicatorX = rightBarX + 4;
    const int indicatorW = kRightBarWidth - 8;
    canvas.fillRect(indicatorX, indicatorY, indicatorW, 2, IDLE_COLOR_TEXT);

    // Bottom status bar content.
    canvas.setTextColor(kIdleStatusIconColor, IDLE_COLOR_BOTTOM_BAR);
    canvas.loadFont(lucide28);
    canvas.drawString(kIconTelescope, kTextPaddingX, bottomBarY + 3);
    canvas.unloadFont();
    canvas.drawFastVLine(28, bottomBarY + 3, kBottomBarHeight - 6, IDLE_COLOR_SPACER);
    canvas.setTextColor(IDLE_COLOR_TEXT, IDLE_COLOR_BOTTOM_BAR);
    canvas.drawString(Movement::isBusy() ? "Homing" : "Idle", 34, bottomBarY + 4);

    char posMmText[12];
    const float posMm = static_cast<float>(pos) / static_cast<float>(FOCUSER_STEPS_PER_MM);
    snprintf(posMmText, sizeof(posMmText), "%.2f", posMm);
    canvas.drawRightString(posMmText, screenWidth - 2, bottomBarY + 4, 1);

    // Spacer marker to separate status text from future fields.
    canvas.drawFastVLine(120, bottomBarY + 3, kBottomBarHeight - 6, IDLE_COLOR_SPACER);
  }

  void handleInput() override {
    // --- SELECT button detection ---
    static bool selectClicked = false;
    static bool longPressHandled = false;
    static unsigned long selectPressTime = 0;

    if (digitalRead(BUTTON_SELECT_PIN) == buttonVoltage) {
      if (!selectClicked && !longPressHandled) {
        selectPressTime = millis();
        selectClicked = true;
      } else if ((millis() - selectPressTime >= kSelectLongPressMs) && !longPressHandled) {
        onSelectLongPress();
        longPressHandled = true;
      }
    } else if (digitalRead(BUTTON_SELECT_PIN) == !buttonVoltage) {
      if (selectClicked) {
        onSelectShortPress();
        selectClicked = false;
        longPressHandled = false;
      }
    }

    // --- UP button detection ---
    static bool upClicked = false;
    static bool upLongHandled = false;
    static unsigned long upPressTime = 0;

    if (digitalRead(BUTTON_UP_PIN) == buttonVoltage) {
      if (!upClicked) {
        upPressTime = millis();
        upClicked = true;
        upLongHandled = false;
      } else if (!upLongHandled && (millis() - upPressTime >= kLongPressTimeMs)) {
        onUpButtonLongPress();
        upLongHandled = true;
      }
    } else if (upClicked && digitalRead(BUTTON_UP_PIN) == !buttonVoltage) {
      unsigned long upReleasedTime = millis();
      long upPressDuration = upReleasedTime - upPressTime;
      if (upPressDuration < kShortPressTimeMs) {
        onUpButtonPressed();
      }
      upClicked = false;
    }

    // --- DOWN button detection ---
    static bool downClicked = false;
    static bool downLongHandled = false;
    static unsigned long downPressTime = 0;

    if (digitalRead(BUTTON_DOWN_PIN) == buttonVoltage) {
      if (!downClicked) {
        downPressTime = millis();
        downClicked = true;
        downLongHandled = false;
      } else if (!downLongHandled && (millis() - downPressTime >= kLongPressTimeMs)) {
        onDownButtonLongPress();
        downLongHandled = true;
      }
    } else if (downClicked && digitalRead(BUTTON_DOWN_PIN) == !buttonVoltage) {
      unsigned long downReleasedTime = millis();
      long downPressDuration = downReleasedTime - downPressTime;
      if (downPressDuration < kShortPressTimeMs) {
        onDownButtonPressed();
      }
      downClicked = false;
    }

    // --- Continuous jog while direction buttons are held ---
    bool upHeld = (digitalRead(BUTTON_UP_PIN) == buttonVoltage);
    bool downHeld = (digitalRead(BUTTON_DOWN_PIN) == buttonVoltage);
    if (upHeld && !downHeld) {
      onUpButtonHeld();
    } else if (downHeld && !upHeld) {
      onDownButtonHeld();
    } else {
      onDirectionButtonsReleased();
    }

    draw();
  }

  const char *getTitle() const override {
    return title_;
  }

 private:
  static void drawProjectedStar(const StarMapRenderer::StarPoint& point, void* userData) {
    (void)userData;
    if (point.radius > 0) {
      canvas.fillCircle(point.x, point.y, point.radius, point.color);
    } else {
      canvas.drawPixel(point.x, point.y, point.color);
    }
  }

  void drawStarMap(int x, int y, int width, int height, float fovDeg) {
    if (width <= 2 || height <= 2) {
      return;
    }

    // Draw a subtle border around the map viewport.
    canvas.drawRect(x, y, width, height, IDLE_COLOR_SPACER);

    StarMapRenderer::Viewport viewport = {x, y, width, height};
    StarMapRenderer::forEachProjectedStar(
        viewport,
        fovDeg,
        idleMapCenterRaDeg,
        idleMapCenterDecDeg,
        &IdleScreen::drawProjectedStar,
        this);

    // Center marker for configured pointing target.
    const int cx = StarMapRenderer::centerX(viewport);
    const int cy = StarMapRenderer::centerY(viewport);
    canvas.setTextColor(IDLE_COLOR_TEXT, IDLE_COLOR_BG);
    canvas.drawString(kIconTarget, cx - 3, cy - 5);
  }

  void onSelectShortPress() {
    // Same as library CustomScreen: no short-press action by default.
  }

  void onSelectLongPress() {
    // Idle-screen long press opens the main menu.
    screenManager.pushScreen(&mainMenu);
    prevSelectState = buttonVoltage;
  }

  void onUpButtonPressed() {
    // Add Idle screen UP event behavior here.
  }

  void onUpButtonHeld() {
    Movement::jogForward();
  }

  void onUpButtonLongPress() {
    // Add Idle screen UP long-press behavior here.
  }

  void onDownButtonPressed() {
    // Add Idle screen DOWN event behavior here.
  }

  void onDownButtonHeld() {
    Movement::jogBackward();
  }

  void onDownButtonLongPress() {
    // Add Idle screen DOWN long-press behavior here.
  }

  void onDirectionButtonsReleased() {
    Movement::stopJog();
  }

  const char *title_;
};

/*
 * Preset screen reserved for future preset-management UI.
 */
class PresetScreen : public Screen {
 public:
  explicit PresetScreen(const char *title = "Preset", uint8_t presetId = 0)
    : title_(title), presetId_(presetId) {
    setNameInternal(title);
  }

  void configureForAdd(const char* presetName) {
    mode_ = SaveMode::kAdd;
    presetId_ = 0;
    setNameInternal(presetName);
  }

  void configureForEdit(uint8_t presetId, const char* presetName) {
    mode_ = SaveMode::kEdit;
    presetId_ = presetId;
    setNameInternal(presetName);
  }

  void draw() override {
    static constexpr int kBottomBarHeight = 20;
    static constexpr int kRightBarWidth = 22;
    static constexpr int kTextPaddingX = 4;

    int screenWidth = canvas.width();
    int screenHeight = canvas.height();
    int bottomBarY = screenHeight - kBottomBarHeight;
    int rightBarX = screenWidth - kRightBarWidth;
    int panelWidth = rightBarX;
    int panelHeight = bottomBarY;

    // Base background.
    canvas.fillScreen(IDLE_COLOR_BG);

    // Main preset content area (replaces idle star map).
    canvas.drawRect(0, 0, panelWidth, panelHeight, IDLE_COLOR_SPACER);
    canvas.setTextColor(IDLE_COLOR_TEXT, IDLE_COLOR_BG);
    canvas.drawString(title_, 8, 8);
    canvas.drawString("SELECT: Save current", 8, 30);
    canvas.drawString("UP/DOWN: Jog focus", 8, 46);
    canvas.drawString("Hold SELECT: Back", 8, 62);

    // Right-side position bar.
    canvas.fillRect(rightBarX, 0, kRightBarWidth, panelHeight, IDLE_COLOR_RIGHT_BAR);
    canvas.drawRect(rightBarX, 0, kRightBarWidth, panelHeight, IDLE_COLOR_RIGHT_BAR_BORDER);

    // Bottom status bar across full width.
    canvas.fillRect(0, bottomBarY, screenWidth, kBottomBarHeight, IDLE_COLOR_BOTTOM_BAR);

    int32_t pos = Movement::getCurrentPositionSteps();
    if (pos < FOCUSER_SOFT_MIN_STEPS) {
      pos = FOCUSER_SOFT_MIN_STEPS;
    } else if (pos > FOCUSER_SOFT_MAX_STEPS) {
      pos = FOCUSER_SOFT_MAX_STEPS;
    }

    const int barTop = 1;
    const int barBottom = bottomBarY - 2;
    const int barHeight = barBottom - barTop;
    const int32_t range = FOCUSER_SOFT_MAX_STEPS - FOCUSER_SOFT_MIN_STEPS;

    int markerY = barBottom;
    if (range > 0) {
      markerY = barBottom - static_cast<int>(
          (static_cast<int64_t>(pos - FOCUSER_SOFT_MIN_STEPS) * barHeight) / range);
    }

    const int indicatorY = constrain(markerY, barTop + 2, barBottom - 2);
    const int indicatorX = rightBarX + 4;
    const int indicatorW = kRightBarWidth - 8;
    canvas.fillRect(indicatorX, indicatorY, indicatorW, 2, IDLE_COLOR_TEXT);

    // Bottom status content.
    canvas.setTextColor(kIdleStatusIconColor, IDLE_COLOR_BOTTOM_BAR);
    canvas.loadFont(lucide28);
    canvas.drawString(kIconTelescope, kTextPaddingX, bottomBarY + 3);
    canvas.unloadFont();
    canvas.drawFastVLine(28, bottomBarY + 3, kBottomBarHeight - 6, IDLE_COLOR_SPACER);
    canvas.setTextColor(IDLE_COLOR_TEXT, IDLE_COLOR_BOTTOM_BAR);
    canvas.drawString(Movement::isBusy() ? "Homing" : "Preset", 34, bottomBarY + 4);

    char posMmText[12];
    const float posMm = static_cast<float>(pos) / static_cast<float>(FOCUSER_STEPS_PER_MM);
    snprintf(posMmText, sizeof(posMmText), "%.2f", posMm);
    canvas.drawRightString(posMmText, screenWidth - 2, bottomBarY + 4, 1);

    canvas.drawFastVLine(120, bottomBarY + 3, kBottomBarHeight - 6, IDLE_COLOR_SPACER);
  }

  void handleInput() override {
    // --- SELECT button detection ---
    static bool selectClicked = false;
    static bool longPressHandled = false;
    static unsigned long selectPressTime = 0;

    if (digitalRead(BUTTON_SELECT_PIN) == buttonVoltage) {
      if (!selectClicked && !longPressHandled) {
        selectPressTime = millis();
        selectClicked = true;
      } else if ((millis() - selectPressTime >= kSelectLongPressMs) && !longPressHandled) {
        onSelectLongPress();
        longPressHandled = true;
      }
    } else if (digitalRead(BUTTON_SELECT_PIN) == !buttonVoltage) {
      if (selectClicked) {
        onSelectShortPress();
        selectClicked = false;
        longPressHandled = false;
      }
    }

    // --- UP button detection ---
    static bool upClicked = false;
    static bool upLongHandled = false;
    static unsigned long upPressTime = 0;

    if (digitalRead(BUTTON_UP_PIN) == buttonVoltage) {
      if (!upClicked) {
        upPressTime = millis();
        upClicked = true;
        upLongHandled = false;
      } else if (!upLongHandled && (millis() - upPressTime >= kLongPressTimeMs)) {
        onUpButtonLongPress();
        upLongHandled = true;
      }
    } else if (upClicked && digitalRead(BUTTON_UP_PIN) == !buttonVoltage) {
      unsigned long upReleasedTime = millis();
      long upPressDuration = upReleasedTime - upPressTime;
      if (upPressDuration < kShortPressTimeMs) {
        onUpButtonPressed();
      }
      upClicked = false;
    }

    // --- DOWN button detection ---
    static bool downClicked = false;
    static bool downLongHandled = false;
    static unsigned long downPressTime = 0;

    if (digitalRead(BUTTON_DOWN_PIN) == buttonVoltage) {
      if (!downClicked) {
        downPressTime = millis();
        downClicked = true;
        downLongHandled = false;
      } else if (!downLongHandled && (millis() - downPressTime >= kLongPressTimeMs)) {
        onDownButtonLongPress();
        downLongHandled = true;
      }
    } else if (downClicked && digitalRead(BUTTON_DOWN_PIN) == !buttonVoltage) {
      unsigned long downReleasedTime = millis();
      long downPressDuration = downReleasedTime - downPressTime;
      if (downPressDuration < kShortPressTimeMs) {
        onDownButtonPressed();
      }
      downClicked = false;
    }

    // --- Continuous jog while direction buttons are held ---
    bool upHeld = (digitalRead(BUTTON_UP_PIN) == buttonVoltage);
    bool downHeld = (digitalRead(BUTTON_DOWN_PIN) == buttonVoltage);
    if (upHeld && !downHeld) {
      onUpButtonHeld();
    } else if (downHeld && !upHeld) {
      onDownButtonHeld();
    } else {
      onDirectionButtonsReleased();
    }

    draw();
  }

  const char *getTitle() const override {
    return title_;
  }

 private:
  enum class SaveMode {
    kAdd,
    kEdit
  };

  void setNameInternal(const char* source) {
    if (source == nullptr || source[0] == '\0') {
      strncpy(nameBuffer_, "Preset", sizeof(nameBuffer_) - 1);
      nameBuffer_[sizeof(nameBuffer_) - 1] = '\0';
    } else {
      strncpy(nameBuffer_, source, sizeof(nameBuffer_) - 1);
      nameBuffer_[sizeof(nameBuffer_) - 1] = '\0';
    }
    title_ = nameBuffer_;
  }

  void onSelectShortPress() {
    const int32_t currentSteps = Movement::getCurrentPositionSteps();
    savedPresetPositionSteps = currentSteps;

    bool saved = false;
    if (mode_ == SaveMode::kEdit) {
      saved = preset::set(presetId_, nameBuffer_, currentSteps);
    } else {
      uint8_t newId = 0;
      saved = preset::add(nameBuffer_, currentSteps, newId);
      if (saved) {
        presetId_ = newId;
      }
    }

    if (saved) {
      notifyPresetMenuDataChanged();
      if (mode_ == SaveMode::kAdd) {
        if (screenManager.canGoBack()) {
          screenManager.popScreen();
        }
        if (screenManager.canGoBack()) {
          screenManager.popScreen();
        }
      } else if (screenManager.canGoBack()) {
        screenManager.popScreen();
      }
      prevSelectState = buttonVoltage;
    }
  }

  void onSelectLongPress() {
    // Same as library CustomScreen long-press behavior.
    if (screenManager.canGoBack()) {
      screenManager.popScreen();
    }
    prevSelectState = buttonVoltage;
  }

  void onUpButtonPressed() {
    // Add Preset screen UP event behavior here.
  }

  void onUpButtonHeld() {
    Movement::jogForward();
  }

  void onUpButtonLongPress() {
    // Add Preset screen UP long-press behavior here.
  }

  void onDownButtonPressed() {
    // Add Preset screen DOWN event behavior here.
  }

  void onDownButtonHeld() {
    Movement::jogBackward();
  }

  void onDownButtonLongPress() {
    // Add Preset screen DOWN long-press behavior here.
  }

  void onDirectionButtonsReleased() {
    Movement::stopJog();
  }

  const char *title_;
  char nameBuffer_[preset::kMaxNameLen] = "Preset";
  uint8_t presetId_;
  SaveMode mode_ = SaveMode::kAdd;
};

class DeviceInfoScreen : public Screen {
 public:
  explicit DeviceInfoScreen(const char* title = "Device Info")
      : title_(title) {
  }

  void draw() override {
    int screenWidth = canvas.width();
    int screenHeight = canvas.height();
    static constexpr int kOuterPadding = 6;
    static constexpr int kInnerPadding = 5;
    static constexpr int kColumnGap = 8;
    static constexpr int kLineHeight = 8;
    static constexpr int kHeaderHeight = 15;
    static constexpr int kFooterHeight = 10;

    static constexpr uint16_t kInfoBg = 0x0841;
    static constexpr uint16_t kInfoPanel = 0x10A2;
    static constexpr uint16_t kInfoBorder = 0x2965;
    static constexpr uint16_t kInfoHeader = 0x041A;
    static constexpr uint16_t kInfoHeaderText = 0xFFFF;
    static constexpr uint16_t kInfoKey = 0x7D7C;
    static constexpr uint16_t kInfoValue = 0xEF5D;
    static constexpr uint16_t kInfoAccentGood = 0x87F0;
    static constexpr uint16_t kInfoAccentWarn = 0xFD20;
    static constexpr uint16_t kInfoFooter = 0xBDF7;

    const int panelX = kOuterPadding;
    const int panelY = kOuterPadding;
    const int panelW = screenWidth - (kOuterPadding * 2);
    const int panelH = screenHeight - (kOuterPadding * 2);
    const int contentTop = panelY + kHeaderHeight + kInnerPadding;
    const int contentBottom = panelY + panelH - kFooterHeight - kInnerPadding;
    const int columnWidth = (panelW - (kInnerPadding * 2) - kColumnGap) / 2;
    const int leftX = panelX + kInnerPadding;
    const int rightX = leftX + columnWidth + kColumnGap;
    const int leftValueRightX = leftX + columnWidth - 2;
    const int rightValueRightX = rightX + columnWidth - 2;

    canvas.fillScreen(kInfoBg);
    canvas.fillRoundRect(panelX, panelY, panelW, panelH, 5, kInfoPanel);
    canvas.drawRoundRect(panelX, panelY, panelW, panelH, 5, kInfoBorder);
    canvas.fillRoundRect(panelX + 1, panelY + 1, panelW - 2, kHeaderHeight, 4, kInfoHeader);
    canvas.drawFastHLine(leftX, contentTop - 3, panelW - (kInnerPadding * 2), kInfoBorder);
    canvas.drawFastVLine(leftX + columnWidth + (kColumnGap / 2), contentTop - 1, contentBottom - contentTop + 2, kInfoBorder);

    canvas.setTextFont(1);
    canvas.setTextSize(1);
    canvas.setTextColor(kInfoHeaderText, kInfoHeader);
    canvas.drawCentreString(title_, panelX + (panelW / 2), panelY + 4, 1);

    char valueBuffer[24];
    int leftY = contentTop;
    int rightY = contentTop;

    drawKeyValueLine(leftX, leftValueRightX, leftY, "Motor", Movement::isMotorEnabled() ? "ON" : "OFF",
                     Movement::isMotorEnabled() ? kInfoAccentGood : kInfoAccentWarn);
    leftY += kLineHeight;

    drawKeyValueLine(leftX, leftValueRightX, leftY, "UART", Movement::isUartConnected() ? "OK" : "FAIL",
                     Movement::isUartConnected() ? kInfoAccentGood : kInfoAccentWarn);
    leftY += kLineHeight;

    drawKeyValueLine(leftX, leftValueRightX, leftY, "Busy", Movement::isBusy() ? "YES" : "NO",
                     Movement::isBusy() ? kInfoAccentWarn : kInfoValue);
    leftY += kLineHeight;

    snprintf(valueBuffer, sizeof(valueBuffer), "%.2fmm", static_cast<float>(Movement::getCurrentPositionSteps()) / static_cast<float>(FOCUSER_STEPS_PER_MM));
    drawKeyValueLine(leftX, leftValueRightX, leftY, "Pos", valueBuffer);
    leftY += kLineHeight;

    char bytesA[12];
    formatBytesCompact(ESP.getFreeHeap(), bytesA, sizeof(bytesA));
    drawKeyValueLine(leftX, leftValueRightX, leftY, "Heap Free", bytesA);
    leftY += kLineHeight;

    formatBytesCompact(ESP.getMinFreeHeap(), bytesA, sizeof(bytesA));
    drawKeyValueLine(leftX, leftValueRightX, leftY, "Heap Min", bytesA);
    leftY += kLineHeight;

    formatBytesCompact(ESP.getHeapSize(), bytesA, sizeof(bytesA));
    drawKeyValueLine(leftX, leftValueRightX, leftY, "Heap Tot", bytesA);
    leftY += kLineHeight;

    formatBytesCompact(ESP.getMaxAllocHeap(), bytesA, sizeof(bytesA));
    drawKeyValueLine(leftX, leftValueRightX, leftY, "Heap Max", bytesA);
    leftY += kLineHeight;

    if (psramFound()) {
      snprintf(valueBuffer, sizeof(valueBuffer), "ON");
    } else {
      snprintf(valueBuffer, sizeof(valueBuffer), "OFF");
    }
    drawKeyValueLine(rightX, rightValueRightX, rightY, "PSRAM", valueBuffer,
                     psramFound() ? kInfoAccentGood : kInfoAccentWarn);
    rightY += kLineHeight;

    formatBytesCompact(ESP.getFreePsram(), bytesA, sizeof(bytesA));
    drawKeyValueLine(rightX, rightValueRightX, rightY, "PS Free", bytesA,
                     psramFound() ? kInfoValue : kInfoAccentWarn);
    rightY += kLineHeight;

    formatBytesCompact(ESP.getPsramSize(), bytesA, sizeof(bytesA));
    drawKeyValueLine(rightX, rightValueRightX, rightY, "PS Tot", bytesA,
                     psramFound() ? kInfoValue : kInfoAccentWarn);
    rightY += kLineHeight;

    formatBytesCompact(ESP.getMaxAllocPsram(), bytesA, sizeof(bytesA));
    drawKeyValueLine(rightX, rightValueRightX, rightY, "PS Max", bytesA,
                     psramFound() ? kInfoValue : kInfoAccentWarn);
    rightY += kLineHeight;

    snprintf(valueBuffer, sizeof(valueBuffer), "%umA %uuS",
             static_cast<unsigned>(Movement::getDriverCurrentMa()),
             static_cast<unsigned>(Movement::getDriverMicrosteps()));
    drawKeyValueLine(rightX, rightValueRightX, rightY, "Driver", valueBuffer);
    rightY += kLineHeight;

    snprintf(valueBuffer, sizeof(valueBuffer), "%luMHz", static_cast<unsigned long>(ESP.getCpuFreqMHz()));
    drawKeyValueLine(rightX, rightValueRightX, rightY, "CPU", valueBuffer);
    rightY += kLineHeight;

    snprintf(valueBuffer, sizeof(valueBuffer), "%luKB", static_cast<unsigned long>(ESP.getFreeSketchSpace() / 1024UL));
    drawKeyValueLine(rightX, rightValueRightX, rightY, "Flash Free", valueBuffer);
    rightY += kLineHeight;

    formatUptime(millis(), valueBuffer, sizeof(valueBuffer));
    drawKeyValueLine(rightX, rightValueRightX, rightY, "Uptime", valueBuffer);

    canvas.setTextColor(kInfoFooter, kInfoPanel);
    canvas.drawCentreString("Hold SELECT to go back", panelX + (panelW / 2), panelY + panelH - kFooterHeight, 1);
  }

  void handleInput() override {
    static bool selectClicked = false;
    static bool longPressHandled = false;
    static unsigned long selectPressTime = 0;

    if (digitalRead(BUTTON_SELECT_PIN) == buttonVoltage) {
      if (!selectClicked && !longPressHandled) {
        selectPressTime = millis();
        selectClicked = true;
      } else if ((millis() - selectPressTime >= kSelectLongPressMs) && !longPressHandled) {
        onSelectLongPress();
        longPressHandled = true;
      }
    } else if (digitalRead(BUTTON_SELECT_PIN) == !buttonVoltage) {
      if (selectClicked) {
        selectClicked = false;
        longPressHandled = false;
      }
    }

    draw();
  }

  const char* getTitle() const override {
    return title_;
  }

 private:
  static void formatBytesCompact(uint32_t bytes, char* destination, size_t destinationSize) {
    if (destination == nullptr || destinationSize == 0) {
      return;
    }

    if (bytes >= 1024UL * 1024UL) {
      snprintf(destination, destinationSize, "%luM", static_cast<unsigned long>(bytes / (1024UL * 1024UL)));
    } else if (bytes >= 1024UL) {
      snprintf(destination, destinationSize, "%luK", static_cast<unsigned long>(bytes / 1024UL));
    } else {
      snprintf(destination, destinationSize, "%luB", static_cast<unsigned long>(bytes));
    }
  }

  static void formatUptime(uint32_t uptimeMs, char* destination, size_t destinationSize) {
    const uint32_t totalSeconds = uptimeMs / 1000UL;
    const uint32_t hours = totalSeconds / 3600UL;
    const uint32_t minutes = (totalSeconds % 3600UL) / 60UL;
    const uint32_t seconds = totalSeconds % 60UL;
    snprintf(destination, destinationSize, "%02lu:%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
  }

  void drawKeyValueLine(int x, int valueRightX, int y, const char* key, const char* value,
                        uint16_t valueColor = 0xEF5D) {
    canvas.setTextColor(0x7D7C, 0x10A2);
    canvas.drawString(key, x, y, 1);
    canvas.setTextColor(valueColor, 0x10A2);
    canvas.drawRightString(value, valueRightX, y, 1);
  }

  void onSelectLongPress() {
    if (screenManager.canGoBack()) {
      screenManager.popScreen();
    }
    prevSelectState = buttonVoltage;
  }

  const char* title_;
};

/*
 * Global instances that can be attached directly to menu items.
 */
extern IdleScreen idle;
extern PresetScreen presetScreen;
extern DeviceInfoScreen deviceInfoScreen;
}
