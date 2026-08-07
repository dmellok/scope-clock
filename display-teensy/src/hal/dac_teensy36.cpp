// dac_teensy36.cpp — internal dual DAC (A21/A22) + blank pin.
//
// write() is the hottest code in the firmware: the analog face puts roughly
// 14,000 dots on the tube every refresh, so this runs ~28,000 times per frame
// and its cost sets the frame rate outright.
//
// Which is why it does not call analogWrite(). That helper re-does the entire
// DAC setup on every single call:
//
//     SIM_SCGC2 |= SIM_SCGC2_DAC0;              // RMW of a clock gate
//     DAC0_C0 = DAC_C0_DACEN | DAC_C0_DACRFS;   // rewrite the control register
//     *(volatile aliased_int16_t *)&DAC0_DAT0L = val;   // the only useful part
//
// — three peripheral accesses plus a function call and pin dispatch, where one
// store would do. Peripheral writes cross the AIPS bridge and the read in that
// read-modify-write stalls, so it costs ~113 cycles a channel. Hoisting the
// setup into init() leaves a single 16-bit store in the hot path.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal/dac.h"
#include <Arduino.h>

namespace {
constexpr int kBlankPin = 2;

// The 12-bit value spans DAT0L/DAT0H, which are adjacent; the core writes them
// as one 16-bit store via a may_alias type, and so do we.
typedef int16_t __attribute__((__may_alias__)) dac_data_t;

// Clamp rather than let a 12-bit DAC wrap: an off-screen coordinate should slam
// into the edge of the tube, not reappear on the opposite side.
inline int clampDac(int v) { return v < 0 ? 0 : (v > 4095 ? 4095 : v); }
}

namespace hal { namespace dac {

void init() {
  // Everything analogWrite() would otherwise redo per call, done once. DACRFS
  // selects the 3.3V VDDA reference, matching what analogWrite() picks under
  // the default analogReference() — so the output range is unchanged and the
  // tuned display geometry still lands where it did.
  SIM_SCGC2 |= SIM_SCGC2_DAC0 | SIM_SCGC2_DAC1;
  DAC0_C0 = DAC_C0_DACEN | DAC_C0_DACRFS;
  DAC1_C0 = DAC_C0_DACEN | DAC_C0_DACRFS;

  pinMode(kBlankPin, OUTPUT);
  digitalWriteFast(kBlankPin, LOW);   // start blanked, before anything is drawn
}

void write(int x, int y) {
  *(volatile dac_data_t *)&DAC0_DAT0L = (dac_data_t)clampDac(x);
  *(volatile dac_data_t *)&DAC1_DAT0L = (dac_data_t)clampDac(y);
}

// The blanking input is active low despite the name: driving it HIGH is what
// makes photons (SCTVcode DoSeg). blank(true) therefore drives it LOW.
void blank(bool on) { digitalWriteFast(kBlankPin, on ? LOW : HIGH); }

}}
