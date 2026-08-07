// protocol.h — wire protocol shared by the Teensy client and the ESP32 bridge.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Framing: [START=0x7E][id:u8][len:u8][payload:len][crc8]
// crc8 is over id+len+payload. Keep this file the single source of truth;
// both platformio.ini files add `-I ../shared`.
#pragma once
#include <stdint.h>

namespace proto {

static constexpr uint8_t START = 0x7E;
static constexpr uint8_t MAX_PAYLOAD = 240;

enum class Msg : uint8_t {
  // host -> device
  SetTime        = 0x01,   // local (TZ/DST-applied) time -> RTC
  SetMode        = 0x02,   // face id | pushed
  PushList       = 0x03,   // arbitrary draw list, retained
  Banner         = 0x04,   // text + duration_ms + priority
  SetBrightness  = 0x05,
  SetHz          = 0x06,   // 50 | 60
  Ping           = 0x07,
  // device -> host
  Hello          = 0x81,   // fw version, caps, panel size
  Pong           = 0x82,
  EventEncoder   = 0x83,   // signed delta
  EventButton    = 0x84,   // press | long
  Status         = 0x85,   // uptime, rtc ok + last-set age, mode, frame us
};

inline uint8_t crc8(const uint8_t* p, uint16_t n) {
  uint8_t c = 0x00;
  while (n--) {
    c ^= *p++;
    for (uint8_t i = 0; i < 8; ++i)
      c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

// SetTime payload (local time; host already applied timezone + DST).
struct __attribute__((packed)) SetTimePayload {
  uint8_t year;   // 0-99 (20xx)
  uint8_t month;  // 1-12
  uint8_t day;    // 1-31
  uint8_t hour;   // 0-23
  uint8_t minute; // 0-59
  uint8_t second; // 0-59
};

} // namespace proto
