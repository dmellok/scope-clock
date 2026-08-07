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
  bool      hostPresent = false;
  DrawList  pushed;            // host-authored list when mode == Pushed
  // banner overlay (auto-expires locally so a host stall can't strand it)
  bool      bannerActive = false;
  uint32_t  bannerUntilMs = 0;
  char      bannerText[64] = {0};
};
