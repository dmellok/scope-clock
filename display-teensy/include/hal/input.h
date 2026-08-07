// hal/input.h — rotary encoder + button (+ centering pots). Emits events upward.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
struct DeviceState;
namespace hal { namespace input {
  void init();
  void poll(DeviceState& dev);   // debounces; sends EventEncoder/EventButton via link
}}
