#include <Arduino.h>
#include <FastAccelStepper.h>
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

MenuScreen mainMenu(kTitleMainMenu);
CustomScreens::IdleScreen idleScreen(kTitleIdle);
CustomScreens::PresetScreen presetScreen(kTitlePresets);
MenuScreen PresetsMenu(kTitlePresets);
MenuScreen Filter1("Filter 1");
CustomScreen Homing("Homing");
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
static char kFilter1[] = "Filter 1";
static char kSettingWifi[] = "WiFi";
static char kSettingBluetooth[] = "Bluetooth";
static char kSettingBrightness[] = "Brightness";
static char kSettingTmcDriver[] = "TMC Driver";
static char kSettingMicrosteps[] = "Microsteps";
static char kSettingSpreadCycle[] = "SpreadCycle";

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
  PresetsMenu.addItem(kFilter1, &Filter1);
  PresetsMenu.addItem(kPresetTestFocus);
  PresetsMenu.addItem("0mm", nullptr, []() {
    Movement::moveToPositionMm(0);
  });
  PresetsMenu.addItem("5mm", nullptr, []() {
    Movement::moveToPositionMm(5);
  });
  PresetsMenu.addItem("10mm", nullptr, []() {
    Movement::moveToPositionMm(10);
  });
  PresetsMenu.addItem("20mm", nullptr, []() {
    Movement::moveToPositionMm(20);
  });
  PresetsMenu.addItem("50mm", nullptr, []() {
    Movement::moveToPositionMm(50);
  });
  PresetsMenu.addItem("55mm", nullptr, []() {
    Movement::moveToPositionMm(55);
  });


  // Configure filter 1 preset page
  Filter1.addItem(kFilterGoto, nullptr, []() {
    if (!Movement::isMotorEnabled()) {
      return;
    }
    Movement::moveToPosition(savedPresetPositionSteps);
  });
  Filter1.addItem(kFilterEdit, &presetScreen);
  
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
  analogWrite(PIN_LCD_BL, settingsScreen.getSettingValue("Brightness") * 255 / 100);
}
