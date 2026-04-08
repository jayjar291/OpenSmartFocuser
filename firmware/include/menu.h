#pragma once

#include <Arduino.h>
#include <OpenMenuOS.h>
#include "custom_screens.h"

extern OpenMenuOS menu;
extern MenuScreen mainMenu;
extern CustomScreens::IdleScreen idleScreen;
extern SettingsScreen settingsScreen;
extern long savedPresetPositionSteps;

long loadPresetPositionById(uint8_t id);
void gotoPresetPositionById(uint8_t id);
void initMenu();
uint8_t getFocusSpeedSetting();
uint8_t getBrightnessSetting();