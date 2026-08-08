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
#include "hal/midi.h"
#include "hal/audio.h"
#include "hal/watchdog.h"
#include "debug.h"

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

void sendStatus(const DeviceState& dev, const ClockState& clk);

// Telemetry, well clear of the bridge's own heartbeat so the two do not beat
// against each other. Audio mode returns from the loop early and still owes the
// bridge a status, or the link would look dead the whole time music is playing.
static void heartbeat(const DeviceState& dev, const ClockState& clk) {
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus >= 5000) { lastStatus = millis(); sendStatus(dev, clk); }
}

static void frameSync(uint16_t hz) {
  const uint32_t period = 1000000UL / hz;
  while (micros() - lastMicros < period) { /* spin briefly */ }
  lastMicros = micros();
}

void setup() {
  Serial.begin(115200);       // USB debug (+ audio, per USB type)
  // Why we are here. A Teensy hard fault does not reset the chip — it hangs,
  // and our own watchdog turns that into a reset 2s later, so "it rebooted"
  // and "it faulted" look identical from the outside without this.
  dbg::resetCause();
  hal::dac::init();
  vec::init();                // sin/cos tables, beam parked and blanked
  hal::rtc::init();
  hal::input::init();
  hal::link::init();          // Serial1 <-> ESP32 bridge
  hal::midi::init();          // USB-MIDI on the front jack
  faces::registerBuiltins();
  clk.rtcPresent = hal::rtc::present();
  hal::link::sendHello();
}

void loop() {
  // Unconditionally first: as long as the loop turns over, the beam is moving.
  hal::wdt::feed();
  hal::wdt::armAfter(15000);     // only once this firmware has proven itself

  // Diagnostic console on the front jack, so audio mode can be driven without
  // the bridge. Reflashing the Teensy drops the USB-host port and USBHost_t36
  // will not re-claim it, so every experiment that goes through the link costs
  // a multi-minute recovery; this costs nothing.
  if (Serial.available()) {
    const int c = Serial.read();
    if (c >= '0' && c <= '3') { hal::audio::setVariant((uint8_t)(c - '0')); dev.mode = Mode::Audio; }
    else if (c == 'f')        { dev.mode = Mode::Face; }
    else if (c == 's')        { dbg::probeSd(); }
  }

  hal::link::poll(dev, clk);     // 1. host commands in (non-blocking)
  hal::input::poll(dev);         // 2. encoder/button out
  hal::midi::poll();             // 2b. USB-MIDI in (bounded drain, front jack)

  // Audio mode hands the DACs to the audio DMA, so the changeover has to happen
  // exactly on the edge — starting it twice re-runs a 257ms ramp, and failing to
  // stop it leaves the render path writing DACs the DMA is also driving.
  static Mode lastMode = Mode::Face;
  if (dev.mode != lastMode) {
    if (dev.mode == Mode::Audio)       hal::audio::start();
    else if (lastMode == Mode::Audio)  hal::audio::stop();
    lastMode = dev.mode;
  }

  if (dev.mode == Mode::Audio) {
    // Nothing to compose and nothing to render: X and Y come straight off the
    // stereo pair at 44.1kHz. All this loop owes the display is the level watch
    // that keeps a silent, stationary beam from burning the phosphor.
    hal::audio::service();
    dev.frameUs = 0;               // no frame was drawn; do not report a stale one
    heartbeat(dev, clk);

    // Paced, exactly like the render path is. Returning early without this let
    // the loop free-run at ~100kHz, which hammers USBHost_t36's Task() and the
    // MIDI drain a hundred thousand times a second for no benefit whatsoever —
    // the DMA is feeding the DACs and there is nothing here that needs doing
    // faster than the millisecond USB frame it is all built on.
    static uint32_t liveness = 0;
    if (millis() - liveness >= 1000) {
      liveness = millis();
      dbg::sayf("audio: t=%lus level=%u lit=%d", (unsigned long)(millis()/1000), hal::audio::level(), hal::audio::beamLit()?1:0);
    }
    frameSync(1000);
    return;
  }

  frame.clear();                 // 3. compose the current frame
  switch (dev.mode) {
    case Mode::Face:
      hal::rtc::read(clk);
      vec::updateScreenSaver(clk.hour);
      faces::current(dev)(clk, frame);
      break;
    case Mode::Pushed: {
      // Copy, then resolve any template items against the RTC. The copy is what
      // makes a template reusable: the retained list keeps its format strings,
      // and this frame gets the rendered result. A pushed list with no template
      // items simply passes through untouched.
      frame = dev.pushed;
      hal::rtc::read(clk);
      static char scratch[128];
      expandTemplate(frame, clk, scratch, sizeof scratch);
      break;
    }
  }
  txt::centerLines(frame);       // 4. resolve text positions (no-op if placed)
  overlayBanner(dev, frame);     // 5. banner goes on top, already positioned

  vec::setBrightness(dev.brightness);
  const uint32_t drawStart = micros();
  vec::renderFrame(frame);       // 5. draw to the CRT (the refresh)
  dev.frameUs = micros() - drawStart;
  vec::tuneDwell(dev.frameUs, 1000000UL / dev.hz);

  heartbeat(dev, clk);

  frameSync(dev.hz);             // 6. hold 50/60 Hz cadence
}


