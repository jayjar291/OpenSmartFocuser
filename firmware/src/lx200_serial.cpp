#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "lx200_serial.h"

float idleMapCenterRaDeg = IDLE_STAR_MAP_CENTER_RA_DEG;
float idleMapCenterDecDeg = IDLE_STAR_MAP_CENTER_DEC_DEG;

static const char* skipSpaces(const char* text) {
  while (*text == ' ' || *text == '\t') {
    ++text;
  }
  return text;
}

static bool charEqIgnoreCase(char value, char expectedUpper) {
  return (char)toupper((unsigned char)value) == expectedUpper;
}

static void sendLx200SetAck(bool success) {
  // Some clients expect set-command acknowledgements to be terminated.
  Serial.print(success ? '1' : '0');
  Serial.print('#');
}

// Converts RA degrees to LX200 string format HH:MM:SS.
static void formatRaLx200(float raDeg, char* out, size_t outSize) {
  float wrapped = fmodf(raDeg, 360.0f);
  if (wrapped < 0.0f) {
    wrapped += 360.0f;
  }

  int totalSeconds = (int)(wrapped * 240.0f + 0.5f);
  totalSeconds %= (24 * 3600);

  int hh = totalSeconds / 3600;
  int mm = (totalSeconds % 3600) / 60;
  int ss = totalSeconds % 60;
  snprintf(out, outSize, "%02d:%02d:%02d", hh, mm, ss);
}

// Converts Dec degrees to LX200 string format sDD*MM:SS.
static void formatDecLx200(float decDeg, char* out, size_t outSize) {
  float clamped = decDeg;
  if (clamped > 90.0f) {
    clamped = 90.0f;
  }
  if (clamped < -90.0f) {
    clamped = -90.0f;
  }

  char sign = (clamped < 0.0f) ? '-' : '+';
  float absDec = fabsf(clamped);
  int totalSeconds = (int)(absDec * 3600.0f + 0.5f);

  int dd = totalSeconds / 3600;
  int mm = (totalSeconds % 3600) / 60;
  int ss = totalSeconds % 60;
  snprintf(out, outSize, "%c%02d*%02d:%02d", sign, dd, mm, ss);
}

// Parses HH:MM:SS and writes RA in degrees (0..360).
static bool parseRaLx200(const char* text, float* outRaDeg) {
  text = skipSpaces(text);

  int hh = 0;
  int mm = 0;
  int ss = 0;

  if (sscanf(text, "%d:%d:%d", &hh, &mm, &ss) == 3) {
    // HH:MM:SS supported.
  } else {
    float mmFloat = 0.0f;
    if (sscanf(text, "%d:%f", &hh, &mmFloat) == 2) {
      // HH:MM.M supported.
      if (mmFloat < 0.0f || mmFloat >= 60.0f) {
        return false;
      }
      mm = (int)mmFloat;
      ss = (int)(((mmFloat - (float)mm) * 60.0f) + 0.5f);
    } else if (sscanf(text, "%d:%d", &hh, &mm) == 2) {
      // HH:MM supported.
      ss = 0;
    } else {
      return false;
    }
  }

  if (ss >= 60) {
    ss -= 60;
    mm += 1;
  }
  if (mm >= 60) {
    mm -= 60;
    hh += 1;
  }
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
    return false;
  }

  int totalSeconds = hh * 3600 + mm * 60 + ss;
  *outRaDeg = ((float)totalSeconds / 86400.0f) * 360.0f;
  return true;
}

// Parses sDD*MM:SS and writes Dec in degrees (-90..+90).
static bool parseDecLx200(const char* text, float* outDecDeg) {
  text = skipSpaces(text);

  char sign = '+';
  if (*text == '+' || *text == '-') {
    sign = *text;
    ++text;
  }

  char buf[24];
  size_t len = 0;
  while (text[len] != '\0' && len < (sizeof(buf) - 1)) {
    char ch = text[len];
    if (ch == (char)223) {
      ch = '*';
    }
    buf[len] = ch;
    ++len;
  }
  buf[len] = '\0';

  char* p = buf;
  char* end = nullptr;
  long dd = strtol(p, &end, 10);
  if (end == p) {
    return false;
  }

  if (*end != '*' && *end != ':') {
    return false;
  }
  p = end + 1;

  long mm = strtol(p, &end, 10);
  if (end == p) {
    return false;
  }

  long ss = 0;
  if (*end == ':') {
    p = end + 1;
    ss = strtol(p, &end, 10);
    if (end == p) {
      return false;
    }
  }

  if (*end != '\0') {
    return false;
  }

  if (dd < 0 || dd > 90 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
    return false;
  }

  float dec = (float)dd + ((float)mm / 60.0f) + ((float)ss / 3600.0f);
  if (sign == '-') {
    dec = -dec;
  }
  if (dec < -90.0f || dec > 90.0f) {
    return false;
  }

  *outDecDeg = dec;
  return true;
}

// Minimal Meade LX200 serial command support for idle star-map center.
static void processLx200Command(const char* cmd) {
  cmd = skipSpaces(cmd);

  size_t cmdLen = strlen(cmd);
  while (cmdLen > 0 && (cmd[cmdLen - 1] == ' ' || cmd[cmdLen - 1] == '\t')) {
    --cmdLen;
  }

  char normalizedCmd[40];
  if (cmdLen >= sizeof(normalizedCmd)) {
    cmdLen = sizeof(normalizedCmd) - 1;
  }
  memcpy(normalizedCmd, cmd, cmdLen);
  normalizedCmd[cmdLen] = '\0';
  cmd = normalizedCmd;

  if (charEqIgnoreCase(cmd[0], 'G') && charEqIgnoreCase(cmd[1], 'R') && cmd[2] == '\0') {
    char raBuf[16];
    formatRaLx200(idleMapCenterRaDeg, raBuf, sizeof(raBuf));
    Serial.print(raBuf);
    Serial.print('#');
    return;
  }

  if (charEqIgnoreCase(cmd[0], 'G') && charEqIgnoreCase(cmd[1], 'D') && cmd[2] == '\0') {
    char decBuf[16];
    formatDecLx200(idleMapCenterDecDeg, decBuf, sizeof(decBuf));
    Serial.print(decBuf);
    Serial.print('#');
    return;
  }

  if (charEqIgnoreCase(cmd[0], 'S') && charEqIgnoreCase(cmd[1], 'R')) {
    float parsedRa = 0.0f;
    if (parseRaLx200(skipSpaces(cmd + 2), &parsedRa)) {
      idleMapCenterRaDeg = parsedRa;
      sendLx200SetAck(true);
    } else {
      sendLx200SetAck(false);
    }
    return;
  }

  if (charEqIgnoreCase(cmd[0], 'S') && charEqIgnoreCase(cmd[1], 'D')) {
    float parsedDec = 0.0f;
    if (parseDecLx200(skipSpaces(cmd + 2), &parsedDec)) {
      idleMapCenterDecDeg = parsedDec;
      sendLx200SetAck(true);
    } else {
      sendLx200SetAck(false);
    }
    return;
  }

  // Return explicit failure for unsupported set-like commands.
  if (charEqIgnoreCase(cmd[0], 'S')) {
    sendLx200SetAck(false);
  }
}

// Reads LX200 commands from Serial in :COMMAND# framing.
void handleSerialLx200() {
  static char cmdBuffer[40];
  static size_t cmdLen = 0;
  static bool inFrame = false;

  while (Serial.available() > 0) {
    char ch = (char)Serial.read();

    if (!inFrame && ch == ':') {
      inFrame = true;
      cmdLen = 0;
      continue;
    }

    if (!inFrame) {
      continue;
    }

    if (ch == '#') {
      cmdBuffer[cmdLen] = '\0';
      processLx200Command(cmdBuffer);
      inFrame = false;
      cmdLen = 0;
      continue;
    }

    if (cmdLen < (sizeof(cmdBuffer) - 1)) {
      cmdBuffer[cmdLen++] = ch;
    } else {
      inFrame = false;
      cmdLen = 0;
    }
  }
}
