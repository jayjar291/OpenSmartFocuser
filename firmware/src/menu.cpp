#include "menu.h"

#include <cstring>
#include "config.h"
#include "menu_strings.h"
#include "movement.h"
#include "preset.h"
#include "debug_serial.h"

OpenMenuOS menu(PIN_BUTTON_UP, PIN_BUTTON_DOWN, PIN_BUTTON_SELECT);

MenuScreen mainMenu(MenuStrings::kTitleMainMenu);
CustomScreens::IdleScreen idleScreen(MenuStrings::kTitleIdle);
CustomScreens::PresetScreen presetScreen(MenuStrings::kPresetActions);
CustomScreens::DeviceInfoScreen deviceInfoScreen(MenuStrings::kTitleDeviceInfo);
MenuScreen PresetsMenu(MenuStrings::kTitlePresets);
MenuScreen PresetActionsMenu(MenuStrings::kPresetActions);
MenuScreen AddPresetMenu(MenuStrings::kPresetAdd);
MenuScreen TestPOS(MenuStrings::kTestPositions);

SettingsScreen TMCSettings(MenuStrings::kTitleTmcSettings);
SettingsScreen settingsScreen(MenuStrings::kTitleSettings);

long savedPresetPositionSteps = 0;

namespace {

constexpr const char* kConfiguredPresetNames[] = PRESET_NAME_OPTIONS;
constexpr size_t kConfiguredPresetNameCount = sizeof(kConfiguredPresetNames) / sizeof(kConfiguredPresetNames[0]);

static_assert(kConfiguredPresetNameCount > 0, "PRESET_NAME_OPTIONS must contain at least one preset name.");
static_assert(kConfiguredPresetNameCount <= preset::kMaxPresets,
              "PRESET_NAME_OPTIONS must not exceed preset::kMaxPresets.");

char gPresetLabelBuffer[preset::kMaxPresets][preset::kMaxNameLen + 8] = {};
char gPresetActionsTitle[preset::kMaxNameLen + 8] = "Preset";

uint8_t gPresetIdsBySlot[preset::kMaxPresets] = {};
uint8_t gNameIndexesBySlot[preset::kMaxPresets] = {};
uint8_t gSelectedPresetId = 0;

void refreshPresetsMenu();
void refreshAddPresetMenu();
void openPresetFromSlot(uint8_t slotIndex);
void addPresetFromNameSlot(uint8_t slotIndex);

#define DECLARE_PRESET_SLOT_CALLBACKS(N)                       \
  static void openPresetSlot##N() { openPresetFromSlot(N - 1); } \
  static void addPresetNameSlot##N() { addPresetFromNameSlot(N - 1); }

DECLARE_PRESET_SLOT_CALLBACKS(1)
DECLARE_PRESET_SLOT_CALLBACKS(2)
DECLARE_PRESET_SLOT_CALLBACKS(3)
DECLARE_PRESET_SLOT_CALLBACKS(4)
DECLARE_PRESET_SLOT_CALLBACKS(5)
DECLARE_PRESET_SLOT_CALLBACKS(6)
DECLARE_PRESET_SLOT_CALLBACKS(7)
DECLARE_PRESET_SLOT_CALLBACKS(8)
DECLARE_PRESET_SLOT_CALLBACKS(9)
DECLARE_PRESET_SLOT_CALLBACKS(10)
DECLARE_PRESET_SLOT_CALLBACKS(11)
DECLARE_PRESET_SLOT_CALLBACKS(12)

#undef DECLARE_PRESET_SLOT_CALLBACKS

constexpr ActionCallback kOpenPresetSlotCallbacks[preset::kMaxPresets] = {
    openPresetSlot1, openPresetSlot2, openPresetSlot3, openPresetSlot4,
    openPresetSlot5, openPresetSlot6, openPresetSlot7, openPresetSlot8,
    openPresetSlot9, openPresetSlot10, openPresetSlot11, openPresetSlot12};

constexpr ActionCallback kAddPresetNameSlotCallbacks[preset::kMaxPresets] = {
    addPresetNameSlot1, addPresetNameSlot2, addPresetNameSlot3, addPresetNameSlot4,
    addPresetNameSlot5, addPresetNameSlot6, addPresetNameSlot7, addPresetNameSlot8,
    addPresetNameSlot9, addPresetNameSlot10, addPresetNameSlot11, addPresetNameSlot12};

bool isConfiguredNameInUse(const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }

  preset::Preset current{};
  for (uint8_t i = 0; i < preset::capacity(); ++i) {
    if (!preset::getByIndex(i, current) || !current.used) {
      continue;
    }
    if (strcmp(current.name, name) == 0) {
      return true;
    }
  }
  return false;
}

void gotoSelectedPresetAction() {
  if (gSelectedPresetId == 0) {
    return;
  }
  (void)preset::gotoById(gSelectedPresetId);
}

void editSelectedPresetAction() {
  if (gSelectedPresetId == 0) {
    return;
  }

  preset::Preset current{};
  if (!preset::getById(gSelectedPresetId, current)) {
    return;
  }

  presetScreen.configureForEdit(current.id, current.name);
  screenManager.pushScreen(&presetScreen);
}

void removeSelectedPresetAction() {
  if (gSelectedPresetId == 0) {
    return;
  }

  if (preset::remove(gSelectedPresetId)) {
    gSelectedPresetId = 0;
    refreshPresetsMenu();
    if (screenManager.canGoBack()) {
      screenManager.popScreen();
    }
  }
}

void refreshPresetActionsMenu() {
  PresetActionsMenu.items.clear();
  PresetActionsMenu.currentItemIndex = 0;

  preset::Preset current{};
  if (!preset::getById(gSelectedPresetId, current)) {
    PresetActionsMenu.title = MenuStrings::kPresetActions;
    PresetActionsMenu.addItem(MenuStrings::kPresetEmpty);
    return;
  }

  snprintf(gPresetActionsTitle, sizeof(gPresetActionsTitle), "%s", current.name);
  PresetActionsMenu.title = gPresetActionsTitle;
  PresetActionsMenu.addItem(MenuStrings::kFilterGoto, nullptr, gotoSelectedPresetAction);
  PresetActionsMenu.addItem(MenuStrings::kFilterEdit, nullptr, editSelectedPresetAction);
  PresetActionsMenu.addItem(MenuStrings::kFilterClear, nullptr, removeSelectedPresetAction);
}

void openPresetFromSlot(uint8_t slotIndex) {
  if (slotIndex >= preset::kMaxPresets) {
    return;
  }

  const uint8_t presetId = gPresetIdsBySlot[slotIndex];
  if (presetId == 0) {
    return;
  }

  gSelectedPresetId = presetId;
  refreshPresetActionsMenu();
  screenManager.pushScreen(&PresetActionsMenu);
}

void addPresetFromNameSlot(uint8_t slotIndex) {
  if (slotIndex >= preset::kMaxPresets) {
    return;
  }

  const uint8_t nameIndex = gNameIndexesBySlot[slotIndex];
  if (nameIndex >= kConfiguredPresetNameCount) {
    return;
  }

  const char* name = kConfiguredPresetNames[nameIndex];
  presetScreen.configureForAdd(name);
  screenManager.pushScreen(&presetScreen);
}

void refreshAddPresetMenu() {
  AddPresetMenu.items.clear();
  AddPresetMenu.currentItemIndex = 0;
  memset(gNameIndexesBySlot, 0, sizeof(gNameIndexesBySlot));

  uint8_t slotCount = 0;
  for (uint8_t i = 0; i < kConfiguredPresetNameCount; ++i) {
    if (isConfiguredNameInUse(kConfiguredPresetNames[i])) {
      continue;
    }
    if (slotCount >= preset::kMaxPresets) {
      break;
    }

    gNameIndexesBySlot[slotCount] = i;
    AddPresetMenu.addItem(kConfiguredPresetNames[i], nullptr, kAddPresetNameSlotCallbacks[slotCount]);
    ++slotCount;
  }

  if (slotCount == 0) {
    AddPresetMenu.addItem(MenuStrings::kPresetNoNames);
  }
}

void openAddPresetMenu() {
  refreshAddPresetMenu();
  screenManager.pushScreen(&AddPresetMenu);
}

void refreshPresetsMenu() {
  PresetsMenu.items.clear();
  PresetsMenu.currentItemIndex = 0;
  memset(gPresetIdsBySlot, 0, sizeof(gPresetIdsBySlot));

  PresetsMenu.addItem(MenuStrings::kPresetAdd, nullptr, openAddPresetMenu);

  uint8_t slotCount = 0;
  preset::Preset current{};
  for (uint8_t i = 0; i < preset::capacity(); ++i) {
    if (!preset::getByIndex(i, current) || !current.used) {
      continue;
    }
    if (slotCount >= preset::kMaxPresets) {
      break;
    }

    gPresetIdsBySlot[slotCount] = current.id;
    snprintf(gPresetLabelBuffer[slotCount], sizeof(gPresetLabelBuffer[slotCount]), "%u: %s", current.id, current.name);
    PresetsMenu.addItem(gPresetLabelBuffer[slotCount], nullptr, kOpenPresetSlotCallbacks[slotCount]);
    ++slotCount;
  }

  if (slotCount == 0) {
    PresetsMenu.addItem(MenuStrings::kPresetEmpty);
  }

  PresetsMenu.addItem(MenuStrings::kTestPositions, &TestPOS);
}

void openPresetsMenuAction() {
  refreshPresetsMenu();
  screenManager.pushScreen(&PresetsMenu);
}

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

void notifyPresetMenuDataChanged() {
  refreshPresetsMenu();
  refreshAddPresetMenu();
}

long loadPresetPositionById(uint8_t id) {
  preset::Preset current{};
  if (!preset::getById(id, current)) {
    return 0;
  }
  return current.steps;
}

void gotoPresetPositionById(uint8_t id) {
  if (!Movement::isMotorEnabled()) {
    Movement::toggleMotorEnabled();
  }
  savedPresetPositionSteps = loadPresetPositionById(id);
  (void)preset::gotoById(id);
}

void initMenu() {
  DebugSerial::printFramed("initMenu: settings");

  settingsScreen.addOptionSetting(MenuStrings::kSettingFocusSpeed, MenuStrings::speeds, 5, 1);
  settingsScreen.addBooleanSetting(MenuStrings::kSettingWifi, true);
  settingsScreen.addBooleanSetting(MenuStrings::kSettingBluetooth, true);
  settingsScreen.addRangeSetting(MenuStrings::kSettingBrightness, 0, 100, DEFAULT_BRIGHTNESS, MenuStrings::kPercentUnit);
  settingsScreen.addSubscreenSetting(MenuStrings::kSettingTmcDriver, &TMCSettings);

  DebugSerial::printFramed("initMenu: tmc settings");
  TMCSettings.addRangeSetting(MenuStrings::kSettingMicrosteps, 1, 255, TMC_MICROSTEPS, MenuStrings::kMicrostepUnit);
  TMCSettings.addBooleanSetting(MenuStrings::kSettingSpreadCycle, TMC_SPREAD_CYCLE);

  DebugSerial::printFramed("initMenu: test menu");
  TestPOS.addItem(MenuStrings::kPos0mm, nullptr, []() { Movement::moveToPositionMm(0); });
  TestPOS.addItem(MenuStrings::kPos5mm, nullptr, []() { Movement::moveToPositionMm(5); });
  TestPOS.addItem(MenuStrings::kPos10mm, nullptr, []() { Movement::moveToPositionMm(10); });
  TestPOS.addItem(MenuStrings::kPos20mm, nullptr, []() { Movement::moveToPositionMm(20); });
  TestPOS.addItem(MenuStrings::kPos50mm, nullptr, []() { Movement::moveToPositionMm(50); });
  TestPOS.addItem(MenuStrings::kPos55mm, nullptr, []() { Movement::moveToPositionMm(55); });

  DebugSerial::printFramed("initMenu: presets");
  refreshPresetsMenu();

  DebugSerial::printFramed("initMenu: main menu items");
  mainMenu.addItem(MenuStrings::kMainMenuPresets, nullptr, openPresetsMenuAction);
  mainMenu.addItem(MenuStrings::kMainMenuInfo, &deviceInfoScreen);
  mainMenu.addItem(MenuStrings::kMainMenuHome, nullptr, startHomingAction);
  mainMenu.addItem(MenuStrings::kMainMenuSettings, &settingsScreen);
  mainMenu.addItem(MenuStrings::kMainMenuToggleMotor, nullptr, toggleMotorEnabledAction);

  DebugSerial::printFramed("initMenu: style");
  menu.setMenuStyle(1);
  menu.setScrollbar(true);
  menu.setScrollbarColor(MENU_COLOR_SCROLLBAR);
  menu.setScrollbarStyle(1);
  menu.setSelectionBorderColor(MENU_COLOR_SELECTION_BORDER);
  menu.setSelectionFillColor(MENU_COLOR_SELECTION_FILL);
  Screen::config.selectedItemColor = MENU_COLOR_TEXT;
  menu.setDisplayRotation(1);
  menu.setButtonsMode(MenuStrings::kButtonsModeLow);

  DebugSerial::printFramed("initMenu: tft alloc mode");
#if TFT_DISABLE_PSRAM_ALLOCATIONS
  tft.setAttribute(PSRAM_ENABLE, 0);
  canvas.setAttribute(PSRAM_ENABLE, 0);
#endif

  DebugSerial::printFramed("initMenu: begin");
  menu.begin(&idleScreen);
  DebugSerial::printFramed("initMenu: done");
}

uint8_t getFocusSpeedSetting() {
  return settingsScreen.getSettingValue(MenuStrings::kSettingFocusSpeed);
}

uint8_t getBrightnessSetting() {
  return settingsScreen.getSettingValue(MenuStrings::kSettingBrightness);
}