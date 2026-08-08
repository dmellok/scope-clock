// input.cpp — rotary encoder, button, and the analog centring pots.
// Quadrature table and button debounce ported from SCTVcode InitEnc/DoEnc/DoButt.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal/input.h"
#include "hal/link.h"
#include "face.h"
#include "protocol.h"     // shared/
#include "state.h"
#include "vector.h"
#include <Arduino.h>

namespace {
constexpr int kBtn = 14, kA = 16, kB = 15;

// Centring pots. X is A15 on every Teensy 3.6 board (SCTV A..D). Y moved
// between revisions — A16 on B..D, A18 on A — so it was resolved by probing:
// A16 reads a rock-steady wiper (jitter ~0.6 LSB, same as the known-good A15)
// while A18 swings randomly across a third of its range, which is what an
// unconnected pin does. This is a B..D board.
constexpr int kXPosPin = A15;
constexpr int kYPosPin = A16;

// The original summed 40 ADC reads per pin per frame to beat the noise down,
// which spends the better part of a millisecond of the refresh inside
// analogRead. One sample a frame through a slow IIR gets the same smoothing
// for a fortieth of the cost, and a knob cannot move fast enough to notice a
// ~half-second time constant. The loop is the refresh; keep it out of the ADC.
constexpr int kTrimShift = 5;   // ~32 frames

struct Trim {
  int     pin;
  int32_t accum  = 0;
  bool    primed = false;

  // Matches the original's gain: it averaged 40 samples of (raw - 512) and
  // divided by 10, i.e. 4x the centred reading — a +/-2048 count range, half
  // the DAC either way. This is the coarse centring control, so it wants to be
  // broad enough to walk the image right across the tube.
  int step() {
    const int32_t sample = ((int32_t)analogRead(pin) - 512) * 4;
    if (!primed) {                    // jump straight to the knob on boot
      accum = sample << kTrimShift;
      primed = true;
    } else {
      accum += sample - (accum >> kTrimShift);
    }
    return (int)(accum >> kTrimShift);
  }
};

Trim xTrim{kXPosPin};
Trim yTrim{kYPosPin};

// ---- rotary encoder ------------------------------------------------------
// Old reading in bits 1:0, new reading in bits 3:2; the table turns that pair
// into a direction. The PEC11R has two detents per electrical cycle, so only
// transitions where A moves count — which is why half the table is `none`.
// `impos` entries are states you cannot reach without missing an edge, and
// scoring them zero is what makes the decoder immune to contact bounce.
constexpr int8_t incr = 1, decr = -1, none = 0, impos = 0;
const int8_t kEncTab[16] = {
  none, decr, none, impos,   // new = 00
  incr, none, impos, none,   // new = 01
  none, impos, none, incr,   // new = 10
  impos, none, decr, none    // new = 11
};

// The original polled the encoder from inside the render loop because one pass
// of the display can take most of a frame, and a knob turned briskly changes
// state faster than that — it sampled again mid-DispStr to avoid dropping
// detents. Edge interrupts get the same result without the render code having
// to know the encoder exists: the ISR only fires when the knob actually moves,
// so it costs nothing while the display is idle.
volatile uint8_t encState = 0;
volatile int8_t  encDelta = 0;

void encIsr() {
  encState = (uint8_t)(((encState >> 2) | (digitalReadFast(kB) << 3)
                                        | (digitalReadFast(kA) << 2)) & 0x0f);
  encDelta = (int8_t)(encDelta + kEncTab[encState]);
}

// ---- button --------------------------------------------------------------
// Active low. Three consecutive agreeing polls to debounce, as the original,
// but classified on release so a long hold can be told apart from a tap.
constexpr uint8_t  kDebouncePolls = 3;
constexpr uint32_t kLongPressMs   = 800;
uint8_t  butHist    = 0;
bool     butDown    = false;
bool     longSent   = false;
uint32_t butDownMs  = 0;

void sendButton(uint8_t kind) {   // 0 = press, 1 = long
  hal::link::send(static_cast<uint8_t>(proto::Msg::EventButton), &kind, 1);
}
} // namespace

namespace hal { namespace input {

void init() {
  pinMode(kBtn, INPUT_PULLUP);
  pinMode(kA, INPUT_PULLUP);
  pinMode(kB, INPUT_PULLUP);
  analogReadResolution(10);   // the +/-512 centring above assumes 10 bits

  // Seed the history with where the knob actually is, so the first edge is not
  // read as motion. (InitEnc: the x5 copies the 2-bit reading into both the
  // old and new halves at once.)
  encState = (uint8_t)((digitalReadFast(kB) << 1 | digitalReadFast(kA)) * 5);
  attachInterrupt(digitalPinToInterrupt(kA), encIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(kB), encIsr, CHANGE);
}

void poll(DeviceState& dev) {
  vec::setTrim(xTrim.step(), yTrim.step());

  // Drain whatever the ISR accumulated since the last frame.
  noInterrupts();
  const int8_t detents = encDelta;
  encDelta = 0;
  interrupts();

  if (detents != 0) {
    // Turning the knob picks the next face, as it did originally. Faces stay
    // local so the clock keeps working with no host attached; the host is only
    // told that the knob moved.
    const int n = faces::count();
    int f = ((int)dev.faceId + detents) % n;
    if (f < 0) f += n;
    dev.faceId = (uint8_t)f;
    // The knob is the way out of anything the host has pushed. Without this a
    // bad scene could only be cleared over the network, which is a poor place
    // to leave a physical object with a physical control on it.
    dev.mode = Mode::Face;
    hal::link::send(static_cast<uint8_t>(proto::Msg::EventEncoder),
                    reinterpret_cast<const uint8_t*>(&detents), 1);
  }

  const bool down = (digitalReadFast(kBtn) == 0);   // zero is pressed
  if (down) {
    if (butHist < kDebouncePolls) ++butHist;
    if (butHist == kDebouncePolls && !butDown) {
      butDown = true; longSent = false; butDownMs = millis();
    }
    if (butDown && !longSent && (millis() - butDownMs) >= kLongPressMs) {
      longSent = true;
      sendButton(1);                                 // long, reported on hold
    }
  } else {
    if (butDown && !longSent) sendButton(0);         // tap, reported on release
    butHist = 0;
    butDown = false;
  }
}

}}
