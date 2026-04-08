#pragma once

#include <cstddef>
#include "serial_command_handler.h"

namespace SerialCommandIndex {

struct CommandEntry {
  const char* token;
  const uint8_t tokenLength;
  SerialCommandHandler::CommandCallback callback;
};

bool dispatch(const char* frame,
              size_t frameLength);

} // namespace SerialCommandIndex
