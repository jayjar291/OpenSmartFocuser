#include "serial_command_index.h"

#include <cstring>

namespace SerialCommandIndex {

namespace {

const CommandEntry kCommands[] = {
  {":GP", 3, SerialCommandHandler::handleGetPosition},
  {":HM", 3, SerialCommandHandler::handleHome},
  {":TI", 3, SerialCommandHandler::handleGetTarget},
  {":TG", 3, SerialCommandHandler::handleSetTarget},
  {":PG", 3, SerialCommandHandler::handleGotoPreset},
  {":PR", 3, SerialCommandHandler::handleGetPreset},
  {":PL", 3, SerialCommandHandler::handleListPresets},
  {":PA", 3, SerialCommandHandler::handleAddPreset},
  {":PS", 3, SerialCommandHandler::handleSetPreset},
  {":PC", 3, SerialCommandHandler::handleRemovePreset},
};

} // namespace

bool dispatch(const char* frame, size_t frameLength) {
  // Basic frame validation before token matching.
  if (frame == nullptr || frameLength < 4 || frame[0] != ':' || frame[frameLength - 1] != '#') {
    return false;
  }

  // Compare the incoming token against each registered command entry.
  for (const CommandEntry& entry : kCommands) {
    const size_t tokenLength = entry.tokenLength;
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
