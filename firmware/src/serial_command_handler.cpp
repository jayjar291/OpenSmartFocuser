#include "serial_command_handler.h"

#include <cstdlib>
#include <cstring>
#include "debug_serial.h"
#include "movement.h"
#include "serial_command_index.h"

extern float idleMapCenterRaDeg;
extern float idleMapCenterDecDeg;
extern long loadPresetPositionById(uint8_t id);
extern void gotoPresetPositionById(uint8_t id);

namespace SerialCommandHandler {

namespace {

constexpr size_t kMaxCommandLength = 95;
constexpr size_t kMaxTargetNameLength = 16;
HardwareSerial* gSerial = nullptr;
char gCommandBuffer[kMaxCommandLength + 1] = {0};
size_t gCommandLength = 0;
bool gCapturing = false;
bool gHomeCompletionPending = false;
char gTargetName[kMaxTargetNameLength + 1] = {0};

bool startsWith(const char* text, const char* prefix) {
  while (*prefix != '\0') {
    if (*text == '\0' || *text != *prefix) {
      return false;
    }
    ++text;
    ++prefix;
  }
  return true;
}

float wrapRaDegrees(float raDeg) {
  while (raDeg >= 360.0f) {
    raDeg -= 360.0f;
  }
  while (raDeg < 0.0f) {
    raDeg += 360.0f;
  }
  return raDeg;
}

float clampDecDegrees(float decDeg) {
  if (decDeg > 90.0f) {
    return 90.0f;
  }
  if (decDeg < -90.0f) {
    return -90.0f;
  }
  return decDeg;
}

bool parseSetTarget(const char* command,
                    float& raOut,
                    float& decOut,
                    char* nameOut,
                    size_t nameOutSize) {
  if (!startsWith(command, ":TG") || command[0] != ':') {
    return false;
  }

  const size_t length = std::strlen(command);
  if (length < 6 || command[length - 1] != '#') {
    return false;
  }

  const char* payloadStart = command + 3;
  char payload[kMaxCommandLength + 1] = {0};
  std::strncpy(payload, payloadStart, sizeof(payload) - 1);
  char* hashPos = std::strchr(payload, '#');
  if (hashPos == nullptr) {
    return false;
  }
  *hashPos = '\0';

  char* firstComma = std::strchr(payload, ',');
  if (firstComma == nullptr) {
    return false;
  }
  *firstComma = '\0';

  char* secondComma = std::strchr(firstComma + 1, ',');

  char* raEnd = nullptr;
  const float parsedRa = std::strtof(payload, &raEnd);
  if (raEnd == payload || *raEnd != '\0') {
    return false;
  }

  char* decStart = firstComma + 1;
  if (*decStart == '\0') {
    return false;
  }

  if (secondComma != nullptr) {
    *secondComma = '\0';
  }

  char* decEnd = nullptr;
  const float parsedDec = std::strtof(decStart, &decEnd);
  if (decEnd == decStart || *decEnd != '\0') {
    return false;
  }

  nameOut[0] = '\0';
  if (secondComma != nullptr) {
    const char* nameStart = secondComma + 1;
    const size_t nameLength = std::strlen(nameStart);
    if (nameLength > kMaxTargetNameLength || nameLength >= nameOutSize) {
      return false;
    }
    std::strncpy(nameOut, nameStart, nameOutSize - 1);
    nameOut[nameOutSize - 1] = '\0';
  }

  raOut = wrapRaDegrees(parsedRa);
  decOut = clampDecDegrees(parsedDec);
  return true;
}

bool parsePresetId(const char* command, size_t prefixLength, uint8_t& idOut) {
  const size_t length = std::strlen(command);
  if (length <= prefixLength + 1 || command[length - 1] != '#') {
    return false;
  }
  const char* idStart = command + prefixLength;
  char* idEnd = nullptr;
  const unsigned long parsed = std::strtoul(idStart, &idEnd, 10);
  if (idEnd == idStart || *idEnd != '#' || parsed == 0 || parsed > 255) {
    return false;
  }
  idOut = static_cast<uint8_t>(parsed);
  return true;
}

void resetParser() {
  gCommandLength = 0;
  gCapturing = false;
}

void dispatchCommand(const char* command, size_t commandLength) {
  if (startsWith(command, ":TG") && commandLength >= 6 && command[commandLength - 1] == '#') {
    handleSetTarget(*gSerial, command, commandLength);
    return;
  }

  if (startsWith(command, ":PG") && commandLength >= 5 && command[commandLength - 1] == '#') {
    handleGotoPreset(*gSerial, command, commandLength);
    return;
  }

  if (startsWith(command, ":PR") && commandLength >= 5 && command[commandLength - 1] == '#') {
    handleGetPreset(*gSerial, command, commandLength);
    return;
  }

  const SerialCommandIndex::CommandEntry* entry =
      SerialCommandIndex::find(command, commandLength);
  if (entry == nullptr || entry->callback == nullptr) {
    return;
  }

  entry->callback(*gSerial, command, commandLength);
}

} // namespace

void begin(HardwareSerial& serial) {
  gSerial = &serial;
  resetParser();
  gHomeCompletionPending = false;
  gTargetName[0] = '\0';
}

void poll() {
  if (gSerial == nullptr) {
    return;
  }

  while (gSerial->available() > 0) {
    const char incoming = static_cast<char>(gSerial->read());

    if (!gCapturing) {
      if (incoming == ':') {
        gCapturing = true;
        gCommandLength = 0;
        gCommandBuffer[gCommandLength++] = incoming;
      }
      continue;
    }

    if (gCommandLength >= kMaxCommandLength) {
      resetParser();
      continue;
    }

    gCommandBuffer[gCommandLength++] = incoming;

    if (incoming == '#') {
      gCommandBuffer[gCommandLength] = '\0';
      dispatchCommand(gCommandBuffer, gCommandLength);
      resetParser();
    }
  }

  if (gHomeCompletionPending && !Movement::isBusy()) {
    gSerial->println(":HD#");
    gHomeCompletionPending = false;
  }
}

void handleGetPosition(HardwareSerial& serial, const char* command, size_t commandLength) {
  (void)command;
  (void)commandLength;
  char response[24] = {0};
  snprintf(response, sizeof(response), "%ld#", static_cast<long>(Movement::getCurrentPositionSteps()));
  serial.println(response);
}

void handleHome(HardwareSerial& serial, const char* command, size_t commandLength) {
  (void)command;
  (void)commandLength;
  Movement::startHoming();
  serial.println(":ACK#");
  gHomeCompletionPending = Movement::isBusy();

  if (!gHomeCompletionPending) {
    serial.println(":HD#");
  }
}

void handleGetTarget(HardwareSerial& serial, const char* command, size_t commandLength) {
  (void)command;
  (void)commandLength;
  char response[56] = {0};
  snprintf(response, sizeof(response), ":TI%.6f,%.6f#", idleMapCenterRaDeg, idleMapCenterDecDeg);
  serial.println(response);
}

void handleSetTarget(HardwareSerial& serial, const char* command, size_t commandLength) {
  (void)commandLength;

  float parsedRa = 0.0f;
  float parsedDec = 0.0f;
  char parsedName[kMaxTargetNameLength + 1] = {0};

  if (!parseSetTarget(command, parsedRa, parsedDec, parsedName, sizeof(parsedName))) {
    serial.println(":ERR#");
    return;
  }

  idleMapCenterRaDeg = parsedRa;
  idleMapCenterDecDeg = parsedDec;
  std::strncpy(gTargetName, parsedName, sizeof(gTargetName) - 1);
  gTargetName[sizeof(gTargetName) - 1] = '\0';

  serial.println(":ACK#");
}

void handleGotoPreset(HardwareSerial& serial, const char* command, size_t commandLength) {
  (void)commandLength;
  uint8_t id = 0;
  if (!parsePresetId(command, 3, id)) {
    serial.println(":ERR#");
    return;
  }

  const long targetSteps = loadPresetPositionById(id);
  const int32_t currentSteps = Movement::getCurrentPositionSteps();
  const bool wasEnabled = Movement::isMotorEnabled();

  DebugSerial::printFramedValue("PG id=", static_cast<int>(id), "");
  DebugSerial::printFramedValue("PG target=", targetSteps, "");
  DebugSerial::printFramedValue("PG current=", currentSteps, "");
  DebugSerial::printFramedValue("PG motorEnabled=", wasEnabled ? 1 : 0, "");

  if (!wasEnabled) {
    Movement::toggleMotorEnabled();
  }

  Movement::moveToPosition(static_cast<int32_t>(targetSteps));
  serial.println(":ACK#");
}

void handleGetPreset(HardwareSerial& serial, const char* command, size_t commandLength) {
  (void)commandLength;
  uint8_t id = 0;
  if (!parsePresetId(command, 3, id)) {
    serial.println(":ERR#");
    return;
  }
  const long pos = loadPresetPositionById(id);
  char response[32] = {0};
  snprintf(response, sizeof(response), ":PR%u,%ld#", static_cast<unsigned>(id), pos);
  serial.println(response);
}

// Example command callback implementation:
// void handleSetPosition(HardwareSerial& serial, const char* command, size_t commandLength) {
//   (void)serial;
//   (void)command;
//   (void)commandLength;
// }

} // namespace SerialCommandHandler
