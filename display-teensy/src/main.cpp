// main.cpp — the fixed pipeline. No stage may block: the loop is the refresh.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <Arduino.h>
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"
#include "face.h"
#include "hal/dac.h"
#include "hal/rtc.h"
#include "hal/input.h"
#include "hal/link.h"

static ClockState  clk;
static DeviceState dev;
static DrawList    frame;
static uint32_t    lastMicros = 0;

static void frameSync(uint16_t hz) {
  const uint32_t period = 1000000UL / hz;
  while (micros() - lastMicros < period) { /* spin briefly */ }
  lastMicros = micros();
}

void setup() {
  Serial.begin(115200);       // USB debug (+ audio, per USB type)
  hal::dac::init();
  vec::init();                // sin/cos tables, beam parked and blanked
  hal::rtc::init();
  hal::input::init();
  hal::link::init();          // Serial1 <-> ESP32 bridge
  faces::registerBuiltins();
  clk.rtcPresent = hal::rtc::present();
  hal::link::sendHello();
}

void loop() {
  hal::link::poll(dev, clk);     // 1. host commands in (non-blocking)
  hal::input::poll(dev);         // 2. encoder/button out

  frame.clear();                 // 3. compose the current frame
  switch (dev.mode) {
    case Mode::Face:
      hal::rtc::read(clk);
      vec::updateScreenSaver(clk.hour);
      faces::current(dev)(clk, frame);
      break;
    case Mode::Pushed:
      frame = dev.pushed;        // host-authored
      break;
  }
  // TODO(banner): if dev.bannerActive && millis() < bannerUntilMs, overlay text;
  //               else clear bannerActive. Times out locally.

  txt::centerLines(frame);       // 4. resolve text positions (no-op if placed)
  vec::setBrightness(dev.brightness);
  const uint32_t drawStart = micros();
  vec::renderFrame(frame);       // 5. draw to the CRT (the refresh)
  dev.frameUs = micros() - drawStart;

  frameSync(dev.hz);             // 6. hold 50/60 Hz cadence
}

