// protocol.cpp — device-side handling of inbound messages (called from link).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "protocol.h"        // shared/
#include "state.h"
#include "drawlist.h"
#include "hal/rtc.h"
#include "hal/link.h"
#include "nowplaying.h"
#include "gauges.h"
#include "vector.h"
#include "faces_impl.h"
#include "hostdata.h"
#include <Arduino.h>

namespace {

void applySetTime(ClockState& clk, const proto::SetTimePayload& p) {
  clk.year = p.year; clk.month = p.month; clk.day = p.day;
  clk.hour = p.hour; clk.minute = p.minute; clk.second = p.second;
  hal::rtc::write(clk);      // host already applied TZ/DST -> straight to RTC
  clk.setAtMs = millis();
  clk.everSet = true;
}

// Copy into a fixed buffer, always terminated, never past the end.
void copyField(char* dst, uint8_t cap, const uint8_t* src, uint8_t n) {
  const uint8_t k = n < (uint8_t)(cap - 1) ? n : (uint8_t)(cap - 1);
  for (uint8_t i = 0; i < k; ++i) dst[i] = (char)src[i];
  dst[k] = '\0';
}

// A Banner is a Notify with no title in the bottom strip, so there is one
// renderer and one expiry rather than two of each.
void applyBanner(DeviceState& dev, const uint8_t* p, uint8_t len) {
  if (len < 3) return;
  const uint16_t ms = (uint16_t)(p[0] | (p[1] << 8));
  // p[2] is priority — reserved until there is more than one banner source.
  if (ms == 0) { dev.noteActive = false; return; }

  dev.noteTitle[0] = '\0';
  dev.notePlace = 0;
  dev.noteSolo = false;        // a plain banner never blanks the face
  copyField(dev.noteBody, sizeof dev.noteBody, p + 3, (uint8_t)(len - 3));

  // Deliberately millis()-relative and owned by the device: if the host stops
  // talking mid-banner, it still comes down on time.
  dev.noteActive = true;
  dev.noteUntilMs = millis() + ms;
}

// Notify [ms:u16][place:u8][titleLen:u8][title][body]
void applyNotify(DeviceState& dev, const uint8_t* p, uint8_t len) {
  if (len < 4) return;
  const uint16_t ms = (uint16_t)(p[0] | (p[1] << 8));
  if (ms == 0) { dev.noteActive = false; return; }

  // The high bit is the solo flag; the placement is what is left.
  const bool solo = (p[2] & 0x80) != 0;
  const uint8_t raw = (uint8_t)(p[2] & 0x7F);
  const uint8_t place = raw > 2 ? 0 : raw;
  uint8_t tlen = p[3];
  // A titleLen that overruns the payload is the one hostile input here; clamp
  // it rather than reading past the frame.
  if ((uint16_t)tlen + 4 > len) tlen = (uint8_t)(len - 4);

  dev.notePlace = place;
  dev.noteSolo = solo;
  copyField(dev.noteTitle, sizeof dev.noteTitle, p + 4, tlen);
  copyField(dev.noteBody,  sizeof dev.noteBody,  p + 4 + tlen,
            (uint8_t)(len - 4 - tlen));
  dev.noteActive = true;
  dev.noteUntilMs = millis() + ms;
}

// Staging for a draw list too big for one frame. Lives here rather than in
// DeviceState because it is protocol machinery, not something the renderer or
// any face has any business seeing.
//
// 192 items of 9 bytes is the worst realistic case for line art, plus the
// count byte. A transfer that would overflow this is abandoned rather than
// truncated: a half-decoded picture is worse than none, and Commit will find
// the staged bytes malformed anyway.
constexpr uint16_t kStageCap = 2048;
uint8_t  stage[kStageCap];
uint16_t stageLen = 0;
bool     stageOk  = false;      // false once a transfer has overflowed

} // namespace

// Periodic telemetry upward. This is the permanent home for the numbers that
// otherwise get added and removed as temporary printf debugging.
void sendStatus(const DeviceState& dev, const ClockState& clk) {
  proto::StatusPayload s{};
  s.uptimeS    = millis() / 1000UL;
  s.frameUs    = dev.frameUs;
  s.hz         = dev.hz;
  s.setAgeS    = clk.everSet ? (uint16_t)((millis() - clk.setAtMs) / 1000UL) : 0xFFFF;
  s.mode       = (dev.mode == Mode::Pushed) ? 1 : 0;
  s.faceId     = dev.faceId;
  s.brightness = dev.brightness;
  s.rtcOk      = clk.rtcPresent ? 1 : 0;
  s.linkSilentS = hal::link::silentSeconds();
  hal::link::send(static_cast<uint8_t>(proto::Msg::Status),
                  reinterpret_cast<const uint8_t*>(&s), sizeof(s));
}

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
      // Anything unrecognised falls back to the local face rather than being
      // ignored: a host that means "stop whatever you are doing" should always
      // get a clock, never a stuck mode.
      switch (payload[0]) {
        case 1:  dev.mode = Mode::Pushed; break;
        case 2:  dev.mode = Mode::Audio;  break;
        default: dev.mode = Mode::Face;   break;
      }
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

    case proto::Msg::PushBegin:
      stageLen = 0;
      stageOk  = true;
      break;

    case proto::Msg::PushChunk:
      if (!stageOk) break;                       // already given up on this one
      if (stageLen + len > kStageCap) { stageOk = false; break; }
      for (uint8_t i = 0; i < len; ++i) stage[stageLen++] = payload[i];
      break;

    case proto::Msg::PushCommit:
      // Decoded by exactly the same function as a single-frame PushList, so
      // there is one parser and one set of bounds checks, not two.
      if (stageOk && decodePushList(stage, stageLen, dev.pushed,
                                    dev.arena, DeviceState::kArenaSize)) {
        dev.mode = Mode::Pushed;
      } else {
        dev.mode = Mode::Face;
      }
      stageLen = 0;
      stageOk  = false;
      break;

    case proto::Msg::Banner:
      applyBanner(dev, payload, len);
      break;

    case proto::Msg::Notify:
      applyNotify(dev, payload, len);
      break;

    case proto::Msg::SetNowPlaying:
      np::set(payload, len);
      break;

    case proto::Msg::SetWeather:
      host::setWeather(payload, len);
      break;

    case proto::Msg::SetTicker:
      host::setTicker(payload, len);
      break;

    case proto::Msg::SetElement:
      if (len >= 1) faces::impl::setAtomZ(payload[0]);
      break;

    case proto::Msg::SetWobble:
      if (len >= 1) vec::setWobble(payload[0] != 0);
      break;

    case proto::Msg::SetGauges:
      gauge::set(payload, len);
      break;

    case proto::Msg::SetScales: {
      if (len < 1) break;
      const uint8_t n = payload[0] < len - 1 ? payload[0] : (uint8_t)(len - 1);
      for (uint8_t i = 0; i < n && i < DeviceState::kMaxFaces; ++i) {
        const uint8_t v = payload[1 + i];
        // Ignore nonsense rather than let the host shrink a face to nothing.
        if (v >= 40 && v <= 250) dev.faceScale[i] = v;
      }
      break;
    }

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
