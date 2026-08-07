// state.h — owned state structs. Replaces the ~100 loose globals of the original.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include "drawlist.h"

// The RTC holds LOCAL time (the host applies timezone + DST). The device never
// reasons about zones.
struct ClockState {
  int16_t year = 26, month = 1, day = 1, wday = 0;
  int16_t hour = 0, minute = 0, second = 0;
  bool    rtcPresent = false;
  bool    hr12 = true;
};

enum class Mode : uint8_t { Face, Pushed };

struct DeviceState {
  Mode      mode = Mode::Face;
  uint8_t   faceId = 0;
  uint8_t   brightness = 255;
  uint16_t  hz = 60;            // 50 or 60
  uint32_t  frameUs = 0;        // last render time; the budget is 1e6/hz, and
                                // exceeding it means the refresh is free-running
                                // slower than hz. Reported via Msg::Status.
  bool      hostPresent = false;

  DrawList  pushed;             // host-authored list when mode == Pushed
  // Backing store for pushed text. Item holds a const char*, and the link's
  // receive buffer is overwritten by the next frame, so the strings have to be
  // copied somewhere with the same lifetime as the list itself. A payload
  // cannot exceed MAX_PAYLOAD, so its strings cannot either.
  static constexpr uint16_t kArenaSize = 240;
  char      arena[kArenaSize] = {0};
  // banner overlay (auto-expires locally so a host stall can't strand it)
  bool      bannerActive = false;
  uint32_t  bannerUntilMs = 0;
  char      bannerText[64] = {0};
};
