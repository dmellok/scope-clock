// protocol.cpp — device-side handling of inbound messages (called from link).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "protocol.h"        // shared/
#include "state.h"
#include "drawlist.h"
#include "hal/rtc.h"
#include "hal/link.h"
#include <Arduino.h>

namespace {

void applySetTime(ClockState& clk, const proto::SetTimePayload& p) {
  clk.year = p.year; clk.month = p.month; clk.day = p.day;
  clk.hour = p.hour; clk.minute = p.minute; clk.second = p.second;
  hal::rtc::write(clk);      // host already applied TZ/DST -> straight to RTC
}

void applyBanner(DeviceState& dev, const uint8_t* p, uint8_t len) {
  if (len < 3) return;
  const uint16_t ms = (uint16_t)(p[0] | (p[1] << 8));
  // p[2] is priority — reserved until there is more than one banner source.
  if (ms == 0) { dev.bannerActive = false; return; }

  const uint8_t textLen = (uint8_t)(len - 3);
  const uint8_t n = textLen < sizeof(dev.bannerText) - 1
                  ? textLen : (uint8_t)(sizeof(dev.bannerText) - 1);
  for (uint8_t i = 0; i < n; ++i) dev.bannerText[i] = (char)p[3 + i];
  dev.bannerText[n] = '\0';

  // Deliberately millis()-relative and owned by the device: if the host stops
  // talking mid-banner, it still clears itself instead of sticking forever.
  dev.bannerUntilMs = millis() + ms;
  dev.bannerActive  = true;
}

} // namespace

// dispatch(msgId, payload, len, dev, clk) is invoked by link::poll once a full,
// CRC-valid frame is assembled.
void dispatch(uint8_t id, const uint8_t* payload, uint8_t len,
              DeviceState& dev, ClockState& clk) {
  switch (static_cast<proto::Msg>(id)) {
    case proto::Msg::SetTime:
      // Ignore a truncated frame rather than reading past the payload.
      if (len < sizeof(proto::SetTimePayload)) break;
      applySetTime(clk, *reinterpret_cast<const proto::SetTimePayload*>(payload));
      break;

    case proto::Msg::SetMode:
      if (len < 1) break;
      dev.mode = payload[0] ? Mode::Pushed : Mode::Face;
      if (len >= 2) dev.faceId = payload[1];
      break;

    case proto::Msg::PushList:
      // Switch to it on success, but do not strand the display on a bad frame:
      // a list that fails to decode leaves the device on its local face.
      if (decodePushList(payload, len, dev.pushed,
                         dev.arena, DeviceState::kArenaSize)) {
        dev.mode = Mode::Pushed;
      } else {
        dev.mode = Mode::Face;
      }
      break;

    case proto::Msg::Banner:
      applyBanner(dev, payload, len);
      break;

    case proto::Msg::SetBrightness:
      if (len < 1) break;
      dev.brightness = payload[0];
      break;

    case proto::Msg::SetHz:
      if (len < 1) break;
      if (payload[0] == 50 || payload[0] == 60) dev.hz = payload[0];
      break;

    case proto::Msg::Ping:
      // Answering keeps the link's liveness measurable from both ends.
      hal::link::send(static_cast<uint8_t>(proto::Msg::Pong), nullptr, 0);
      break;

    default: break;
  }
}
