// dac_teensy36.cpp — internal dual DAC (A21/A22) + blank pin.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal/dac.h"
#include <Arduino.h>

namespace {
constexpr int kBlankPin = 2;
constexpr int kXDac = A21;
constexpr int kYDac = A22;

// Clamp rather than let a 12-bit DAC wrap: an off-screen coordinate should slam
// into the edge of the tube, not reappear on the opposite side.
inline int clampDac(int v) { return v < 0 ? 0 : (v > 4095 ? 4095 : v); }
}

namespace hal { namespace dac {
void init() {
  analogWriteResolution(12);
  pinMode(kBlankPin, OUTPUT);
  digitalWriteFast(kBlankPin, LOW);   // start blanked, before anything is drawn
}
void write(int x, int y) { analogWrite(kXDac, clampDac(x)); analogWrite(kYDac, clampDac(y)); }
// The blanking input is active low despite the name: driving it HIGH is what
// makes photons (SCTVcode DoSeg). blank(true) therefore drives it LOW.
void blank(bool on)      { digitalWriteFast(kBlankPin, on ? LOW : HIGH); }
}}
