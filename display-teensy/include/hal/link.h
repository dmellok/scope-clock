// hal/link.h — framed serial to the ESP32 bridge (Serial1, pins 0/1).
// Everything here MUST be non-blocking — the loop is now also the clock.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
struct DeviceState;
struct ClockState;
namespace hal { namespace link {
  void init();
  void poll(DeviceState& dev, ClockState& clk);   // ingest host frames -> mutate state
  void sendHello();
  void send(uint8_t msgId, const uint8_t* payload, uint8_t len);
}}
