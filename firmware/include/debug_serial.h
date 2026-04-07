#pragma once

#include <Arduino.h>
#include "config.h"

namespace DebugSerial {

inline void printFramed(const char* message) {
#if SERIAL_DEBUG_ENABLED
  Serial.print("!{");
  Serial.print(message);
  Serial.println("}*");
#else
  (void)message;
#endif
}

template <typename T>
inline void printFramedValue(const char* prefix, T value, const char* suffix) {
#if SERIAL_DEBUG_ENABLED
  Serial.print("!{");
  Serial.print(prefix);
  Serial.print(value);
  Serial.print(suffix);
  Serial.println("}*");
#else
  (void)prefix;
  (void)value;
  (void)suffix;
#endif
}

} // namespace DebugSerial
