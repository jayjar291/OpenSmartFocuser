#include "serial_command_handler.h"
#include "serial_command_index.h"
#include "movement.h"
#include "debug_serial.h"
#include "PayloadParser.h"
#include "preset.h"
#include "menu.h"

#include <cstring>

namespace SerialCommandHandler {

namespace {

constexpr size_t kMaxCommandLength = 64;
constexpr size_t kMaxPayloadLength = 64;
constexpr const char* kResponseAck = ":ACK#";
constexpr const char* kResponseUnknown = ":ER01#";
constexpr const char* kResponseInvalidArgs = ":ER02#";
constexpr const char* kResponseBusy = ":ER03#";
constexpr const char* kResponseHomingRequired = ":ER04#";
constexpr const char* kResponseAddonUnavalable = ":ER05#";
constexpr const char* kResponsePositionExceedLimit  = ":ER06#";
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
//
void begin(HardwareSerial& serial) {
  gSerial = &serial;
  gCommandLength = 0;
  gCapturing = false;
}

// This should be called frequently in the main loop to process incoming serial data.
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
        gSerial->println(kResponseUnknown);
      }
      gCommandLength = 0;
      gCapturing = false;
    }
  }
}

//------------------------------------------------------status commands below------------------------------------------------------

//:PP# heartbeat, response :PP#.
void handleHeartbeat(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->println(":PP#");
}

//:FV# get firmware version, response :FV<versionString>#.
void handleGetFirmwareVersion(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->println(":TODO#");
}

//:GM# get movement status, response :GM<statusString>#.
void handleGetMovement(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  Movement::MovementStatus status = Movement::getMovementStatus();
   gSerial->print(":GM");
   switch (status) {
     case Movement::MovementStatus::Idle:
       gSerial->print("Idle");
       break;
     case Movement::MovementStatus::Moving:
       gSerial->print("Moving");
       break;
     case Movement::MovementStatus::Homing:
       gSerial->print("Homing");
       break;
     case Movement::MovementStatus::Error:
       gSerial->print("Error");
       break;
   }
   gSerial->println("#");
}

//:GS# get current speed setting, response :GS<speed># where speed is 1-5.
void handleGetSpeed(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->print(":GS");
  uint8_t speedSetting = Movement::getSpeedSetting();
  if (speedSetting <= 4) {
    gSerial->print(speedSetting);
  } else {
    gSerial->print("Unknown");
  }
  gSerial->println("#");
}

//:GP# get current position in steps, response :GP<positionSteps>#.
void handleGetPosition(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->print(":GP");
  gSerial->print(Movement::getCurrentPositionSteps());
  gSerial->println("#");
}

//:SP<positionSteps># override current position in steps, response :ACK#.
void handleSetPosition(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 1, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }
  int32_t positionSteps = 0;
  if (!readInt32Arg(args, 0, positionSteps)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }
  if (!Movement::checkSoftEndstops(positionSteps)) {
    gSerial->println(kResponsePositionExceedLimit);
    return;
  }
  Movement::setCurrentPositionSteps(positionSteps);
  gSerial->println(kResponseAck);
}

//-----------------------------------------------------------homing command below------------------------------------------------------

//:GH# home, response :ACK#.
void handleHome(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->println(kResponseAck);
  Movement::startHoming();
}

//-----------------------------------------------starmap target commands below------------------------------------------------------

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

void handleClearTarget(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->println(":TODO#");
}

//----------------------------------------------preset commands below------------------------------------------------------

//:PG<presetId># goto preset by id, response :ACK#.
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
    gSerial->println(kResponseInvalidArgs);
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
    gSerial->println(kResponseInvalidArgs);
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

//--------------------------------------------------------------config commands below------------------------------------------------------

//:CI# get motor current in mA, response :CI<currentMa>#.
void handleGetCurrent(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->print(":CI");
  gSerial->print(Movement::getDriverCurrentMa());
  gSerial->println("#");
}

//:CU# get microstep setting, response :CU<currentMa>#.
void handleGetMicrosteps(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->print(":CU");
  gSerial->print(Movement::getDriverMicrosteps());
  gSerial->println("#");
}

//----------------------------------------------------------------motion commands below------------------------------------------------------

//:MA<positionSteps># move to absolute position in steps, response :ACK#.
void handleMoveAbsolute(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 1, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  int32_t positionSteps = 0;
  if (!readInt32Arg(args, 0, positionSteps)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  if (!Movement::checkSoftEndstops(positionSteps)) {
    gSerial->println(kResponsePositionExceedLimit);
    return;
  }

  Movement::moveToPosition(positionSteps);
  gSerial->println(kResponseAck);
}

//:MR<relativeSteps># move relative number of steps, response :ACK#.
void handleMoveRelative(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 1, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  int32_t relativeSteps = 0;
  if (!readInt32Arg(args, 0, relativeSteps)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  if (!Movement::checkSoftEndstops(Movement::getCurrentPositionSteps() + relativeSteps)) {
    gSerial->println(kResponsePositionExceedLimit);
    return;
  }

  Movement::moveRelative(relativeSteps);
  gSerial->println(kResponseAck);
}
//:MH# stop motion immediately, response :ACK#.
void handleHalt(const char* parameters, size_t parametersLength) {  
  (void)parameters;
  (void)parametersLength;
  Movement::halt();
  gSerial->println(kResponseAck);
}
//:MS<speed 0-4># set movement speed, response :ACK#.
void handleSetSpeed(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 1, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }
  int32_t speed = 0;
  if (!readInt32Arg(args, 0, speed)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }
  if (speed < 0 || speed > 4) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }
  Movement::setSpeedSetting(static_cast<uint8_t>(speed));
  adjustFocusSpeedSetting(speed);
  gSerial->println(kResponseAck);
}


//-------------------------------------------------------------------system commands below------------------------------------------------------
//:RB# reboot, response :ACK#. and 3 second delay before rebooting.
void handleReboot(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->println(kResponseAck);
  delay(1000);
  gSerial->print(":Rebooting.");
  delay(1000);
  gSerial->print(".");
  delay(1000);
  gSerial->println(".#");
  delay(250);
  ESP.restart();
}

} // namespace SerialCommandHandler