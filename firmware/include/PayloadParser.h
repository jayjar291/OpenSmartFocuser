// PayloadParser.h
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

struct ParsedArgs {
  enum Type : uint8_t { Empty, Int32, Float, String };

  struct Arg {
    Type type = Empty;
    union {
      int32_t i32;
      float f32;
      const char* str; // points into payload buffer
    } v;
  };

  static const uint8_t kMaxArgs = 5;
  Arg args[kMaxArgs];
  uint8_t count = 0;
  bool truncated = false;

  const Arg* at(uint8_t i) const { return (i < count) ? &args[i] : nullptr; }
};

struct PayloadParserFixed {
  static ParsedArgs parseInPlace(char* payload) {
    ParsedArgs out;
    if (!payload) return out;

    char* p = payload;

    while (true) {
      if (out.count >= ParsedArgs::kMaxArgs) {
        out.truncated = true;
        break;
      }

      char* start = p;

      while (*p && *p != ',') ++p;

      bool atComma = (*p == ',');
      if (atComma) {
        *p = '\0';
        ++p;
      }

      start = ltrim(start);
      rtrim(start);

      out.args[out.count++] = parseToken(start);

      if (!atComma) break;
    }

    return out;
  }

  static bool asInt32(const ParsedArgs::Arg& a, int32_t& out) {
    if (a.type != ParsedArgs::Int32) return false;
    out = a.v.i32;
    return true;
  }

  static bool asFloat(const ParsedArgs::Arg& a, float& out) {
    if (a.type != ParsedArgs::Float) return false;
    out = a.v.f32;
    return true;
  }

  static bool asString(const ParsedArgs::Arg& a, const char*& out) {
    if (a.type == ParsedArgs::Empty) { out = ""; return true; }
    if (a.type != ParsedArgs::String) return false;
    out = a.v.str;
    return true;
  }

private:
  static char* ltrim(char* s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
  }

  static void rtrim(char* s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
      s[n - 1] = '\0';
      --n;
    }
  }

  static ParsedArgs::Arg parseToken(const char* s) {
    ParsedArgs::Arg a;

    if (!s || *s == '\0') {
      a.type = ParsedArgs::Empty;
      return a;
    }

    // int32
    errno = 0;
    char* end = nullptr;
    long iv = strtol(s, &end, 10);
    if (errno == 0 && end && *end == '\0' && iv >= INT32_MIN && iv <= INT32_MAX) {
      a.type = ParsedArgs::Int32;
      a.v.i32 = (int32_t)iv;
      return a;
    }

    // float
    errno = 0;
    end = nullptr;
    float fv = strtof(s, &end);
    if (errno == 0 && end && *end == '\0') {
      a.type = ParsedArgs::Float;
      a.v.f32 = fv;
      return a;
    }

    // string
    a.type = ParsedArgs::String;
    a.v.str = s;
    return a;
  }
};