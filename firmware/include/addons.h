#pragma once

#include <cstdint>

namespace Addons {

enum class AddonType : uint8_t {
    None = 0,
    Shutter,
    FlatPanel,
    AutoCollimation,
    OTASensors,
};

bool isEnabled();
bool isInitialized();
bool hasAddon(AddonType type);
void initializeAddons();
void detachServos();

void SetFlatPanelBrightness(uint8_t brightness);
void SetShutterPosition(uint8_t position);

void toggleFlatPanel();
void toggleShutter();

} // namespace Addons