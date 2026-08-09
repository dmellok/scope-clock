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
#include "debug.h"
#include "hal/link.h"
#include "protocol.h"     // shared/
#include "state.h"
#include "hal/dac.h"
#include <Arduino.h>
#include <USBHost_t36.h>

// defined in protocol.cpp
void dispatch(uint8_t id, const uint8_t* payload, uint8_t len,
              DeviceState& dev, ClockState& clk);

namespace {
USBHost myusb;
USBHub  hub1(myusb);        // tolerate the bridge being behind a hub

// The AtomS3U's USB Serial/JTAG peripheral (303A:1001) will not be claimed by
// a plain USBSerial, for one byte's worth of reason. USBHost_t36's generic
// composite-CDC path insists the CDC *Data* interface carry subclass 0:
//
//     if (descriptors[5] != 0xA) return false;  // class 0x0A, CDC data  — ok
//     if (descriptors[6] != 0)   return false;  // subclass             — we send 2
//
// Espressif's descriptor is 09 04 01 00 02 0A 02 00 00, i.e. subclass 2. The
// CDC spec (Table 19) says that field is unused on a Data Class interface, and
// macOS, Linux and Windows all ignore it — but this driver does not. The other
// CDC path is no help either: it is gated on bDeviceClass == 2, and this is an
// IAD composite that reports 239 (JTAG rides alongside the serial function).
//
// The peripheral's descriptors are burned into silicon, so the fix belongs on
// this side. USBHost_t36 provides the hook: naming a VID/PID with a forced
// sertype bypasses the descriptor sniffing entirely and takes the shared
// endpoint path, which also queues SET_LINE_CODING and asserts DTR/RTS — the
// latter being what makes the ESP32 side consider the port open at all.
constexpr uint16_t kBridgeVid = 0x303A;   // Espressif
constexpr uint16_t kBridgePid = 0x1001;   // USB JTAG/serial debug unit
USBSerial userial(myusb, kBridgeVid, kBridgePid, USBSerial::CDCACM, /*claim at interface*/ 0);

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

// Re-announce ourselves while the link is up but silent.
//
// The bridge's USB Serial/JTAG peripheral throws away everything written to it
// until it believes a host is listening, and it only comes to believe that on
// RECEIVING something (HWCDC::write calls flushTXBuffer — a drop — whenever
// isCDC_Connected() is false, and that flag is set from the RX and TX-complete
// interrupts). A soft reset clears it.
//
// An ESP32 soft reset does not drop the USB pull-up, so from this side nothing
// disconnects: no port change, no re-enumeration, no fresh claim. Sending Hello
// only on the connect transition therefore deadlocks after every OTA update —
// the bridge is discarding its frames waiting to hear from us, and we are
// staying quiet because as far as we know the link never dropped.
//
// Speaking first costs one small frame every few seconds and breaks the tie.
// It doubles as the time request, since the bridge answers Hello with SET_TIME.
// Silence has to be judged against the bridge's own heartbeat, not guessed at:
// a threshold shorter than its ping interval makes the link look dead in the
// gap between pings, so we re-announce, the bridge answers with SET_TIME, and
// the RTC gets rewritten over I2C every few seconds forever.
constexpr uint32_t kSilenceMs    = 8000;   // > the bridge's 5s ping
constexpr uint32_t kHelloRetryMs = 3000;   // how often to retry while silent
constexpr uint32_t kTimeAskMs    = 10000;  // how often to re-ask for the time
uint32_t lastHelloMs = 0;
}

namespace hal { namespace link {

// Restart budget, kept in memory that a SOFTWARE reset does not clear — SRAM
// holds its contents when power never drops, which is exactly the difference
// between the two kinds of reset here. That is what lets "restart and try
// again" be BOUNDED rather than a loop: the count survives the restart it
// causes, so the second attempt knows it is the second.
__attribute__((section(".noinit"))) uint32_t claimTries;
__attribute__((section(".noinit"))) uint32_t claimMagic;
constexpr uint32_t kClaimMagic = 0x5C10C4A1;
constexpr uint32_t kMaxClaimTries = 2;

void init() {
  // A power-on reset is a fresh start in every sense, and .noinit is undefined
  // after one, so the budget is reseeded there and only there. RCM_SRS0 bit 7
  // is POR; see dbg::resetCause().
  if (claimMagic != kClaimMagic || (RCM_SRS0 & 0x80)) {
    claimMagic = kClaimMagic;
    claimTries = 0;
  }
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

uint16_t silentSeconds() {
  if (!everHeard) return 0xFFFF;
  const uint32_t s = (millis() - lastFrameMs) / 1000UL;
  return s > 0xFFFE ? 0xFFFE : (uint16_t)s;
}

void sendHello() {
  const uint8_t caps[] = { 1 /*fw major*/, 0 /*fw minor*/ };
  send(static_cast<uint8_t>(proto::Msg::Hello), caps, sizeof(caps));
}

void poll(DeviceState& dev, ClockState& clk) {
  myusb.Task();             // drives enumeration/callbacks; never blocks

  // Why the link is down, from the side that can actually see it. The bridge
  // can only report that nothing arrived; only this end knows whether the host
  // port ever claimed a device. Costs nothing with no console attached, since
  // dbg::sayf drops everything when the port has no room, and it prints only
  // while the link is DOWN — a healthy clock says nothing at all.
  {
    static uint32_t lastSay = 0;
    if (!(bool)userial && millis() - lastSay > 1000) {
      lastSay = millis();
      dbg::sayf("link down: no device claimed, t=%lus try=%lu/%lu",
                (unsigned long)(millis() / 1000),
                (unsigned long)claimTries, (unsigned long)kMaxClaimTries);
    }
  }

  // Greet the bridge once per connection, not once per boot — on USB the
  // device can arrive long after we did, and can come and go.
  static bool wasUp = false;
  const bool isUp = (bool)userial;
  const uint32_t now = millis();
  if (isUp && !wasUp) {
    st = RxState::Start;    // a fresh cable means a fresh byte stream
    everHeard = false;
  }
  if (!isUp) everHeard = false;

  // Restart if the port disappears after having worked.
  //
  // USBHost_t36 does not re-claim a device that goes away: proven on hardware,
  // where after the bridge resets its USB peripheral the link only comes back
  // when this MCU restarts. That is also why a power cycle was the only known
  // cure — it is the one case where this side enumerates from a cold boot
  // rather than trying to re-enumerate.
  //
  // Cheap here: the RTC keeps the time and dac::init() blanks the beam before
  // anything is drawn, so a restart costs a blink.
  //
  // Retries rather than trying once: observed on hardware, the first attempt
  // can land before the far side has finished re-initialising, and the second
  // succeeds. It cannot become a reset loop on an absent bridge, because
  // everBeenUp is false after a restart if the port never comes up — so a
  // bridge that is simply unplugged costs exactly one restart, not a cycle.
  static bool     everBeenUp = false;
  static uint32_t downSince  = 0;
  if (isUp) { everBeenUp = true; downSince = 0; claimTries = 0; }
  else if (everBeenUp && !downSince) downSince = now;
  if (downSince && (now - downSince) > 25000 && now > 45000) {
    hal::dac::blank(true);          // never leave the beam parked and lit
    SCB_AIRCR = 0x05FA0004;         // system reset
  }

  // Never claimed anything at all, which is a different fault from losing a
  // device that was working. It is what a cold boot with the bridge already
  // plugged in can land in: USBHost_t36 does not pick up something that was
  // sitting there when its stack came up, and no amount of resetting the far
  // side's peripheral helps — proven the hard way, where only physically
  // replugging the AtomS3U recovered it. Restarting THIS end is the software
  // equivalent of that replug, since it re-enumerates the bus from scratch.
  //
  // Budgeted, not repeated: a clock with no bridge attached must not restart
  // forever, so it gets kMaxClaimTries per power cycle and then gives up and
  // runs as a standalone clock, which is a perfectly good thing to be.
  if (!everBeenUp && claimTries < kMaxClaimTries && now > 40000) {
    ++claimTries;
    dbg::sayf("link: nothing claimed in 40s, restarting (%lu/%lu)",
              (unsigned long)claimTries, (unsigned long)kMaxClaimTries);
    delay(20);                      // let that line reach the console
    hal::dac::blank(true);
    SCB_AIRCR = 0x05FA0004;
  }

  wasUp = isUp;

  // Keep announcing until the bridge actually answers, not just once on
  // connect — see kHelloRetryMs above for why once is not enough.
  //
  // Silence is not the only reason to ask. The bridge can be pinging away
  // quite happily while the SET_TIME it sent went into the bit bucket — its
  // CDC peripheral drops output until it has received something, so a reply
  // sent before we first spoke is simply gone. Then the link looks perfectly
  // healthy from here and we sit with an undisciplined RTC indefinitely.
  //
  // So also ask while we have no time at all. Hello doubles as the request,
  // and "I have not been set" is the honest trigger.
  if (isUp) {
    const bool silent   = !everHeard || (now - lastFrameMs) > kSilenceMs;
    const bool needTime = !clk.everSet;
    const uint32_t since = now - lastHelloMs;
    if ((silent && since > kHelloRetryMs) || (needTime && since > kTimeAskMs)) {
      lastHelloMs = now;
      sendHello();
    }
  }

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
