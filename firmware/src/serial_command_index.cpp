#include "serial_command_index.h"

#include <cstring>

namespace SerialCommandIndex {

namespace {

const CommandEntry kCommands[] = {
    {":GP#", 4, SerialCommandHandler::handleGetPosition},
    {":HM#", 4, SerialCommandHandler::handleHome},
  {":TI#", 4, SerialCommandHandler::handleGetTarget},

    // Example command registration:
  // {":SP#", 4, SerialCommandHandler::handleSetPosition},
};

} // namespace

const CommandEntry* find(const char* token, size_t tokenLength) {
  for (const CommandEntry& entry : kCommands) {
    if (entry.tokenLength == tokenLength &&
        std::memcmp(entry.token, token, tokenLength) == 0) {
      return &entry;
    }
  }
  return nullptr;
}

} // namespace SerialCommandIndex
