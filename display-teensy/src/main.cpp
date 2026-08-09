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
#include "protocol.h"     // shared/
#include "hal/midi.h"
#include "hal/audio.h"
#include "hal/watchdog.h"
#include "notify.h"
#include "settime.h"
#include "menu.h"
#include "debug.h"

static ClockState  clk;
static DeviceState dev;
static DrawList    frame;
static uint32_t    lastMicros = 0;



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

  // The knob asked to set the clock. The RTC lives here, not in hal::input, so
  // seeding the editor and committing it are done on this side of the fence.
  if (dev.editSeed) {
    dev.editSeed = false;
    hal::rtc::read(clk);
    if (dev.edit == DeviceState::Edit::Date) {
      dev.editVal[0] = (uint8_t)clk.day;
      dev.editVal[1] = (uint8_t)clk.month;
      dev.editVal[2] = (uint8_t)clk.year;
    } else {
      dev.editVal[0] = (uint8_t)clk.hour;
      dev.editVal[1] = (uint8_t)clk.minute;
      dev.editVal[2] = (uint8_t)clk.second;
    }
  }
  if (dev.editCommit) {
    // Which kind was committed is remembered separately, because dev.edit is
    // cleared the moment the last tap lands.
    const bool wasDate = dev.editCommitDate;
    dev.editCommit = false;
    hal::rtc::read(clk);                  // change one half, keep the other
    if (wasDate) {
      clk.day   = dev.editVal[0];
      clk.month = dev.editVal[1];
      clk.year  = dev.editVal[2];
      // wday is derived from the date on every read, so it follows on its own.
    } else {
      clk.hour   = dev.editVal[0];
      clk.minute = dev.editVal[1];
      clk.second = dev.editVal[2];
    }
    hal::rtc::write(clk);
  }

  // A setting changed at the knob that the host also keeps. Told once, so a
  // bridge can persist it; harmless when there is no bridge listening.
  if (dev.fontChanged) {
    dev.fontChanged = false;
    const uint8_t p = txt::defaultFaceId();
    hal::link::send(static_cast<uint8_t>(proto::Msg::EventFont), &p, 1);
  }
  if (dev.wobbleChanged) {
    dev.wobbleChanged = false;
    const uint8_t p = vec::wobble() ? 1 : 0;
    hal::link::send(static_cast<uint8_t>(proto::Msg::EventWobble), &p, 1);
  }
  hal::midi::poll();             // 2b. USB-MIDI in (bounded drain, front jack)
  vec::tickWobble();             // 2c. anti-burn-in drift, all modes

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
  // Per-face size, applied to the face and not to what goes on top of it, so a
  // notification stays where it was put whatever the face is scaled to.
  if (dev.mode == Mode::Face)
    // Bounds-checked, not wrapped: the modulo this replaces turned an
    // out-of-range face into a silently wrong answer rather than a default.
    scaleList(frame, dev.faceId < DeviceState::kMaxFaces
                       ? dev.faceScale[dev.faceId] : DeviceState::kDefaultScale);
  overlayNotify(dev, frame);     // 5. notification on top, already positioned
  overlaySetTime(dev, frame);    //    ...the editor, and the menu, over
  overlayMenu(dev, frame);       //    everything else

  vec::setBrightness(dev.brightness);
  const uint32_t drawStart = micros();
  vec::renderFrame(frame);       // 5. draw to the CRT (the refresh)
  dev.frameUs = micros() - drawStart;
  vec::tuneDwell(dev.frameUs, 1000000UL / dev.hz);

  heartbeat(dev, clk);

  frameSync(dev.hz);             // 6. hold 50/60 Hz cadence
}


