// faces_audio.cpp — the microphone visualisers.
//
// The mic is in the AtomS3U on the back of the clock, so what these draw is the
// room the clock is standing in. The DSP is on the bridge (hard rule 5), and
// only band energies and one triggered trace ever cross the link — see
// SetAudio / SetWave in shared/protocol.h.
//
// All four are polar or full-width on purpose. A spectrum drawn as a row of
// bars along the bottom would spend most of its beam time where a round tube
// has no phosphor; wrapped round the rim it uses the glass the way the glass
// wants to be used, which is the same reasoning the radar face is built on.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"
#include "audiofx.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

namespace faces { namespace impl {
namespace {

constexpr int kMask = vec::kSteps - 1;

// Clockwise from north, like every other polar face here.
inline int px(int a, int r) { return (int)((r * vec::sinT(a & kMask)) >> 16); }
inline int py(int a, int r) { return (int)((r * vec::cosT(a & kMask)) >> 16); }

void centred(DrawList& d, int y, int sc, const char* s) {
  d.text(-txt::inkWidth(sc, s) / 2, y, sc, s);
}

// Every one of these says so rather than sitting at zero, which is exactly what
// a broken face looks like.
bool silent(DrawList& d) {
  if (afx::live()) return false;
  centred(d, -70, 8, "NO SIGNAL");
  // Short on purpose: at y = -260 the chord still allows plenty, but the longer
  // line this replaced measured 1070 device units wide and put its corners past
  // the field at the authored size.
  centred(d, -260, 6, "MIC IS OFF");
  return true;
}

} // namespace

// 32 bars round the rim, mirrored about the vertical so the figure is
// symmetric — 16 bands spread over a full turn are chunky and read as a
// wheel of unrelated spikes, where mirroring makes it a shape.
void spectrum(const ClockState&, DrawList& d) {
  afx::tick();
  constexpr int kBase = 420, kTop = 1150;
  d.circle(0, 0, kBase);
  if (silent(d)) return;

  const afx::State& s = afx::get();
  for (int i = 0; i < 32; ++i) {
    const int a = i * (vec::kSteps / 32);
    const uint8_t v = s.band[i < 16 ? i : 31 - i];
    const uint8_t p = s.peak[i < 16 ? i : 31 - i];
    const int r = kBase + ((kTop - kBase) * v) / 255;
    d.line(px(a, kBase), py(a, kBase), px(a, r), py(a, r));
    // The peak mark is what makes a transient readable after it has gone.
    const int pr = kBase + ((kTop - kBase) * p) / 255;
    if (pr > r + 20)
      d.line(px(a, pr), py(a, pr), px(a, pr + 45), py(a, pr + 45));
  }
}

// The triggered trace. The bridge starts each capture at a rising zero
// crossing, which is the only reason this stands still instead of skating
// sideways — the thing a scope has always done, on a clock that is one.
void scope(const ClockState&, DrawList& d) {
  afx::tick();
  constexpr int kHalf = 1080;
  d.line(-kHalf, 0, kHalf, 0);
  if (silent(d)) return;

  const afx::State& s = afx::get();
  int n = s.waveN;
  if (n < 2) { centred(d, -70, 8, "NO TRACE"); return; }
  // Every other sample past 48. A trace is beam TRAVEL, and travel is what the
  // frame budget is made of: at full deflection 64 points measured 159% of the
  // gauges control, which is a face whose cost depends on its data — the thing
  // the radar had to be fixed for.
  const int step = n > 48 ? 2 : 1;

  int lx = 0, ly = 0; bool first = true;
  for (int i = 0; i < n; i += step) {
    const int x = -kHalf + (2 * kHalf * i) / (n - 1);
    int y = s.wave[i] * 6;
    // Clamped to the CHORD at this x, not to a constant: the trace is a
    // rectangle and the tube is a circle, so a full-scale wave put its corners
    // 1482 units out — well past the 1200 field — while every individual
    // coordinate looked reasonable.
    const int32_t h2 = 1180L * 1180L - (int32_t)x * x;
    const int lim = h2 > 0 ? (int)sqrtf((float)h2) : 0;
    if (y >  lim) y =  lim;
    if (y < -lim) y = -lim;
    if (!first) d.line(lx, ly, x, y);
    first = false;
    lx = x; ly = y;
  }
}

// Loudness as a ring, with a peak ring that falls back slowly. The one that
// still reads from across a room when a spectrum has turned to mush.
void vumeter(const ClockState&, DrawList& d) {
  afx::tick();
  // 1040 rather than 1120 because the SCALE sits outside the ring: the ticks
  // used to run to kMax + 30 + 100 = 1250, which is past the field even though
  // every ring inside them was comfortably in it.
  constexpr int kMin = 260, kMax = 1040;
  if (silent(d)) { d.circle(0, 0, kMin); return; }

  const afx::State& s = afx::get();
  const int r  = kMin + ((kMax - kMin) * s.level) / 255;
  const int pr = kMin + ((kMax - kMin) * s.peakLevel) / 255;
  d.circle(0, 0, r);
  d.circle(0, 0, pr);

  // A scale to read it against: twelve ticks, every third one longer.
  for (int i = 0; i < 12; ++i) {
    const int a = i * (vec::kSteps / 12);
    const int len = (i % 3) ? 45 : 85;
    d.line(px(a, kMax + 30), py(a, kMax + 30),
           px(a, kMax + 30 + len), py(a, kMax + 30 + len));
  }

  static char lab[12];
  snprintf(lab, sizeof lab, "%u", (unsigned)((s.level * 100) / 255));
  centred(d, -60, 11, lab);
}

// The spectrogram wound outward: one continuous stroke, newest turn at the
// centre. Drawn as a single spiral rather than concentric rings because
// consecutive segments share an endpoint, so the beam never has to fly between
// them and the whole figure costs one settling delay per segment.
void waterfall(const ClockState&, DrawList& d) {
  afx::tick();
  if (silent(d)) return;

  const afx::State& s = afx::get();
  constexpr int kTurns = 8, kPer = 16;      // 128 segments
  constexpr int kR0 = 300, kR1 = 1080;

  int lx = 0, ly = 0;
  for (int t = 0; t < kTurns; ++t) {
    // Turn 0 is the newest row. Walking BACK from histHead is what puts time
    // in the radius: further out is longer ago.
    const int row = (s.histHead + afx::kHist - t) % afx::kHist;
    for (int k = 0; k < kPer; ++k) {
      const int step = t * kPer + k;
      const int a = k * (vec::kSteps / kPer);
      const int base = kR0 + ((kR1 - kR0) * step) / (kTurns * kPer);
      // Energy pushes the stroke outward from its place on the spiral, so the
      // shape itself is the spectrogram rather than a brightness it cannot have.
      const int r = base + (s.hist[row][k] * 90) / 255;
      const int x = px(a, r), y = py(a, r);
      if (step) d.line(lx, ly, x, y);
      lx = x; ly = y;
    }
  }
}

}}  // namespace faces::impl
