#include <Arduino.h>
#include <limits.h>
#include <FastAccelStepper.h>
#include <Preferences.h>
#include <TMCStepper.h>
#include <OpenMenuOS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "custom_screens.h"
#include "movement.h"

/*
 * Hardware interface constants for TMC2209 UART control.
 */
static constexpr float TMC_R_SENSE = 0.11f;
static constexpr uint8_t TMC_STEPPER_UART_INDEX = 1;
// globlal variables for idle star map center coordinates
float idleMapCenterRaDeg = IDLE_STAR_MAP_CENTER_RA_DEG;
float idleMapCenterDecDeg = IDLE_STAR_MAP_CENTER_DEC_DEG;

/*
 * Global hardware driver instances used during initialization and runtime.
 */
HardwareSerial tmcSerial(TMC_STEPPER_UART_INDEX);
FastAccelStepperEngine stepperEngine;
FastAccelStepper* focuserStepper = nullptr;
TMC2209Stepper focuserDriver(&tmcSerial, TMC_R_SENSE, TMC_DRIVER_ADDRESS);
OpenMenuOS menu(PIN_BUTTON_UP, PIN_BUTTON_DOWN, PIN_BUTTON_SELECT);

/*
 * Global motor enable state controlled from the main menu.
 */
bool motorEnabled = false;
long savedPresetPositionSteps = 0;

static TaskHandle_t motorTaskHandle = nullptr;

static void motorTask(void* /*param*/) {
  for (;;) {
    Movement::updateHoming();
    Movement::updateSoftEndstops();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

/*
 * Application menu hierarchy: top-level pages and settings subpages.
 */
static char kTitleMainMenu[] = "Main Menu";
static char kTitleIdle[] = "Idle";
static char kTitlePresets[] = "Presets";
static char kTitleTmcSettings[] = "TMC Settings";
static char kTitleSettings[] = "Settings";

static char kFilterTitle1[] = "Filter 1";
static char kFilterTitle2[] = "Filter 2";
static char kFilterTitle3[] = "Filter 3";
static char kFilterTitle4[] = "Filter 4";
static char kFilterTitle5[] = "Filter 5";
static char kFilterTitle6[] = "Filter 6";
static char kFilterTitle7[] = "Filter 7";
static char kFilterTitle8[] = "Filter 8";
static char kFilterTitle9[] = "Filter 9";
static char kFilterTitle10[] = "Filter 10";
static char kFilterTitle11[] = "Filter 11";
static char kFilterTitle12[] = "Filter 12";

MenuScreen mainMenu(kTitleMainMenu);
CustomScreens::IdleScreen idleScreen(kTitleIdle);
MenuScreen PresetsMenu(kTitlePresets);
MenuScreen TestPOS("Test Positions");
MenuScreen FilterMenu1(kFilterTitle1);
MenuScreen FilterMenu2(kFilterTitle2);
MenuScreen FilterMenu3(kFilterTitle3);
MenuScreen FilterMenu4(kFilterTitle4);
MenuScreen FilterMenu5(kFilterTitle5);
MenuScreen FilterMenu6(kFilterTitle6);
MenuScreen FilterMenu7(kFilterTitle7);
MenuScreen FilterMenu8(kFilterTitle8);
MenuScreen FilterMenu9(kFilterTitle9);
MenuScreen FilterMenu10(kFilterTitle10);
MenuScreen FilterMenu11(kFilterTitle11);
MenuScreen FilterMenu12(kFilterTitle12);

CustomScreens::PresetScreen PresetEdit1(kFilterTitle1, 1);
CustomScreens::PresetScreen PresetEdit2(kFilterTitle2, 2);
CustomScreens::PresetScreen PresetEdit3(kFilterTitle3, 3);
CustomScreens::PresetScreen PresetEdit4(kFilterTitle4, 4);
CustomScreens::PresetScreen PresetEdit5(kFilterTitle5, 5);
CustomScreens::PresetScreen PresetEdit6(kFilterTitle6, 6);
CustomScreens::PresetScreen PresetEdit7(kFilterTitle7, 7);
CustomScreens::PresetScreen PresetEdit8(kFilterTitle8, 8);
CustomScreens::PresetScreen PresetEdit9(kFilterTitle9, 9);
CustomScreens::PresetScreen PresetEdit10(kFilterTitle10, 10);
CustomScreens::PresetScreen PresetEdit11(kFilterTitle11, 11);
CustomScreens::PresetScreen PresetEdit12(kFilterTitle12, 12);

SettingsScreen TMCSettings(kTitleTmcSettings);
SettingsScreen settingsScreen(kTitleSettings);


/*
 * Mutable labels for APIs that expect char* rather than string literals.
 */
static char kPresetFineFocus[] = "Fine focus point";
static char kPresetTestFocus[] = "Test focus point";
static char kPercentUnit[] = "%";
static char kMicrostepUnit[] = "uSteps";
static char kButtonsModeLow[] = "LOW";
static char kMainMenuPresets[] = "Presets";
static char kMainMenuHome[] = "Home";
static char kMainMenuSettings[] = "Settings";
static char kMainMenuToggleMotor[] = "Toggle Motor";
static char kFilterGoto[] = "Goto";
static char kFilterEdit[] = "Edit";
static char kFilterClear[] = "Clear";
static char kSettingWifi[] = "WiFi";
static char kSettingBluetooth[] = "Bluetooth";
static char kSettingBrightness[] = "Brightness";
static char kSettingTmcDriver[] = "TMC Driver";
static char kSettingMicrosteps[] = "Microsteps";
static char kSettingSpreadCycle[] = "SpreadCycle";

static constexpr const char* kCurrentPositionNamespace = "CurrentPos";
static constexpr const char* kCurrentPositionKey = "steps";
static constexpr uint32_t kCurrentPositionSaveIntervalMs = 8000;
static uint32_t gLastCurrentPositionSaveMs = 0;
static int32_t gLastSavedCurrentPositionSteps = INT32_MIN;

static long loadPresetPositionById(uint8_t id) {
  Preferences preferences;
  long steps = 0;
  if (preferences.begin("Positions", true)) {
    char key[4];
    snprintf(key, sizeof(key), "%u", id);
    steps = preferences.getLong(key, 0);
    preferences.end();
  }
  return steps;
}

static void clearPresetPositionById(uint8_t id) {
  Preferences preferences;
  if (preferences.begin("Positions", false)) {
    char key[4];
    snprintf(key, sizeof(key), "%u", id);
    preferences.remove(key);
    preferences.end();
  }
}

static void gotoPresetPositionById(uint8_t id) {
  if (!Movement::isMotorEnabled()) {
    return;
  }
  savedPresetPositionSteps = loadPresetPositionById(id);
  Movement::moveToPosition(savedPresetPositionSteps);
}

static void clearPersistentCurrentPosition() {
  Preferences preferences;
  if (preferences.begin(kCurrentPositionNamespace, false)) {
    preferences.remove(kCurrentPositionKey);
    preferences.end();
  }
  gLastSavedCurrentPositionSteps = INT32_MIN;
}

static int32_t loadPersistentCurrentPosition() {
  Preferences preferences;
  int32_t steps = 0;
  if (preferences.begin(kCurrentPositionNamespace, true)) {
    steps = preferences.getLong(kCurrentPositionKey, 0);
    preferences.end();
  }
  return steps;
}

static void savePersistentCurrentPositionIfDue() {
  if (Movement::isBusy()) {
    return;
  }

  const uint32_t nowMs = millis();
  if (nowMs - gLastCurrentPositionSaveMs < kCurrentPositionSaveIntervalMs) {
    return;
  }

  const int32_t currentSteps = Movement::getCurrentPositionSteps();
  if (currentSteps == gLastSavedCurrentPositionSteps) {
    gLastCurrentPositionSaveMs = nowMs;
    return;
  }

  Preferences preferences;
  if (preferences.begin(kCurrentPositionNamespace, false)) {
    preferences.putLong(kCurrentPositionKey, currentSteps);
    preferences.end();
    gLastSavedCurrentPositionSteps = currentSteps;
  }
  gLastCurrentPositionSaveMs = nowMs;
}

#define DECLARE_FILTER_CALLBACKS(N)         \
  static void gotoFilter##N() {             \
    gotoPresetPositionById(N);              \
  }                                         \
  static void clearFilter##N() {            \
    clearPresetPositionById(N);             \
  }

DECLARE_FILTER_CALLBACKS(1)
DECLARE_FILTER_CALLBACKS(2)
DECLARE_FILTER_CALLBACKS(3)
DECLARE_FILTER_CALLBACKS(4)
DECLARE_FILTER_CALLBACKS(5)
DECLARE_FILTER_CALLBACKS(6)
DECLARE_FILTER_CALLBACKS(7)
DECLARE_FILTER_CALLBACKS(8)
DECLARE_FILTER_CALLBACKS(9)
DECLARE_FILTER_CALLBACKS(10)
DECLARE_FILTER_CALLBACKS(11)
DECLARE_FILTER_CALLBACKS(12)

#undef DECLARE_FILTER_CALLBACKS

/*
 * Main-menu callback that toggles motor outputs on/off.
 */
static void toggleMotorEnabledAction() {
  if (Movement::isBusy()) {
    Movement::abortHoming();
  }
  Movement::toggleMotorEnabled();
}

/*
 * Main-menu callback to trigger endstop-based homing from the Home option.
 */
static void startHomingAction() {
  clearPersistentCurrentPosition();
  screenManager.pushScreen(&idleScreen);
  delay(100); // Brief delay to allow idle screen to render before starting homing
  Movement::startHoming();
}

/*
 * Builds and configures the complete OpenMenuOS UI tree,
 * then applies visual/input preferences and starts the menu system.
 */
static void initMenu() {
  // Configure settings
  settingsScreen.addBooleanSetting(kSettingWifi, true);
  settingsScreen.addBooleanSetting(kSettingBluetooth, true);
  settingsScreen.addRangeSetting(kSettingBrightness, 0, 100, DEFAULT_BRIGHTNESS, kPercentUnit);
  settingsScreen.addSubscreenSetting(kSettingTmcDriver, &TMCSettings);

  // Configure TMC settings menu
  TMCSettings.addRangeSetting(kSettingMicrosteps, 1, 255, TMC_MICROSTEPS, kMicrostepUnit);
  TMCSettings.addBooleanSetting(kSettingSpreadCycle, TMC_SPREAD_CYCLE);

  // Configure presets menu
  PresetsMenu.addItem(kFilterTitle1, &FilterMenu1);
  PresetsMenu.addItem(kFilterTitle2, &FilterMenu2);
  PresetsMenu.addItem(kFilterTitle3, &FilterMenu3);
  PresetsMenu.addItem(kFilterTitle4, &FilterMenu4);
  PresetsMenu.addItem(kFilterTitle5, &FilterMenu5);
  PresetsMenu.addItem(kFilterTitle6, &FilterMenu6);
  PresetsMenu.addItem(kFilterTitle7, &FilterMenu7);
  PresetsMenu.addItem(kFilterTitle8, &FilterMenu8);
  PresetsMenu.addItem(kFilterTitle9, &FilterMenu9);
  PresetsMenu.addItem(kFilterTitle10, &FilterMenu10);
  PresetsMenu.addItem(kFilterTitle11, &FilterMenu11);
  PresetsMenu.addItem(kFilterTitle12, &FilterMenu12);
  PresetsMenu.addItem("Test Positions", &TestPOS);
    // Configure test positions page with example preset position entries
  TestPOS.addItem("0mm", nullptr, []() {
    Movement::moveToPositionMm(0);
  });
  TestPOS.addItem("5mm", nullptr, []() {
    Movement::moveToPositionMm(5);
  });
  TestPOS.addItem("10mm", nullptr, []() {
    Movement::moveToPositionMm(10);
  });
  TestPOS.addItem("20mm", nullptr, []() {
    Movement::moveToPositionMm(20);
  });
  TestPOS.addItem("50mm", nullptr, []() {
    Movement::moveToPositionMm(50);
  });
  TestPOS.addItem("55mm", nullptr, []() {
    Movement::moveToPositionMm(55);
  });

  // Configure per-filter preset pages (Goto/Edit/Clear).
  FilterMenu1.addItem(kFilterGoto, nullptr, gotoFilter1);
  FilterMenu1.addItem(kFilterEdit, &PresetEdit1);
  FilterMenu1.addItem(kFilterClear, nullptr, clearFilter1);

  FilterMenu2.addItem(kFilterGoto, nullptr, gotoFilter2);
  FilterMenu2.addItem(kFilterEdit, &PresetEdit2);
  FilterMenu2.addItem(kFilterClear, nullptr, clearFilter2);

  FilterMenu3.addItem(kFilterGoto, nullptr, gotoFilter3);
  FilterMenu3.addItem(kFilterEdit, &PresetEdit3);
  FilterMenu3.addItem(kFilterClear, nullptr, clearFilter3);

  FilterMenu4.addItem(kFilterGoto, nullptr, gotoFilter4);
  FilterMenu4.addItem(kFilterEdit, &PresetEdit4);
  FilterMenu4.addItem(kFilterClear, nullptr, clearFilter4);

  FilterMenu5.addItem(kFilterGoto, nullptr, gotoFilter5);
  FilterMenu5.addItem(kFilterEdit, &PresetEdit5);
  FilterMenu5.addItem(kFilterClear, nullptr, clearFilter5);

  FilterMenu6.addItem(kFilterGoto, nullptr, gotoFilter6);
  FilterMenu6.addItem(kFilterEdit, &PresetEdit6);
  FilterMenu6.addItem(kFilterClear, nullptr, clearFilter6);

  FilterMenu7.addItem(kFilterGoto, nullptr, gotoFilter7);
  FilterMenu7.addItem(kFilterEdit, &PresetEdit7);
  FilterMenu7.addItem(kFilterClear, nullptr, clearFilter7);

  FilterMenu8.addItem(kFilterGoto, nullptr, gotoFilter8);
  FilterMenu8.addItem(kFilterEdit, &PresetEdit8);
  FilterMenu8.addItem(kFilterClear, nullptr, clearFilter8);

  FilterMenu9.addItem(kFilterGoto, nullptr, gotoFilter9);
  FilterMenu9.addItem(kFilterEdit, &PresetEdit9);
  FilterMenu9.addItem(kFilterClear, nullptr, clearFilter9);

  FilterMenu10.addItem(kFilterGoto, nullptr, gotoFilter10);
  FilterMenu10.addItem(kFilterEdit, &PresetEdit10);
  FilterMenu10.addItem(kFilterClear, nullptr, clearFilter10);

  FilterMenu11.addItem(kFilterGoto, nullptr, gotoFilter11);
  FilterMenu11.addItem(kFilterEdit, &PresetEdit11);
  FilterMenu11.addItem(kFilterClear, nullptr, clearFilter11);

  FilterMenu12.addItem(kFilterGoto, nullptr, gotoFilter12);
  FilterMenu12.addItem(kFilterEdit, &PresetEdit12);
  FilterMenu12.addItem(kFilterClear, nullptr, clearFilter12);
  
  // Configure main menu
  mainMenu.addItem(kMainMenuPresets, &PresetsMenu);
  mainMenu.addItem(kMainMenuHome, nullptr, startHomingAction);
  mainMenu.addItem(kMainMenuSettings, &settingsScreen);
  mainMenu.addItem(kMainMenuToggleMotor, nullptr, toggleMotorEnabledAction);
  
  // Apply red-dominant configurable theme from config.h
  menu.setMenuStyle(1);
  menu.setScrollbar(true);
  menu.setScrollbarColor(MENU_COLOR_SCROLLBAR);
  menu.setScrollbarStyle(1);
  menu.setSelectionBorderColor(MENU_COLOR_SELECTION_BORDER);
  menu.setSelectionFillColor(MENU_COLOR_SELECTION_FILL);
  Screen::config.selectedItemColor = MENU_COLOR_TEXT;
  menu.setDisplayRotation(1);
  menu.setButtonsMode(kButtonsModeLow);

  
  menu.begin(&idleScreen);
}

/*
 * One-time startup sequence for serial logging, UI initialization,
 * and TMC driver bring-up.
 */
void setup() {
  Serial.begin(115200);
  delay(100);

  initMenu();

  Movement::initializeDriver();
  Movement::setCurrentPositionSteps(loadPersistentCurrentPosition());

  // Core 0 handles motor/homing/endstop tasks. loop() remains on Core 1 for UI/menu.
  BaseType_t taskCreated = xTaskCreatePinnedToCore(
      motorTask,
      "motorTask",
      4096,
      nullptr,
      2,
      &motorTaskHandle,
      0);
  if (taskCreated != pdPASS) {
    Serial.println("Failed to start motor task on Core 0");
  }
}

/*
 * Main runtime loop: services menu input/rendering and applies
 * current brightness setting to the display backlight PWM pin.
 */
void loop() {
  menu.loop();
  savePersistentCurrentPositionIfDue();
  analogWrite(PIN_LCD_BL, settingsScreen.getSettingValue("Brightness") * 255 / 100);
}
