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
  // TEMPORARY: hold until the console is attached so the USB-host enumeration
  // trace is not lost before anyone is listening.
  while (!Serial && millis() < 15000) { /* wait for the console */ }
  hal::dac::init();
  vec::init();                // sin/cos tables, beam parked and blanked
  hal::rtc::init();
  hal::input::init();
  hal::link::init();          // Serial1 <-> ESP32 bridge
  faces::registerBuiltins();
  clk.rtcPresent = hal::rtc::present();

  // TEMPORARY: one-shot RTC set from build-time flags, to undo the deliberate
  // skew used to prove SET_TIME. Remove once the bridge disciplines the RTC.
#ifdef RTC_SET_Y
  { ClockState t; t.year = RTC_SET_Y; t.month = RTC_SET_MO; t.day = RTC_SET_D;
    t.hour = RTC_SET_H; t.minute = RTC_SET_MI; t.second = RTC_SET_S;
    hal::rtc::write(t);
    Serial.printf("rtcset: 20%02d-%02d-%02d %02d:%02d:%02d\n",
                  t.year, t.month, t.day, t.hour, t.minute, t.second); }
#endif
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
  const uint32_t t0 = micros();
  vec::renderFrame(frame);       // 5. draw to the CRT (the refresh)
  const uint32_t drawUs = micros() - t0;

  // ---- TEMPORARY: link + RTC diagnostic, remove once P1 is signed off ----
  static uint32_t lastRep = 0;
  if (millis() - lastRep >= 1000) {
    lastRep = millis();
    Serial.printf("rtc=20%02d-%02d-%02d %02d:%02d:%02d  host=%d  face=%u  draw=%luus  budget=%luus\n",
                  clk.year, clk.month, clk.day, clk.hour, clk.minute, clk.second,
                  dev.hostPresent ? 1 : 0, dev.faceId,
                  (unsigned long)drawUs, (unsigned long)(1000000UL / dev.hz));
  }

  frameSync(dev.hz);             // 6. hold 50/60 Hz cadence
}
