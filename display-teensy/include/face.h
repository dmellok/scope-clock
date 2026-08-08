// face.h — a clock face renders a DrawList from the RTC time. Replaces the
// original `theClock == 0/1/2` magic-number dispatch.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
struct ClockState;
struct DrawList;
struct DeviceState;

namespace faces {
  using RenderFn = void(*)(const ClockState&, DrawList&);
  void      registerBuiltins();
  RenderFn  current(const DeviceState& dev);
  uint8_t   count();

  // Two-level navigation, so the two physical controls do different jobs:
  // the knob moves between kinds of face, the button changes the style within
  // the kind you are on. Each family remembers the variant you last chose, so
  // knobbing away and back returns what you left rather than resetting.
  uint8_t   nextFamily(uint8_t faceId, int dir);
  uint8_t   nextVariant(uint8_t faceId);
}
