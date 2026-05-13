#include "serial_command_index.h"

#include <cstring>

namespace SerialCommandIndex {

namespace {

const CommandEntry kCommands[] = {
  //--------------------------------------------------------------status commands below------------------------------------------------------
  //:GP# get current position in steps, response :GP<steps>#
  {":GP", SerialCommandHandler::handleGetPosition},
  //:GM# get current movement state, response :GM<state># where state is IDLE, MOVING, or HOMING.
  {":GM", SerialCommandHandler::handleGetMovement},
  //:GS# get current speed setting, response :GS<speed># where speed is 0-4.
  {":GS", SerialCommandHandler::handleGetSpeed}, 
  //:SP<position># override current position in steps, response :ACK#.
  {":SP", SerialCommandHandler::handleSetPosition}, 
  //:VF# get firmware version, response :VF<version>#.
  {":VF", SerialCommandHandler::handleGetFirmwareVersion}, // Not implemented yet.
  //:PP# heartbeat command, response :PP#.
  {":PP", SerialCommandHandler::handleHeartbeat}, 
  //--------------------------------------------------------------homing commands below------------------------------------------------------
  //:HM# start homing sequence, response :ACK#
  {":HM", SerialCommandHandler::handleHome},
  //--------------------------------------------------------------starmap target commands below------------------------------------------------------
  //:TI# get DSO target RA, DEC, name, response :TI<RA>,<DEC>,<name>#
  {":TI", SerialCommandHandler::handleGetTarget},
  //:TG<RA>,<DEC>,<name># set DSO target RA, DEC, response :ACK#.
  {":TG", SerialCommandHandler::handleSetTarget},
  //:TC# clear DSO target, response :ACK#.
  {":TC", SerialCommandHandler::handleClearTarget},
  //--------------------------------------------------------------preset commands below------------------------------------------------------
  //:PG<presetId># goto preset by id, response :ACK#.
  {":PG", SerialCommandHandler::handleGotoPreset},
  //:PR<presetId># get preset by id, response :PR<presetId>,<name>,<steps>#.
  {":PR", SerialCommandHandler::handleGetPreset},
  //:PL# list presets, response :PL<presetId>,<name>,<steps># ends with :PL!# 
  {":PL", SerialCommandHandler::handleListPresets},
  //:PA<steps>,<presetName># add preset, response :PA<presetId>,<presetName>#. If steps is -1, current position is used.
  {":PA", SerialCommandHandler::handleAddPreset},
  //:PS<presetId>,<steps>,<presetName># set preset by id, response :ACK#. If steps is -1, current position is used.
  {":PS", SerialCommandHandler::handleSetPreset},
  //:PC<presetId># remove preset by id, response :ACK#.
  {":PC", SerialCommandHandler::handleRemovePreset},
  //--------------------------------------------------------------config commands below------------------------------------------------------
  //:CI# get motor current in mA, response :CI<currentMa>#.
  {":CI", SerialCommandHandler::handleGetCurrent},
  //:CU# get microstep setting, response :CU<currentMa>#.
  {":CU", SerialCommandHandler::handleGetMicrosteps}, 
  //--------------------------------------------------------------movement commands below------------------------------------------------------
  //:MA<positionSteps># move to absolute position in steps, response :ACK#.
  {":MA", SerialCommandHandler::handleMoveAbsolute}, 
  //:MR<relativeSteps># move relative number of steps, response :ACK#.
  {":MR", SerialCommandHandler::handleMoveRelative}, 
  //:MH# stop motion immediately, response :ACK#.
  {":MH", SerialCommandHandler::handleHalt}, 
  //:MS<speed 0-4># set movement speed, response :ACK#.
  {":MS", SerialCommandHandler::handleSetSpeed}, 
  //--------------------------------------------------------------system commands below------------------------------------------------------
  //:RB# reboot the system, response :ACK# and 3 second delay before rebooting.
  {":RB", SerialCommandHandler::handleReboot}, // Reboot command, response :ACK#. and 3 second delay before rebooting.
  //:TM# toggle motor enabled state, response :TM<state># state is 1 for enabled, 0 for disabled.
  {":TM", SerialCommandHandler::handleToggleMotor},
  //:DM# disable motor, response :ACK#.
  {":DM", SerialCommandHandler::handleDisableMotor},
  //:EM# enable motor, response :ACK#.
  {":EM", SerialCommandHandler::handleEnableMotor},
  //--------------------------------------------------------------addon commands below------------------------------------------------------
  // TODO addon commands for sensors, auxiliary outputs, etc.
};

} // namespace

bool dispatch(const char* frame, size_t frameLength) {
  // Basic frame validation before token matching.
  if (frame == nullptr || frameLength < 4 || frame[0] != ':' || frame[frameLength - 1] != '#') {
    return false;
  }

  // Compare the incoming token against each registered command entry.
  for (const CommandEntry& entry : kCommands) {
    const size_t tokenLength = std::strlen(entry.token);
    if (tokenLength < 2 || frameLength < tokenLength + 1) {
      continue;
    }

    if (std::memcmp(entry.token, frame, tokenLength) == 0) {
      if (entry.callback == nullptr) {
        return false;
      }

      // Payload is everything after token and before trailing '#'.
      const char* parameters = frame + tokenLength;
      const size_t parametersLength = frameLength - tokenLength - 1;
      entry.callback(parameters, parametersLength);
      return true;
    }
  }

  // No token match found in the index.
  return false;
}

} // namespace SerialCommandIndex
