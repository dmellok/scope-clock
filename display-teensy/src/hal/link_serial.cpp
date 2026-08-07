// link_serial.cpp — framed serial to the ESP32 bridge over Serial1 (pins 0/1).
// Non-blocking assembler: feed bytes, dispatch on a complete CRC-valid frame.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal/link.h"
#include "protocol.h"     // shared/
#include "state.h"
#include <Arduino.h>

// defined in protocol.cpp
void dispatch(uint8_t id, const uint8_t* payload, uint8_t len,
              DeviceState& dev, ClockState& clk);

#define BRIDGE Serial1

namespace {
enum class RxState { Start, Id, Len, Payload, Crc };
RxState st = RxState::Start;
uint8_t rid, rlen, ridx, rcrc, buf[proto::MAX_PAYLOAD];

// A host that has gone quiet should not look present forever. Any valid frame
// counts as a heartbeat; Ping/Pong just guarantees traffic when idle.
constexpr uint32_t kHostTimeoutMs = 5000;
uint32_t lastFrameMs = 0;
bool     everHeard   = false;
}

namespace hal { namespace link {

void init() { BRIDGE.begin(115200); }

void send(uint8_t id, const uint8_t* p, uint8_t len) {
  BRIDGE.write(proto::START);
  BRIDGE.write(id);
  BRIDGE.write(len);
  if (len) BRIDGE.write(p, len);
  BRIDGE.write(proto::frameCrc(id, len, p));
}

void sendHello() {
  const uint8_t caps[] = { 1 /*fw major*/, 0 /*fw minor*/ };
  send(static_cast<uint8_t>(proto::Msg::Hello), caps, sizeof(caps));
}

void poll(DeviceState& dev, ClockState& clk) {
  while (BRIDGE.available()) {           // bounded by bytes actually buffered
    const uint8_t b = BRIDGE.read();
    switch (st) {
      case RxState::Start:
        if (b == proto::START) st = RxState::Id;
        break;

      case RxState::Id:
        rid  = b;
        rcrc = proto::crc8_update(0x00, b);
        st   = RxState::Len;
        break;

      case RxState::Len:
        // Length is attacker- and noise-controlled at this point, and buf only
        // holds MAX_PAYLOAD. A corrupt byte here would otherwise run straight
        // off the end of it.
        if (b > proto::MAX_PAYLOAD) { st = RxState::Start; break; }
        rlen = b;
        rcrc = proto::crc8_update(rcrc, b);
        ridx = 0;
        st   = rlen ? RxState::Payload : RxState::Crc;
        break;

      case RxState::Payload:
        buf[ridx++] = b;
        rcrc = proto::crc8_update(rcrc, b);
        if (ridx >= rlen) st = RxState::Crc;
        break;

      case RxState::Crc:
        if (b == rcrc) {                 // silently drop anything that fails
          lastFrameMs = millis();
          everHeard   = true;
          dispatch(rid, buf, rlen, dev, clk);
        }
        st = RxState::Start;
        break;
    }
  }
  dev.hostPresent = everHeard && (millis() - lastFrameMs) < kHostTimeoutMs;
}

}}
