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
}
