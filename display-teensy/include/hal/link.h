// hal/link.h — framed link to the ESP32 bridge over the rear USB-A host jack
// (this MCU is the USB host, the AtomS3U is a CDC-ACM device).
// Everything here MUST be non-blocking — the loop is now also the clock.
// See link_usbhost.cpp for the two USBHost_t36 spin-waits it steers around.
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
  uint16_t silentSeconds();   // since anything was last received
}}
