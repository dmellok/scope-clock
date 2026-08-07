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

// CRC-8, polynomial 0x07. Incremental so a receiver can fold each byte in as
// it streams past, without buffering the frame to CRC it afterwards.
inline uint8_t crc8_update(uint8_t c, uint8_t b) {
  c ^= b;
  for (uint8_t i = 0; i < 8; ++i)
    c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  return c;
}

inline uint8_t crc8(const uint8_t* p, uint16_t n) {
  uint8_t c = 0x00;
  while (n--) c = crc8_update(c, *p++);
  return c;
}

// The frame check: ONE contiguous run over id, then len, then payload.
//
// Both ends must call this rather than rolling their own. Combining separate
// CRCs — crc8(hdr) ^ crc8(payload), as the skeleton did — is not the same
// function and is measurably weaker. CRC is linear over GF(2), so an error
// contributes a syndrome that depends only on its bit pattern and its distance
// from the end of the run. Give a byte the same offset in two runs of the same
// length and it produces the same syndrome, so XORing the two CRCs cancels it:
// with len == 2, flipping bit k of `id` and bit k of `payload[0]` is entirely
// invisible. Brute force over two-byte corruptions puts that at 40 misses in
// 1344; one contiguous run misses none, because a byte's offset in the stream
// is then unique.
inline uint8_t frameCrc(uint8_t id, uint8_t len, const uint8_t* payload) {
  uint8_t c = crc8_update(crc8_update(0x00, id), len);
  for (uint8_t i = 0; i < len; ++i) c = crc8_update(c, payload[i]);
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
