#pragma once

#include <OpenMenuOS.h>
#include <FastAccelStepper.h>
#include "config.h"
#include "star_map_renderer.h"

// OpenMenuOS library globals used by built-in screen input handlers.
extern ScreenManager screenManager;
extern MenuScreen mainMenu;
extern FastAccelStepper* focuserStepper;
extern bool motorEnabled;
extern long savedPresetPositionSteps;
extern int buttonVoltage;
extern int BUTTON_UP_PIN;
extern int BUTTON_DOWN_PIN;
extern int BUTTON_SELECT_PIN;
extern int prevSelectState;
extern float idleMapCenterRaDeg;
extern float idleMapCenterDecDeg;

/*
 * Custom OpenMenuOS screens used as extension points for the focuser UI.
 */
namespace CustomScreens {
static constexpr int kShortPressTimeMs = 300;
static constexpr int kLongPressTimeMs = 500;
static constexpr int kSelectLongPressMs = 300;

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
    static constexpr int kRightBarWidth = 14;
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

    drawStarMap(0, 0, mapWidth, mapHeight, kMapFovDeg);

    // Bottom status bar must span the full width, including the right edge.
    canvas.fillRect(0, bottomBarY, screenWidth, kBottomBarHeight, IDLE_COLOR_BOTTOM_BAR);

    // Right-side vertical placeholder bar for focuser position visualization.
    canvas.fillRect(rightBarX, 0, kRightBarWidth, screenHeight, IDLE_COLOR_RIGHT_BAR);
    canvas.drawRect(rightBarX, 0, kRightBarWidth, screenHeight, IDLE_COLOR_RIGHT_BAR_BORDER);

    // Bottom status bar content: left-aligned reticle icon + target field.
    canvas.setTextColor(IDLE_COLOR_TEXT, IDLE_COLOR_BOTTOM_BAR);
    canvas.drawString("+", kTextPaddingX, bottomBarY + 4);
    canvas.drawString("Target: ----", kTextPaddingX + 14, bottomBarY + 4);

    // Spacer marker to separate target text from future status fields.
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
    canvas.drawFastHLine(cx - 3, cy, 7, IDLE_COLOR_TEXT);
    canvas.drawFastVLine(cx, cy - 3, 7, IDLE_COLOR_TEXT);
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
    if (!motorEnabled) {
      focuserStepper->stopMove();
      return;
    }
    focuserStepper->enableOutputs();
    focuserStepper->setSpeedInHz(IDLE_JOG_SPEED_STEPS_PER_SEC);
    focuserStepper->runForward();
  }

  void onUpButtonLongPress() {
    // Add Idle screen UP long-press behavior here.
  }

  void onDownButtonPressed() {
    // Add Idle screen DOWN event behavior here.
  }

  void onDownButtonHeld() {
    if (!motorEnabled) {
      focuserStepper->stopMove();
      return;
    }
    focuserStepper->enableOutputs();
    focuserStepper->setSpeedInHz(IDLE_JOG_SPEED_STEPS_PER_SEC);
    focuserStepper->runBackward();
  }

  void onDownButtonLongPress() {
    // Add Idle screen DOWN long-press behavior here.
  }

  void onDirectionButtonsReleased() {
    focuserStepper->stopMove();
  }

  const char *title_;
};

/*
 * Preset screen reserved for future preset-management UI.
 */
class PresetScreen : public Screen {
 public:
  explicit PresetScreen(const char *title = "Preset")
      : title_(title) {
  }

  void draw() override {
    canvas.fillScreen(TFT_BLACK);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.drawString("Preset", 10, 10);
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
  void onSelectShortPress() {
    // Save the current position as the active preset target.
    savedPresetPositionSteps = focuserStepper->getCurrentPosition();
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
    if (!motorEnabled) {
      focuserStepper->stopMove();
      return;
    }
    focuserStepper->enableOutputs();
    focuserStepper->setSpeedInHz(IDLE_JOG_SPEED_STEPS_PER_SEC);
    focuserStepper->runForward();
  }

  void onUpButtonLongPress() {
    // Add Preset screen UP long-press behavior here.
  }

  void onDownButtonPressed() {
    // Add Preset screen DOWN event behavior here.
  }

  void onDownButtonHeld() {
    if (!motorEnabled) {
      focuserStepper->stopMove();
      return;
    }
    focuserStepper->enableOutputs();
    focuserStepper->setSpeedInHz(IDLE_JOG_SPEED_STEPS_PER_SEC);
    focuserStepper->runBackward();
  }

  void onDownButtonLongPress() {
    // Add Preset screen DOWN long-press behavior here.
  }

  void onDirectionButtonsReleased() {
    focuserStepper->stopMove();
  }

  const char *title_;
};

/*
 * Global instances that can be attached directly to menu items.
 */
extern IdleScreen idle;
extern PresetScreen preset;
}
