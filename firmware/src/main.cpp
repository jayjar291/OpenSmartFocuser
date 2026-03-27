#include <Arduino.h>
#include <FastAccelStepper.h>
#include <TMCStepper.h>
#include <OpenMenuOS.h>
#include "config.h"
#include "custom_screens.h"

/*
 * Hardware interface constants for TMC2209 UART control.
 */
static constexpr float TMC_R_SENSE = 0.11f;
static constexpr uint8_t TMC_STEPPER_UART_INDEX = 1;
static constexpr uint16_t HOMING_MICROSTEPS = 1;

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
bool homingInProgress = false;
long savedPresetPositionSteps = 0;

static void applyNormalMicrosteps() {
  focuserDriver.microsteps(TMC_MICROSTEPS);
}

static void applyHomingMicrosteps() {
  focuserDriver.microsteps(HOMING_MICROSTEPS);
}

static void stopHoming() {
  focuserStepper->forceStop();
  focuserStepper->setSpeedInHz(TMC_MAX_SPEED);
  applyNormalMicrosteps();
  homingInProgress = false;
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
  motorEnabled = !motorEnabled;
  if (homingInProgress) {
    stopHoming();
  }
  if (motorEnabled) {
    focuserStepper->enableOutputs();
  } else {
    focuserStepper->stopMove();
    focuserStepper->disableOutputs();
  }
}

/*
 * Main-menu callback to trigger endstop-based homing from the Home option.
 */
static void startHomingAction() {
  screenManager.pushScreen(&idleScreen);
  if (!motorEnabled) {
    motorEnabled = true;
    focuserStepper->enableOutputs();
  }

  focuserStepper->stopMove();
  applyHomingMicrosteps();
  focuserStepper->setSpeedInHz(HOMING_SPEED_STEPS_PER_SEC);
  focuserStepper->runBackward();
  homingInProgress = true;
}

/*
 * Endstop condition for homing sequence (active-low input).
 */
static bool isEndstopTriggered() {
  return digitalRead(PIN_BUTTON_ENDSTOP) == LOW;
}

/*
 * Runs the homing state machine independently from any custom screen.
 */
static void updateHoming() {
  if (!homingInProgress) {
    return;
  }

  if (!motorEnabled) {
    stopHoming();
    return;
  }

  if (isEndstopTriggered()) {
    focuserStepper->forceStopAndNewPosition(0);
    stopHoming();
    return;
  }
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

  // Configure filter 1 preset page
  Filter1.addItem(kFilterGoto, nullptr, []() {
    if (!motorEnabled) {
      return;
    }
    focuserStepper->enableOutputs();
    focuserStepper->moveTo(savedPresetPositionSteps);
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
 * Initializes STEP/DIR/ENABLE pins, brings up TMC2209 over UART,
 * and applies baseline stepper-driver and motion planner settings.
 */
static void initDriver() {
  pinMode(PIN_TMC_STEP, OUTPUT);
  digitalWrite(PIN_TMC_STEP, LOW);
  pinMode(PIN_TMC_DIR, OUTPUT);
  digitalWrite(PIN_TMC_DIR, LOW);
  pinMode(PIN_TMC_ENABLE, OUTPUT);
  digitalWrite(PIN_TMC_ENABLE, HIGH);
  pinMode(PIN_BUTTON_ENDSTOP, INPUT_PULLUP);

  tmcSerial.begin(TMC_UART_BAUDRATE, SERIAL_8N1, PIN_TMC_UART_RX, PIN_TMC_UART_TX);

  focuserDriver.begin();
  focuserDriver.toff(4);
  focuserDriver.rms_current(TMC_RMS_CURRENT);
  applyNormalMicrosteps();
  focuserDriver.pwm_autoscale(true);

  stepperEngine.init();
  focuserStepper = stepperEngine.stepperConnectToPin(PIN_TMC_STEP);
  focuserStepper->setDirectionPin(PIN_TMC_DIR);
  focuserStepper->setEnablePin(PIN_TMC_ENABLE, true); // active LOW: LOW = motor enabled
  focuserStepper->setSpeedInHz(TMC_MAX_SPEED);
  focuserStepper->setAcceleration(TMC_MAX_ACCELERATION);
  motorEnabled = false;
  homingInProgress = false;
  focuserStepper->disableOutputs();
}

/*
 * One-time startup sequence for serial logging, UI initialization,
 * and TMC driver bring-up.
 */
void setup() {
  delay(100);

  initMenu();

  initDriver();
}

/*
 * Main runtime loop: services menu input/rendering and applies
 * current brightness setting to the display backlight PWM pin.
 */
void loop() {
  updateHoming();

  if (homingInProgress) {
    return;
  }

  menu.loop();
  analogWrite(PIN_LCD_BL, settingsScreen.getSettingValue("Brightness") * 255 / 100);
}
