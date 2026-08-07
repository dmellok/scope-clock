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
uint8_t rid, rlen, ridx, buf[proto::MAX_PAYLOAD];
}

namespace hal { namespace link {

void init() { BRIDGE.begin(115200); }

void send(uint8_t id, const uint8_t* p, uint8_t len) {
  uint8_t hdr[2] = { id, len };
  BRIDGE.write(proto::START);
  BRIDGE.write(hdr, 2);
  if (len) BRIDGE.write(p, len);
  // crc over id+len+payload
  uint8_t c = proto::crc8(hdr, 2);
  if (len) c ^= proto::crc8(p, len);   // (skeleton: replace with contiguous crc)
  BRIDGE.write(c);
}

void sendHello() {
  const uint8_t caps[] = { 1 /*fw major*/, 0 /*fw minor*/ };
  send(static_cast<uint8_t>(proto::Msg::Hello), caps, sizeof(caps));
}

void poll(DeviceState& dev, ClockState& clk) {
  while (BRIDGE.available()) {           // bounded by bytes actually buffered
    uint8_t b = BRIDGE.read();
    switch (st) {
      case RxState::Start:   if (b == proto::START) st = RxState::Id;        break;
      case RxState::Id:      rid = b; st = RxState::Len;                      break;
      case RxState::Len:     rlen = b; ridx = 0;
                             st = rlen ? RxState::Payload : RxState::Crc;     break;
      case RxState::Payload: buf[ridx++] = b; if (ridx >= rlen) st = RxState::Crc; break;
      case RxState::Crc:
        // TODO: verify crc == b before dispatch
        dispatch(rid, buf, rlen, dev, clk);
        st = RxState::Start;
        break;
    }
  }
  dev.hostPresent = true;  // TODO: track via Ping/Pong timeout
}

}}
