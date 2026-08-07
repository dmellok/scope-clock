// hal/rtc.h — DS3232 over I2C. Holds LOCAL time.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
struct ClockState;
namespace hal { namespace rtc {
  void init();
  bool present();
  void read(ClockState& s);
  void write(const ClockState& s);
}}
