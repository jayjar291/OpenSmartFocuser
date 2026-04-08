#include "serial_command_handler.h"
#include "serial_command_index.h"
#include "movement.h"
#include "debug_serial.h"

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
  (void)parameters;
  (void)parametersLength;
  gSerial->println(":TODO#");
}

void handleGetPreset(const char* parameters, size_t parametersLength) {
  (void)parameters;
  (void)parametersLength;
  gSerial->println(":TODO#");
}

} // namespace SerialCommandHandler