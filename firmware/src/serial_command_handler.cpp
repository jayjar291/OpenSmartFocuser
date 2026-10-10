#include "serial_command_handler.h"
#include "addons.h"
#include "serial_command_index.h"
#include "movement.h"
#include "debug_serial.h"
#include "PayloadParser.h"
#include "preset.h"
#include "menu.h"
#include "StarMap.h"

#include <cstring>

namespace SerialCommandHandler {

namespace {

constexpr size_t kMaxCommandLength = 64;
constexpr size_t kMaxPayloadLength = 64;
constexpr const char* kResponseAck = ":ACK#";
constexpr const char* kResponseUnknown = ":ER01#";
constexpr const char* kResponseInvalidArgs = ":ER02#";
constexpr const char* kResponseAddonUnavalable = ":ER05#";
constexpr const char* kResponsePositionExceedLimit  = ":ER06#";
constexpr const char* kResponseError = ":ERR#";

Stream* gSerial = nullptr;
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

bool readPresetIdArg(const ParsedArgs& args, uint8_t index, uint8_t& outPresetId) {
  int32_t presetId = 0;
  if (!readInt32Arg(args, index, presetId) || presetId < 1 || presetId > preset::capacity()) {
    return false;
  }
  outPresetId = static_cast<uint8_t>(presetId);
  return true;
}

bool readFloatArg(const ParsedArgs& args, uint8_t index, float& outValue) {
  if (index >= args.count) {
    return false;
  }

  // Accept native float tokens.
  if (PayloadParserFixed::asFloat(args.args[index], outValue)) {
    return true;
  }

  // Also accept integer tokens and promote to float.
  int32_t i32Value = 0;
  if (PayloadParserFixed::asInt32(args.args[index], i32Value)) {
    outValue = static_cast<float>(i32Value);
    return true;
  }

  return false;
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
void begin(Stream& serial) {
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

//:PF# Poll focuser status, response :PF<status>,<position>#, if add-ons are present, additional fields may be included [<dustCapPosition>,<LightboxBrightness>].
void handlePollStatus(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->print(":PF");
  switch (Movement::getMovementStatus()) {
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
  gSerial->print(",");
  gSerial->print(Movement::getCurrentPositionSteps());
  // Add additional fields for add-ons here if present.
  //if (Addons::hasAddon(Addons::AddOnType::DustCap)) {
  //  gSerial->print(",");
  //  gSerial->print(Addons::getDustCapPosition());
  //}
  //if (Addons::hasAddon(Addons::AddOnType::Lightbox)) {
  //  gSerial->print(",");
  //  gSerial->print(Addons::getLightboxBrightness());
  //}
  gSerial->println("#");
}

//:FV# get firmware version, response :FV<versionString>#.
void handleGetFirmwareVersion(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->print(":VF");
  gSerial->print(VERSION);
  gSerial->println("#");
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

//:GL# get limits max and min in steps, response :GL<minSteps>,<maxSteps>#.
void handleGetLimits(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  int32_t minSteps = 0;
  int32_t maxSteps = 0;
  Movement::getLimits(minSteps, maxSteps);
  gSerial->print(":GL");
  gSerial->print(minSteps);
  gSerial->print(",");
  gSerial->print(maxSteps);
  gSerial->println("#");
}
//:GT# get current step per millimeter, response :GT<stepsPerMm>#.
void handleGetStepsPerMm(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->print(":GT");
  gSerial->print(Movement::getStepsPerMm());
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

//:TG# get DSO target RA, DEC, name, response :TG<RA>,<DEC>,<name>#
void handleGetTarget(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  float raDeg = 0.0f;
  float decDeg = 0.0f;
  const char* name = nullptr;
  StarMap::getTarget(raDeg, decDeg, name);

  gSerial->print(":TG");
  gSerial->print(raDeg, 4);
  gSerial->print(",");
  gSerial->print(decDeg, 4);
  gSerial->print(",");
  if (name != nullptr) {
    gSerial->print(name);
  }
  gSerial->println("#");
}

//:TS<RA>,<DEC>,<name># set DSO target RA, DEC, response :ACK#.
void handleSetTarget(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 3, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  float raDeg = 0.0f;
  float decDeg = 0.0f;
  const char* name = nullptr;
  if (!readFloatArg(args, 0, raDeg) || !readFloatArg(args, 1, decDeg) ||
      !readNonEmptyStringArg(args, 2, name)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  StarMap::setTarget(raDeg, decDeg, name);
  gSerial->println(kResponseAck);
}

//:TC# clear DSO target, response :ACK#.
void handleClearTarget(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  StarMap::clearTarget();
  gSerial->println(kResponseAck);
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

  uint8_t presetId = 0;
  if (!readPresetIdArg(args, 0, presetId)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  if (!preset::gotoById(presetId)) {
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

  uint8_t presetId = 0;
  if (!readPresetIdArg(args, 0, presetId)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  preset::Preset p{};
  if (!preset::getById(presetId, p)) {
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

//:PL# list presets, response :PL<presetId>,<name>,<steps># additional presets are added as[;<presetId>,<name>,<steps>]#
void handleListPresets(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  preset::Preset p{};
  const uint8_t cap = preset::capacity();
  gSerial->print(":PL");
  for (uint8_t i = 0; i < cap; ++i) {
    if (!preset::getByIndex(i, p) || !p.used) {
      continue;
    }
    gSerial->print(p.id);
    gSerial->print(",");
    gSerial->print(p.steps);
    gSerial->print(",");
    gSerial->print(p.name);
    if (i < cap - 1) {
      gSerial->print(";");
    }
  }
  gSerial->println("#");
}

//:PA<steps>,<presetName># add preset, response :PA<presetId>,<presetName>,<Steps>#. If steps is -1, current position is used.
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
  gSerial->print(",");
  gSerial->print(steps);
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

  uint8_t presetId = 0;
  if (!readPresetIdArg(args, 0, presetId)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  preset::Preset existingPreset{};
  if (!preset::getById(presetId, existingPreset)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }
  if (!preset::remove(presetId)) {
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

  uint8_t presetId = 0;
  int32_t steps = 0;
  const char* parsedName = nullptr;
  if (!readPresetIdArg(args, 0, presetId)) {
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

  preset::Preset existingPreset{};
  if (!preset::getById(presetId, existingPreset)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }
  if (!preset::set(presetId, name, steps)) {
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
//:MM<MillimeterSteps># move to absolute position in millimeters, response :ACK#.
void handleMoveAbsoluteMillimeters(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 1, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  float positionMillimeters = 0;
  if (!readFloatArg(args, 0, positionMillimeters)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  Movement::moveToPositionMm(positionMillimeters);
  gSerial->println(kResponseAck);
}

//:MN<MillimeterSteps># move relative number of millimeters, response :ACK#.
void handleMoveRelativeMillimeters(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 1, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  float relativeMillimeters = 0;
  if (!readFloatArg(args, 0, relativeMillimeters)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  int32_t relativeSteps = static_cast<int32_t>(relativeMillimeters * Movement::getStepsPerMm());
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
  DebugSerial::printFramed("Rebooting.");
  delay(1000);
  DebugSerial::printFramed("Rebooting..");
  delay(1000);
  DebugSerial::printFramed("Rebooting...");
  delay(250);
  ESP.restart();
}
//:TM# toggle motor enabled state, response :TM<state># state is 1 for enabled, 0 for disabled.
void handleToggleMotor(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  Movement::toggleMotorEnabled();
  gSerial->print(":TM");
  gSerial->print(Movement::isMotorEnabled() ? "1" : "0");
  gSerial->println("#");
}

//:DM# disable motor, response :ACK#.
void handleDisableMotor(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  if (!Movement::isMotorEnabled()) {
    gSerial->println(kResponseAck);
    return;
  }
  Movement::setMotorEnabledState(false);
  gSerial->println(kResponseAck);
}

//:EM# enable motor, response :ACK#.
void handleEnableMotor(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  if (Movement::isMotorEnabled()) {
    gSerial->println(kResponseAck);
    return;
  }
  Movement::setMotorEnabledState(true);
  gSerial->println(kResponseAck);
}
//:AQ# Queries add-ons, response :AQ<Type># additional add-ons are added as [,<Type>] example :AQ<Type1>,<Type2># if multiple add-ons are present.
void handleQueryAddons(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;

  gSerial->print(":AQ");
  if (Addons::hasAddon(Addons::AddonType::None)) {
    gSerial->print("None");
  }
  else
  {
    if (Addons::hasAddon(Addons::AddonType::Shutter)) {
      gSerial->print("Shutter");
    }

    if (Addons::hasAddon(Addons::AddonType::FlatPanel)) {
      gSerial->print(",FlatPanel");
    }
  }
  gSerial->println("#");
}

void handleSetFlatPanelBrightness(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 1, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  int32_t brightness = 0;
  if (!readInt32Arg(args, 0, brightness)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  if (brightness < 0 || brightness > 255) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  if (!Addons::hasAddon(Addons::AddonType::FlatPanel) || !Addons::isInitialized()) {
    gSerial->println(kResponseAddonUnavalable);
    return;
  }

  Addons::setFlatPanelBrightness(static_cast<uint8_t>(brightness));
  gSerial->println(kResponseAck);
}

void handleSetShutterPosition(const char* parameters, size_t parametersLength) {
  char payload[kMaxPayloadLength] = {0};
  ParsedArgs args;
  if (!parseArgs(parameters, parametersLength, 1, payload, args)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  int32_t position = 0;
  if (!readInt32Arg(args, 0, position)) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  if (position < 0 || position > 270) {
    gSerial->println(kResponseInvalidArgs);
    return;
  }

  if (!Addons::hasAddon(Addons::AddonType::Shutter) || !Addons::isInitialized()) {
    gSerial->println(kResponseAddonUnavalable);
    return;
  }

  Addons::setShutterPosition(static_cast<uint16_t>(position));
  gSerial->println(kResponseAck);
}

} // namespace SerialCommandHandler