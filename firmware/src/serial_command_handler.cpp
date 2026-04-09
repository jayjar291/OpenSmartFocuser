#include "serial_command_handler.h"
#include "serial_command_index.h"
#include "movement.h"
#include "debug_serial.h"
#include "preset.h"

namespace SerialCommandHandler {

namespace {

constexpr size_t kMaxCommandLength = 64;
HardwareSerial* gSerial = nullptr;
char gCommandBuffer[kMaxCommandLength + 1] = {0};
size_t gCommandLength = 0;
bool gCapturing = false;

} // namespace

void begin(HardwareSerial& serial) {
  gSerial = &serial;
  gCommandLength = 0;
  gCapturing = false;
}

void poll() {
  // Guard against use before begin().
  if (gSerial == nullptr) {
    return;
  }
  // Consume all available bytes and build ':' ... '#' framed commands.
  while (gSerial->available() > 0) {
    const char incoming = static_cast<char>(gSerial->read());
    // Ignore bytes until we see a frame start marker.
    if (!gCapturing) {
      if (incoming == ':') {
        gCapturing = true;
        gCommandLength = 0;
        gCommandBuffer[gCommandLength++] = incoming;
      }
      continue;
    }
    // Drop oversized frames to avoid buffer overflow.
    if (gCommandLength >= kMaxCommandLength) {
      gCommandLength = 0;
      gCapturing = false;
      continue;
    }
    gCommandBuffer[gCommandLength++] = incoming;
    // Dispatch a completed frame once terminator is received.
    if (incoming == '#') {
      gCommandBuffer[gCommandLength] = '\0';
      DebugSerial::printFramed("Received command frame:");
      DebugSerial::printFramed(gCommandBuffer);
      if (!SerialCommandIndex::dispatch(gCommandBuffer, gCommandLength)) {
        gSerial->println(":ERR#");
      }
      gCommandLength = 0;
      gCapturing = false;
    }
  }
}

void handleGetPosition(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->print(":GP");
  gSerial->print(Movement::getCurrentPositionSteps());
  gSerial->println("#");
}

void handleHome(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->println(":ACK#");
  Movement::startHoming();
}

void handleGetTarget(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->println(":TODO#");
}

void handleSetTarget(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->println(":TODO#");
}

void handleGotoPreset(const char* parameters, size_t parametersLength) {
  int32_t presetId = 0;
  if (!parseNumberParameter(parameters, parametersLength, presetId)) {
    gSerial->println(":ERR#");
    return;
  }

  if (!preset::gotoById(static_cast<uint8_t>(presetId))) {
    gSerial->println(":ERR#");
    return;
  }

  gSerial->println(":ACK#");
}

void handleGetPreset(const char* parameters, size_t parametersLength) {
  int32_t presetId = 0;
  if (!parseNumberParameter(parameters, parametersLength, presetId)) {
    gSerial->println(":ERR#");
    return;
  }
  preset::Preset p{};
  if (!preset::getById(static_cast<uint8_t>(presetId), p)) {
    gSerial->println(":ERR#");
    return;
  }
  gSerial->print(":PR");
  gSerial->print(p.id);
  gSerial->print(",");
  gSerial->print(p.steps);
  gSerial->print(",");
  gSerial->print(p.name);
  gSerial->println("#");
}

void handleListPresets(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  uint8_t count = preset::count();
  preset::Preset p{};
  const uint8_t cap = preset::capacity();
  for (uint8_t i = 0; i < cap; ++i) {
    if (!preset::getByIndex(i, p) || !p.used) {
      continue;
    }

    gSerial->print(":PL");
    gSerial->print(p.id);
    gSerial->print(",");
    gSerial->print(p.steps);
    gSerial->print(",");
    gSerial->print(p.name);
    gSerial->println("#");
  }
  
  gSerial->println(":PL!#");
}

void handleAddPreset(const char* parameters, size_t parametersLength) {
  int32_t steps = 0;
  char name[preset::kMaxNameLen] = {0};

  if (!parseAddPresetPayload(parameters, parametersLength, steps, name, sizeof(name))) {
    gSerial->println(":ERR#");
    return;
  }

  if (steps == -1) {
    steps = Movement::getCurrentPositionSteps();
  }

  uint8_t newId = 0;
  if (!preset::add(name, steps, newId)) {
    gSerial->println(":ERR#");
    return;
  }

  gSerial->print(":PA");
  gSerial->print(newId);
  gSerial->print(",");
  gSerial->print(name);
  gSerial->println("#");
}

void handleRemovePreset(const char* parameters, size_t parametersLength) {
  int32_t presetId = 0;
  if (!parseNumberParameter(parameters, parametersLength, presetId)) {
    gSerial->println(":ERR#");
    return;
  }

  if (!preset::remove(static_cast<uint8_t>(presetId))) {
    gSerial->println(":ERR#");
    return;
  }

  gSerial->println(":ACK#");
}

void handleSetPreset(const char* parameters, size_t parametersLength) {
  int32_t presetId = 0;
  int32_t steps = 0;
  char name[preset::kMaxNameLen] = {0};

  if (!parseAddPresetPayload(parameters, parametersLength, steps, name, sizeof(name))) {
    gSerial->println(":ERR#");
    return;
  }

  if (presetId == -1) {
    gSerial->println(":ERR#");
    return;
  }

  if (steps == -1) {
    steps = Movement::getCurrentPositionSteps();
  }

  if (!preset::set(static_cast<uint8_t>(presetId), name, steps)) {
    gSerial->println(":ERR#");
    return;
  }

  gSerial->println(":ACK#");
}

static bool parseAddPresetPayload(
    const char* parameters,
    size_t parametersLength,
    int32_t& outSteps,
    char* outName,
    size_t outNameSize) {
  if (parameters == nullptr || parametersLength == 0 || outName == nullptr || outNameSize == 0) {
    return false;
  }

  // 1) Bound and terminate payload copy.
  char payload[64];
  if (parametersLength >= sizeof(payload)) {
    return false;
  }
  memcpy(payload, parameters, parametersLength);
  payload[parametersLength] = '\0';

  // 2) Split once at comma: "<steps>,<name>"
  char* comma = strchr(payload, ',');
  if (comma == nullptr) {
    return false;
  }
  *comma = '\0';

  char* stepsText = payload;
  char* nameText = comma + 1;
  if (*stepsText == '\0' || *nameText == '\0') {
    return false;
  }

  // Parse steps.
  char* endPtr = nullptr;
  long steps = strtol(stepsText, &endPtr, 10);
  if (endPtr == stepsText || *endPtr != '\0') {
    return false;
  }

  // Copy name safely.
  strncpy(outName, nameText, outNameSize - 1);
  outName[outNameSize - 1] = '\0';

  outSteps = static_cast<int32_t>(steps);
  return true;
}

static bool parseNumberParameter(
    const char* parameters,
    size_t parametersLength,
    int32_t& outNumber) {
  if (parameters == nullptr || parametersLength == 0) {
    return false;
  }

  char buffer[32];
  if (parametersLength >= sizeof(buffer)) {
    return false;
  }
  memcpy(buffer, parameters, parametersLength);
  buffer[parametersLength] = '\0';

  char* endPtr = nullptr;
  long value = strtol(buffer, &endPtr, 10);
  if (endPtr == buffer || *endPtr != '\0') {
    return false;
  }

  outNumber = static_cast<int32_t>(value);
  return true;
}

} // namespace SerialCommandHandler