#include "serial_command_handler.h"
#include "serial_command_index.h"
#include "movement.h"
#include "debug_serial.h"
#include "PayloadParser.h"
#include "preset.h"

#include <cstring>

namespace SerialCommandHandler {

namespace {

constexpr size_t kMaxCommandLength = 64;
constexpr size_t kMaxPayloadLength = 64;
constexpr const char* kResponseAck = ":ACK#";
constexpr const char* kResponseInvalidArgs = ":ER02#";
constexpr const char* kResponseError = ":ERR#";
HardwareSerial* gSerial = nullptr;
char gCommandBuffer[kMaxCommandLength + 1] = {0};
size_t gCommandLength = 0;
bool gCapturing = false;

bool parseArgs(const char* parameters,
               size_t parametersLength,
               uint8_t expectedCount,
               char (&payload)[kMaxPayloadLength],
               ParsedArgs& outArgs) {
  if (parameters == nullptr || parametersLength == 0 || parametersLength >= sizeof(payload)) {
    return false;
  }

  memcpy(payload, parameters, parametersLength);
  payload[parametersLength] = '\0';

  outArgs = PayloadParserFixed::parseInPlace(payload);
  return !outArgs.truncated && outArgs.count == expectedCount;
}

bool readInt32Arg(const ParsedArgs& args, uint8_t index, int32_t& outValue) {
  if (index >= args.count) {
    return false;
  }
  return PayloadParserFixed::asInt32(args.args[index], outValue);
}

bool readNonEmptyStringArg(const ParsedArgs& args, uint8_t index, const char*& outValue) {
  if (index >= args.count) {
    return false;
  }
  if (!PayloadParserFixed::asString(args.args[index], outValue)) {
    return false;
  }
  return outValue != nullptr && outValue[0] != '\0';
}

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
        gSerial->println(kResponseError);
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
  gSerial->println(kResponseAck);
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
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 1, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  int32_t presetId = 0;
  if (!readInt32Arg(args, 0, presetId)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  if (!preset::gotoById(static_cast<uint8_t>(presetId))) {
    gSerial->println(kResponseError);
    return;
  }

  gSerial->println(kResponseAck);
}
//:PR<presetId># get preset by id, response :PR<presetId>,<name>,<steps>#.
void handleGetPreset(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 1, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  int32_t presetId = 0;
  if (!readInt32Arg(args, 0, presetId)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  preset::Preset p{};
  if (!preset::getById(static_cast<uint8_t>(presetId), p)) {
    gSerial->println(kResponseError);
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
//:PL# list presets, response :PL<presetId>,<name>,<steps># ends with :PL!#
void handleListPresets(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
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
//:PA<steps>,<presetName># add preset, response :PA<presetId>,<presetName>#. If steps is -1, current position is used.
void handleAddPreset(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 2, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  int32_t steps = 0;
  const char* parsedName = nullptr;
  if (!readInt32Arg(args, 0, steps)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }
  if (!readNonEmptyStringArg(args, 1, parsedName)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  char name[preset::kMaxNameLen] = {0};
  strncpy(name, parsedName, sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';

  if (steps == -1) {
    steps = Movement::getCurrentPositionSteps();
  }

  uint8_t newId = 0;
  if (!preset::add(name, steps, newId)) {
    gSerial->println(kResponseError);
    return;
  }

  gSerial->print(":PA");
  gSerial->print(newId);
  gSerial->print(",");
  gSerial->print(name);
  gSerial->println("#");
}
//:PC<presetId># remove preset by id, response :ACK#.
void handleRemovePreset(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 1, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  int32_t presetId = 0;
  if (!readInt32Arg(args, 0, presetId)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  if (!preset::remove(static_cast<uint8_t>(presetId))) {
    gSerial->println(kResponseError);
    return;
  }
  gSerial->println(kResponseAck);
}

//:PS<presetId>,<steps>,<presetName># set preset by id, response :ACK#. If steps is -1, current position is used.
void handleSetPreset(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 3, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  int32_t presetId = 0;
  int32_t steps = 0;
  const char* parsedName = nullptr;
  if (!readInt32Arg(args, 0, presetId)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }
  if (!readInt32Arg(args, 1, steps)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }
  if (!readNonEmptyStringArg(args, 2, parsedName)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  char name[preset::kMaxNameLen] = {0};
  strncpy(name, parsedName, sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';

  if (steps == -1) {
    steps = Movement::getCurrentPositionSteps();
  }

  if (!preset::set(static_cast<uint8_t>(presetId), name, steps)) {
    gSerial->println(kResponseError);
    return;
  }

  gSerial->println(kResponseAck);
}

} // namespace SerialCommandHandler