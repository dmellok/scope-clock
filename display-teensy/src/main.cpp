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

// The banner sits in the strip below the numeral ring, which reaches about
// -1100 in a field that runs to -1250. Added AFTER txt::centerLines, because
// it carries a real position and any positioned text item opts the whole list
// out of centring — running it first would break the digital face.
//
// Expiry is checked here, on the device, against millis(): a host that dies
// mid-banner cannot leave one burnt onto the screen.
static void overlayBanner(DeviceState& d, DrawList& list) {
  // Placement is a trade-off with no clean answer. The gap between the numeral
  // ring (ink bottom -1100) and the edge of the active field (-1250) is 150
  // units, which a legible banner does not fit inside — and sitting flush
  // against -1250 means the tube's overscan, plus whatever the centring trim
  // is set to, quietly eats the bottom of the text.
  //
  // So it sits 60 units clear of the edge and is allowed to cross the lower
  // part of the VI numeral instead. It is an overlay and it expires; being
  // readable matters more than never touching the dial.
  constexpr int kBannerScale = 8;
  constexpr int kBannerY     = -1190;   // baseline; ink runs to -1030

  if (!d.bannerActive) return;
  if ((int32_t)(millis() - d.bannerUntilMs) >= 0) {   // wrap-safe comparison
    d.bannerActive = false;
    return;
  }
  const int w = txt::inkWidth(kBannerScale, d.bannerText);
  list.text(-w / 2, kBannerY, kBannerScale, d.bannerText);
}

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
  txt::centerLines(frame);       // 4. resolve text positions (no-op if placed)
  overlayBanner(dev, frame);     // 5. banner goes on top, already positioned

  vec::setBrightness(dev.brightness);
  const uint32_t drawStart = micros();
  vec::renderFrame(frame);       // 5. draw to the CRT (the refresh)
  dev.frameUs = micros() - drawStart;
  vec::tuneDwell(dev.frameUs, 1000000UL / dev.hz);

  frameSync(dev.hz);             // 6. hold 50/60 Hz cadence
}


