#include "serial_command_index.h"

#include <cstring>

namespace SerialCommandIndex {

namespace {

const CommandEntry kCommands[] = {
  //:GP# get current position in steps, response :GP<steps>#
  {":GP", SerialCommandHandler::handleGetPosition},
  //:HM# start homing sequence, response :ACK#
  {":HM", SerialCommandHandler::handleHome},
  //:TI# get DSO target RA, DEC, name, response :TI<RA>,<DEC>,<name>#
  {":TI", SerialCommandHandler::handleGetTarget},
  //:TG<RA>,<DEC>,<name># set DSO target RA, DEC, response :ACK#.
  {":TG", SerialCommandHandler::handleSetTarget},
  //:TC# clear DSO target, response :ACK#.
  {":TC", nullptr}, // TODO: implement handleClearTarget
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
