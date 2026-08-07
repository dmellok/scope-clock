// link_usbhost.cpp — framed link to the ESP32 bridge over the rear USB-A host
// jack. The AtomS3U is a CDC-ACM device; this MCU is the host. No wiring, which
// is the whole point of the zero-hardware-mod route.
//
// Two blocking traps in USBHost_t36 that this file exists to route around:
//
//   1. USBSerialBase::begin() ends in `while (pending_control && em < 5000)
//      yield();` — up to five seconds of spin, and yield() does not run our
//      render loop, so the CRT would sit frozen on a static image. We never
//      call it. For CDC-ACM the driver's claim() has already queued SET_
//      LINE_CODING (115200 8N1) and SET_CONTROL_LINE_STATE (DTR|RTS) during
//      enumeration, entirely interrupt-driven, and baud is meaningless to a
//      native-USB device anyway. begin() would only re-send those two.
//
//   2. USBSerialBase::write() spins unbounded when its TX buffer is full:
//      `while (txtail == head) { /* wait... */ }`. A bridge that stops
//      draining would hang the refresh forever. Every send is gated on
//      availableForWrite() and dropped if it will not fit — losing an encoder
//      event is always better than stalling the beam.
//
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal/link.h"
#include "protocol.h"     // shared/
#include "state.h"
#include <Arduino.h>
#include <USBHost_t36.h>

// defined in protocol.cpp
void dispatch(uint8_t id, const uint8_t* payload, uint8_t len,
              DeviceState& dev, ClockState& clk);

namespace {
USBHost   myusb;
USBHub    hub1(myusb);      // tolerate the bridge being behind a hub
USBSerial userial(myusb);   // CDC-ACM; 64-byte endpoints, so not BigBuffer

enum class RxState { Start, Id, Len, Payload, Crc };
RxState st = RxState::Start;
uint8_t rid, rlen, ridx, rcrc, buf[proto::MAX_PAYLOAD];

// Frame overhead on the wire: START + id + len + crc.
constexpr int kFrameOverhead = 4;
// Cap bytes drained per refresh so a flood cannot stretch a frame. One full
// maximum-size frame still gets through per pass.
constexpr int kRxBudget = 512;

// The bridge pings every few seconds, so silence well past that means it has
// stopped talking even though the cable is still in.
constexpr uint32_t kHostTimeoutMs = 15000;
uint32_t lastFrameMs = 0;
bool     everHeard   = false;
}

namespace hal { namespace link {

void init() {
  myusb.begin();            // starts the host stack; enumeration is async
}

void send(uint8_t id, const uint8_t* p, uint8_t len) {
  if (!userial) return;                                   // nothing plugged in
  if (userial.availableForWrite() < (int)(len + kFrameOverhead)) return;  // drop
  userial.write(proto::START);
  userial.write(id);
  userial.write(len);
  if (len) userial.write(p, len);
  userial.write(proto::frameCrc(id, len, p));
}

void sendHello() {
  const uint8_t caps[] = { 1 /*fw major*/, 0 /*fw minor*/ };
  send(static_cast<uint8_t>(proto::Msg::Hello), caps, sizeof(caps));
}

void poll(DeviceState& dev, ClockState& clk) {
  myusb.Task();             // drives enumeration/callbacks; never blocks

  // Greet the bridge once per connection, not once per boot — on USB the
  // device can arrive long after we did, and can come and go.
  static bool wasUp = false;
  const bool isUp = (bool)userial;
  if (isUp && !wasUp) {
    st = RxState::Start;    // a fresh cable means a fresh byte stream
    sendHello();
  }
  if (!isUp) everHeard = false;
  wasUp = isUp;

  int budget = kRxBudget;
  while (isUp && userial.available() && budget-- > 0) {
    const uint8_t b = userial.read();
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
        // Length is noise-controlled at this point and buf only holds
        // MAX_PAYLOAD; without this a corrupt byte runs off the end of it.
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

  dev.hostPresent = isUp && everHeard && (millis() - lastFrameMs) < kHostTimeoutMs;
}

}}
