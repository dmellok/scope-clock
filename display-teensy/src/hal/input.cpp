// input.cpp — rotary encoder, button, and the analog centring pots.
// TODO(port): DoEnc/DoButt quadrature table + debounce, then emit
// proto EventEncoder/EventButton via hal::link::send.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal/input.h"
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
} // namespace

namespace hal { namespace input {

void init() {
  pinMode(kBtn, INPUT_PULLUP);
  pinMode(kA, INPUT_PULLUP);
  pinMode(kB, INPUT_PULLUP);
  analogReadResolution(10);   // the +/-512 centring above assumes 10 bits
}

void poll(DeviceState& dev) {
  (void)dev;   // TODO(port): encoder table + button -> EventEncoder/EventButton
  vec::setTrim(xTrim.step(), yTrim.step());
}

}}
