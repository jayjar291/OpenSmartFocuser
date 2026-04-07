#pragma once

#include <cstddef>
#include "serial_command_handler.h"

namespace SerialCommandIndex {

struct CommandEntry {
  const char* token;
  size_t tokenLength;
  SerialCommandHandler::CommandCallback callback;
};

const CommandEntry* find(const char* token, size_t tokenLength);

} // namespace SerialCommandIndex
