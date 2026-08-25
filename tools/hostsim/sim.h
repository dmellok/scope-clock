// sim.h — what the fake DAC measured.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace sim {

struct Stats {
  uint32_t dots   = 0;      // dots drawn with the beam ON
  uint32_t moves  = 0;      // blanked repositions, i.e. stroke starts
  int32_t  maxR   = 0;      // furthest lit dot from centre, in DAC counts
  int32_t  minX = 0, maxX = 0, minY = 0, maxY = 0;   // DAC counts, centred
  uint32_t outside = 0;     // lit dots beyond the usable rim
};

// The tube's usable radius, measured by pushing concentric rings and looking.
// Device units scale by 3/2 on the way out, so a face authored to the 1200
// working edge lands exactly here.
constexpr int kRimDac = 1800;

void   reset();
Stats  get();

// ---- stroke capture, for the thumbnail generator --------------------------
//
// Captured at the BEAM, not at the draw list: everything the tube draws — text
// glyphs and circles included — arrives here as plain polylines, so a consumer
// needs no glyph table and no circle generator of its own. A stroke is the run
// of lit dots between an unblank and the next blank, which is exactly one pass
// of the beam.
struct Pt { int16_t x, y; };                 // DAC counts, centred on 0

void captureBegin();                          // start recording, clears
void captureEnd();
int  strokeCount();
int  strokePoints(int i);
Pt   strokePoint(int i, int j);

}  // namespace sim
