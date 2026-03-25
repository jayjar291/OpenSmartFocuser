#include <Arduino.h>
#include <AccelStepper.h>
#include <TMCStepper.h>
#include <OpenMenuOS.h>
#include "config.h"

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
 * Application menu hierarchy: top-level pages and settings subpages.
 */
MenuScreen mainMenu("Main Menu");
MenuScreen PresetsMenu("Presets");
SettingsScreen TMCSettings("TMC Settings");
SettingsScreen settingsScreen("Settings");

/*
 * Mutable labels for APIs that expect char* rather than string literals.
 */
static char kPresetFineFocus[] = "Fine focus point";
static char kPresetTestFocus[] = "Test focus point";
static char kPercentUnit[] = "%";
static char kMicrostepUnit[] = "uSteps";

/*
 * Builds and configures the complete OpenMenuOS UI tree,
 * then applies visual/input preferences and starts the menu system.
 */
static void initMenu() {
  // Configure settings
  settingsScreen.addBooleanSetting("WiFi", true);
  settingsScreen.addBooleanSetting("Bluetooth", true);
  settingsScreen.addRangeSetting("Brightness", 0, 100, DEFAULT_BRIGHTNESS, kPercentUnit);
  settingsScreen.addSubscreenSetting("TMC Driver", &TMCSettings);

  // Configure TMC settings menu
  TMCSettings.addRangeSetting("Microsteps", 1, 255, TMC_MICROSTEPS, kMicrostepUnit);
  TMCSettings.addBooleanSetting("SpreadCycle", TMC_SPREAD_CYCLE);


  // Configure presets menu
  PresetsMenu.addItem(kPresetFineFocus);
  PresetsMenu.addItem(kPresetTestFocus);
  
  // Configure main menu
  mainMenu.addItem("Presets", &PresetsMenu);
  mainMenu.addItem("Home");
  mainMenu.addItem("Settings", &settingsScreen);
  
  // Apply style and start
  menu.useStylePreset("default");
  menu.setScrollbar(true);
  menu.setScrollbarStyle(1);
  menu.setDisplayRotation(1);
  menu.setButtonsMode("LOW");

  
  menu.begin(&mainMenu);
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
  analogWrite(PIN_LCD_BL, settingsScreen.getSettingValue("Brightness") * 255 / 100);
}
