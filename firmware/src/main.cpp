#include <Arduino.h>
#include <AccelStepper.h>
#include <TMCStepper.h>
#include <OpenMenuOS.h>
#include "config.h"
#include "custom_screens.h"

/*
 * Hardware interface constants for TMC2209 UART control.
 */
static constexpr float TMC_R_SENSE = 0.11f;
static constexpr uint8_t TMC_STEPPER_UART_INDEX = 1;

/*
 * Global hardware driver instances used during initialization and runtime.
 */
HardwareSerial tmcSerial(TMC_STEPPER_UART_INDEX);
AccelStepper focuserStepper(AccelStepper::DRIVER, PIN_TMC_STEP, PIN_TMC_DIR);
TMC2209Stepper focuserDriver(&tmcSerial, TMC_R_SENSE, TMC_DRIVER_ADDRESS);
OpenMenuOS menu(PIN_BUTTON_UP, PIN_BUTTON_DOWN, PIN_BUTTON_SELECT);

/*
 * Global motor enable state controlled from the main menu.
 */
bool motorEnabled = false;
bool homingInProgress = false;
long savedPresetPositionSteps = 0;

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
  homingInProgress = false;
  if (motorEnabled) {
    focuserStepper.enableOutputs();
  } else {
    focuserStepper.setSpeed(0.0f);
    focuserStepper.disableOutputs();
  }
}

/*
 * Main-menu callback to trigger endstop-based homing from the Home option.
 */
static void startHomingAction() {
  if (!motorEnabled) {
    return;
  }

  homingInProgress = true;
  screenManager.pushScreen(&idleScreen);
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
    homingInProgress = false;
    focuserStepper.setSpeed(0.0f);
    return;
  }

  if (isEndstopTriggered()) {
    focuserStepper.setSpeed(0.0f);
    focuserStepper.setCurrentPosition(0);
    homingInProgress = false;
    return;
  }

  focuserStepper.enableOutputs();
  focuserStepper.setSpeed(-IDLE_JOG_SPEED_STEPS_PER_SEC);
  focuserStepper.runSpeed();
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
    focuserStepper.enableOutputs();
    focuserStepper.moveTo(savedPresetPositionSteps);
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
  focuserDriver.microsteps(TMC_MICROSTEPS);
  focuserDriver.pwm_autoscale(true);

  focuserStepper.setMaxSpeed(TMC_MAX_SPEED);
  focuserStepper.setAcceleration(TMC_MAX_ACCELERATION);
  focuserStepper.setEnablePin(PIN_TMC_ENABLE);
  focuserStepper.setPinsInverted(false, false, true);
  motorEnabled = false;
  homingInProgress = false;
  focuserStepper.disableOutputs();
}

/*
 * One-time startup sequence for serial logging, UI initialization,
 * and TMC driver bring-up.
 */
void setup() {
  Serial.begin(115200);
  delay(100);

  initMenu();

  initDriver();

  Serial.println("OpenSmartFocuser hardware init complete");
}

/*
 * Main runtime loop: services menu input/rendering and applies
 * current brightness setting to the display backlight PWM pin.
 */
void loop() {
  menu.loop();
  updateHoming();
  analogWrite(PIN_LCD_BL, settingsScreen.getSettingValue("Brightness") * 255 / 100);
}
