// protocol.cpp — device-side handling of inbound messages (called from link).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "protocol.h"        // shared/
#include "state.h"
#include "hal/rtc.h"
#include "hal/link.h"

namespace {
void applySetTime(ClockState& clk, const proto::SetTimePayload& p) {
  clk.year = p.year; clk.month = p.month; clk.day = p.day;
  clk.hour = p.hour; clk.minute = p.minute; clk.second = p.second;
  hal::rtc::write(clk);      // host already applied TZ/DST -> straight to RTC
}
} // namespace

// dispatch(msgId, payload, len, dev, clk) is invoked by link::poll once a full,
// CRC-valid frame is assembled.  TODO(P1/P2): SetMode, PushList, Banner, SetHz...
void dispatch(uint8_t id, const uint8_t* payload, uint8_t len,
              DeviceState& dev, ClockState& clk) {
  (void)dev;
  switch (static_cast<proto::Msg>(id)) {
    case proto::Msg::SetTime:
      // Ignore a truncated frame rather than reading past the payload.
      if (len < sizeof(proto::SetTimePayload)) break;
      applySetTime(clk, *reinterpret_cast<const proto::SetTimePayload*>(payload));
      break;
    case proto::Msg::Ping:
      // Answering keeps the link's liveness measurable from both ends.
      hal::link::send(static_cast<uint8_t>(proto::Msg::Pong), nullptr, 0);
      break;
    default: break;  // TODO(P2): SetMode, PushList, Banner, SetBrightness, SetHz
  }
}
