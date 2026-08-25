// sim.cpp — the fake DAC, and the clocks the render path spins on.
//
// A dot only counts when the beam is actually making photons. vec::beginStroke
// writes the DAC while still blanked to reposition, so counting every write
// would inflate the cost of a face with many short strokes by exactly its
// stroke count — which is the opposite of the truth, since those are the items
// whose real cost is settling time, accounted separately.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal/dac.h"
#include "sim.h"
#include <vector>

uint32_t simDemcr = 0, simDwtCtrl = 0;

namespace {
uint32_t g_ms  = 0;
uint32_t g_cyc = 0;
bool     g_blanked = true;
sim::Stats g_st;
constexpr int kMidDac = 2048;
}

uint32_t simMillis() { return g_ms; }
void simStepMillis(uint32_t ms) { g_ms += ms; }
void simSetMillis(uint32_t ms) { g_ms = ms; }

// Big steps on purpose: the dwell loop exits after one iteration rather than
// spinning 120 times a dot. Nothing here measures time in cycles.
uint32_t simCycles() { g_cyc += 4096; return g_cyc; }

namespace {
bool g_cap = false;
std::vector<std::vector<sim::Pt>> g_strokes;
bool g_inStroke = false;
}

namespace sim {
void  reset() { g_st = Stats{}; }
Stats get()   { return g_st; }

void captureBegin(){ g_cap = true; g_inStroke = false; g_strokes.clear(); }
void captureEnd()  { g_cap = false; g_inStroke = false; }
int  strokeCount() { return (int)g_strokes.size(); }
int  strokePoints(int i){ return (int)g_strokes[i].size(); }
Pt   strokePoint(int i,int j){ return g_strokes[i][j]; }
}

namespace hal { namespace dac {

void init() {}

void blank(bool on) {
  g_blanked = on;
  if (!g_cap) return;
  // A stroke ends when the beam is blanked and begins on the next lit dot, not
  // here: beginStroke writes the DAC while STILL blanked to reposition, and
  // treating that write as the stroke's first point would draw a line in from
  // wherever the previous stroke ended.
  if (on) g_inStroke = false;
}

void write(int x, int y) {
  if (g_blanked) { ++g_st.moves; return; }
  const int cx = x - kMidDac, cy = y - kMidDac;
  ++g_st.dots;
  if (g_cap) {
    if (!g_inStroke) { g_strokes.push_back({}); g_inStroke = true; }
    g_strokes.back().push_back(sim::Pt{(int16_t)cx, (int16_t)cy});
  }
  if (cx < g_st.minX) g_st.minX = cx;
  if (cx > g_st.maxX) g_st.maxX = cx;
  if (cy < g_st.minY) g_st.minY = cy;
  if (cy > g_st.maxY) g_st.maxY = cy;
  // Radius, not a box: on a round tube a corner inside the box can still be
  // off the glass, which is why this is the number that matters.
  const int32_t r2 = (int32_t)cx * cx + (int32_t)cy * cy;
  const int32_t r  = (int32_t)(r2 > 0 ? __builtin_sqrt((double)r2) : 0);
  if (r > g_st.maxR) g_st.maxR = r;
  if (r > sim::kRimDac) ++g_st.outside;
}

}}  // namespace hal::dac
