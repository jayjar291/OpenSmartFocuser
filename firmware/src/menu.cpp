#include "menu.h"

#include <Preferences.h>
#include "config.h"
#include "menu_strings.h"
#include "movement.h"

OpenMenuOS menu(PIN_BUTTON_UP, PIN_BUTTON_DOWN, PIN_BUTTON_SELECT);

MenuScreen mainMenu(MenuStrings::kTitleMainMenu);
CustomScreens::IdleScreen idleScreen(MenuStrings::kTitleIdle);
MenuScreen PresetsMenu(MenuStrings::kTitlePresets);
MenuScreen TestPOS(MenuStrings::kTestPositions);
MenuScreen FilterMenu1(MenuStrings::kFilterTitle1);
MenuScreen FilterMenu2(MenuStrings::kFilterTitle2);
MenuScreen FilterMenu3(MenuStrings::kFilterTitle3);
MenuScreen FilterMenu4(MenuStrings::kFilterTitle4);
MenuScreen FilterMenu5(MenuStrings::kFilterTitle5);
MenuScreen FilterMenu6(MenuStrings::kFilterTitle6);
MenuScreen FilterMenu7(MenuStrings::kFilterTitle7);
MenuScreen FilterMenu8(MenuStrings::kFilterTitle8);
MenuScreen FilterMenu9(MenuStrings::kFilterTitle9);
MenuScreen FilterMenu10(MenuStrings::kFilterTitle10);
MenuScreen FilterMenu11(MenuStrings::kFilterTitle11);
MenuScreen FilterMenu12(MenuStrings::kFilterTitle12);

CustomScreens::PresetScreen PresetEdit1(MenuStrings::kFilterTitle1, 1);
CustomScreens::PresetScreen PresetEdit2(MenuStrings::kFilterTitle2, 2);
CustomScreens::PresetScreen PresetEdit3(MenuStrings::kFilterTitle3, 3);
CustomScreens::PresetScreen PresetEdit4(MenuStrings::kFilterTitle4, 4);
CustomScreens::PresetScreen PresetEdit5(MenuStrings::kFilterTitle5, 5);
CustomScreens::PresetScreen PresetEdit6(MenuStrings::kFilterTitle6, 6);
CustomScreens::PresetScreen PresetEdit7(MenuStrings::kFilterTitle7, 7);
CustomScreens::PresetScreen PresetEdit8(MenuStrings::kFilterTitle8, 8);
CustomScreens::PresetScreen PresetEdit9(MenuStrings::kFilterTitle9, 9);
CustomScreens::PresetScreen PresetEdit10(MenuStrings::kFilterTitle10, 10);
CustomScreens::PresetScreen PresetEdit11(MenuStrings::kFilterTitle11, 11);
CustomScreens::PresetScreen PresetEdit12(MenuStrings::kFilterTitle12, 12);

SettingsScreen TMCSettings(MenuStrings::kTitleTmcSettings);
SettingsScreen settingsScreen(MenuStrings::kTitleSettings);

long savedPresetPositionSteps = 0;

namespace {

void clearPresetPositionById(uint8_t id) {
  Preferences preferences;
  if (preferences.begin("Positions", false)) {
    char key[4];
    snprintf(key, sizeof(key), "%u", id);
    preferences.remove(key);
    preferences.end();
  }
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

void toggleMotorEnabledAction() {
  if (Movement::isBusy()) {
    Movement::abortHoming();
  }
  Movement::toggleMotorEnabled();
}

void startHomingAction() {
    screenManager.pushScreen(&idleScreen);
    delay(100); // Brief delay to allow idle screen to render before starting homing
    Movement::startHoming();
}

} // namespace

long loadPresetPositionById(uint8_t id) {
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

void gotoPresetPositionById(uint8_t id) {
  if (!Movement::isMotorEnabled()) {
    Movement::toggleMotorEnabled();
  }
  savedPresetPositionSteps = loadPresetPositionById(id);
  Movement::moveToPosition(savedPresetPositionSteps);
}

void initMenu() {

  settingsScreen.addOptionSetting(MenuStrings::kSettingFocusSpeed, MenuStrings::speeds, 5, 1);
  settingsScreen.addBooleanSetting(MenuStrings::kSettingWifi, true);
  settingsScreen.addBooleanSetting(MenuStrings::kSettingBluetooth, true);
  settingsScreen.addRangeSetting(MenuStrings::kSettingBrightness, 0, 100, DEFAULT_BRIGHTNESS, MenuStrings::kPercentUnit);
  settingsScreen.addSubscreenSetting(MenuStrings::kSettingTmcDriver, &TMCSettings);

  TMCSettings.addRangeSetting(MenuStrings::kSettingMicrosteps, 1, 255, TMC_MICROSTEPS, MenuStrings::kMicrostepUnit);
  TMCSettings.addBooleanSetting(MenuStrings::kSettingSpreadCycle, TMC_SPREAD_CYCLE);

  PresetsMenu.addItem(MenuStrings::kFilterTitle1, &FilterMenu1);
  PresetsMenu.addItem(MenuStrings::kFilterTitle2, &FilterMenu2);
  PresetsMenu.addItem(MenuStrings::kFilterTitle3, &FilterMenu3);
  PresetsMenu.addItem(MenuStrings::kFilterTitle4, &FilterMenu4);
  PresetsMenu.addItem(MenuStrings::kFilterTitle5, &FilterMenu5);
  PresetsMenu.addItem(MenuStrings::kFilterTitle6, &FilterMenu6);
  PresetsMenu.addItem(MenuStrings::kFilterTitle7, &FilterMenu7);
  PresetsMenu.addItem(MenuStrings::kFilterTitle8, &FilterMenu8);
  PresetsMenu.addItem(MenuStrings::kFilterTitle9, &FilterMenu9);
  PresetsMenu.addItem(MenuStrings::kFilterTitle10, &FilterMenu10);
  PresetsMenu.addItem(MenuStrings::kFilterTitle11, &FilterMenu11);
  PresetsMenu.addItem(MenuStrings::kFilterTitle12, &FilterMenu12);
  PresetsMenu.addItem(MenuStrings::kTestPositions, &TestPOS);

  TestPOS.addItem(MenuStrings::kPos0mm, nullptr, []() { Movement::moveToPositionMm(0); });
  TestPOS.addItem(MenuStrings::kPos5mm, nullptr, []() { Movement::moveToPositionMm(5); });
  TestPOS.addItem(MenuStrings::kPos10mm, nullptr, []() { Movement::moveToPositionMm(10); });
  TestPOS.addItem(MenuStrings::kPos20mm, nullptr, []() { Movement::moveToPositionMm(20); });
  TestPOS.addItem(MenuStrings::kPos50mm, nullptr, []() { Movement::moveToPositionMm(50); });
  TestPOS.addItem(MenuStrings::kPos55mm, nullptr, []() { Movement::moveToPositionMm(55); });

  FilterMenu1.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter1);
  FilterMenu1.addItem(MenuStrings::kFilterEdit, &PresetEdit1);
  FilterMenu1.addItem(MenuStrings::kFilterClear, nullptr, clearFilter1);

  FilterMenu2.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter2);
  FilterMenu2.addItem(MenuStrings::kFilterEdit, &PresetEdit2);
  FilterMenu2.addItem(MenuStrings::kFilterClear, nullptr, clearFilter2);

  FilterMenu3.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter3);
  FilterMenu3.addItem(MenuStrings::kFilterEdit, &PresetEdit3);
  FilterMenu3.addItem(MenuStrings::kFilterClear, nullptr, clearFilter3);

  FilterMenu4.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter4);
  FilterMenu4.addItem(MenuStrings::kFilterEdit, &PresetEdit4);
  FilterMenu4.addItem(MenuStrings::kFilterClear, nullptr, clearFilter4);

  FilterMenu5.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter5);
  FilterMenu5.addItem(MenuStrings::kFilterEdit, &PresetEdit5);
  FilterMenu5.addItem(MenuStrings::kFilterClear, nullptr, clearFilter5);

  FilterMenu6.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter6);
  FilterMenu6.addItem(MenuStrings::kFilterEdit, &PresetEdit6);
  FilterMenu6.addItem(MenuStrings::kFilterClear, nullptr, clearFilter6);

  FilterMenu7.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter7);
  FilterMenu7.addItem(MenuStrings::kFilterEdit, &PresetEdit7);
  FilterMenu7.addItem(MenuStrings::kFilterClear, nullptr, clearFilter7);

  FilterMenu8.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter8);
  FilterMenu8.addItem(MenuStrings::kFilterEdit, &PresetEdit8);
  FilterMenu8.addItem(MenuStrings::kFilterClear, nullptr, clearFilter8);

  FilterMenu9.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter9);
  FilterMenu9.addItem(MenuStrings::kFilterEdit, &PresetEdit9);
  FilterMenu9.addItem(MenuStrings::kFilterClear, nullptr, clearFilter9);

  FilterMenu10.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter10);
  FilterMenu10.addItem(MenuStrings::kFilterEdit, &PresetEdit10);
  FilterMenu10.addItem(MenuStrings::kFilterClear, nullptr, clearFilter10);

  FilterMenu11.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter11);
  FilterMenu11.addItem(MenuStrings::kFilterEdit, &PresetEdit11);
  FilterMenu11.addItem(MenuStrings::kFilterClear, nullptr, clearFilter11);

  FilterMenu12.addItem(MenuStrings::kFilterGoto, nullptr, gotoFilter12);
  FilterMenu12.addItem(MenuStrings::kFilterEdit, &PresetEdit12);
  FilterMenu12.addItem(MenuStrings::kFilterClear, nullptr, clearFilter12);

  mainMenu.addItem(MenuStrings::kMainMenuPresets, &PresetsMenu);
  mainMenu.addItem(MenuStrings::kMainMenuHome, nullptr, startHomingAction);
  mainMenu.addItem(MenuStrings::kMainMenuSettings, &settingsScreen);
  mainMenu.addItem(MenuStrings::kMainMenuToggleMotor, nullptr, toggleMotorEnabledAction);

  menu.setMenuStyle(1);
  menu.setScrollbar(true);
  menu.setScrollbarColor(MENU_COLOR_SCROLLBAR);
  menu.setScrollbarStyle(1);
  menu.setSelectionBorderColor(MENU_COLOR_SELECTION_BORDER);
  menu.setSelectionFillColor(MENU_COLOR_SELECTION_FILL);
  Screen::config.selectedItemColor = MENU_COLOR_TEXT;
  menu.setDisplayRotation(1);
  menu.setButtonsMode(MenuStrings::kButtonsModeLow);

  menu.begin(&idleScreen);
}

uint8_t getFocusSpeedSetting() {
  return settingsScreen.getSettingValue(MenuStrings::kSettingFocusSpeed);
}

uint8_t getBrightnessSetting() {
  return settingsScreen.getSettingValue(MenuStrings::kSettingBrightness);
}