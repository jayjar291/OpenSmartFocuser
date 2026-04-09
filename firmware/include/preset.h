#pragma once
#include <Arduino.h>
#include <Preferences.h>

namespace preset {

static constexpr uint8_t kMaxPresets = 12;
static constexpr size_t kMaxNameLen = 16;

struct Preset {
  bool used;
  uint8_t id;
  int32_t steps;
  char name[kMaxNameLen];
};

bool begin();
uint8_t capacity();
bool getByIndex(uint8_t index, Preset& outPreset);
bool getById(uint8_t id, Preset& outPreset);

bool add(const char* name, int32_t steps, uint8_t& outId);
bool set(uint8_t id, const char* name, int32_t steps);
bool remove(uint8_t id);

bool gotoById(uint8_t id);
size_t count();

} // namespace preset