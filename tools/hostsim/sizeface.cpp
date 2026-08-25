// sizeface.cpp — sweep a face for many frames and report its worst frame.
//
// One sampled frame is not enough and never was: the tesseract and the tunnel
// both ran off the tube only partway through a rotation, and sector only
// reaches 95% of the frame budget at 23:59:59. So this renders a long run and
// reports the worst value seen, not a snapshot.
//
// What it measures:
//   maxR      furthest lit dot from centre, in DAC counts. The bound is a
//             RADIUS, not a box — a name inside +-1200 in x and y can still put
//             its corners off the glass, because at y = -1000 the chord is only
//             about 560 either side.
//   dots      lit dots in the worst frame. Beam-on time is proportional to it.
//   moves     blanked repositions in the worst frame, i.e. stroke starts. Each
//             costs settling + glow, about 20us, and ignoring that made
//             oganesson look like 86% of budget when the tube reported 102-109%.
//
// The absolute us-per-dot calibration of the original harness did not survive,
// so this deliberately reports a KNOWN-GOOD face alongside the one under test.
// A relative number against a face that is known to render cleanly on the tube
// is worth more than an absolute one derived from a guess.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "face.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "radar.h"
#include "gauges.h"
#include "audiofx.h"
#include <math.h>
#include "sim.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr int kFrames  = 1100;
constexpr int kFrameMs = 17;

struct Worst {
  uint32_t dots = 0, moves = 0;
  int32_t  maxR = 0;
  uint32_t outside = 0;
  int      dotsFrame = 0, rFrame = 0;
};

Worst sweep(const char* what, faces::RenderFn fn, int pct) {
  ClockState clk;
  clk.hour = 23; clk.minute = 59; clk.second = 59; clk.rtcPresent = true;
  Worst w;
  for (int f = 0; f < kFrames; ++f) {
    DrawList d;
    fn(clk, d);
    // What the tube actually receives. The device scales the FACE (not the
    // overlays) by the per-face percentage before rendering, so measuring the
    // authored 100% answers "is it authored to the field" while this answers
    // "does it fit the glass" — and they are different questions.
    if (pct != 100) scaleList(d, pct);
    sim::reset();
    vec::renderFrame(d);
    const sim::Stats s = sim::get();
    if (s.dots > w.dots) { w.dots = s.dots; w.dotsFrame = f; }
    if (s.moves > w.moves) w.moves = s.moves;
    if (s.maxR > w.maxR)  { w.maxR = s.maxR; w.rFrame = f; }
    w.outside += s.outside;
    simStepMillis(kFrameMs);
  }
  // Device units are DAC counts * 2/3; the 1200 working edge is the bound a
  // face must respect, and 1250 is the overlay allowance.
  const int devR = (int)(w.maxR * 2 / 3);
  printf("%-20s @%3d%%  %6u dots  %4u strokes   maxR %5d dac = %4d device%s\n",
         what, pct, w.dots, w.moves, w.maxR, devR,
         w.maxR > sim::kRimDac ? "   <-- OFF THE GLASS" : "");
  if (w.outside)
    printf("%-20s          %u lit dots beyond the %d-count rim\n",
           what, w.outside, sim::kRimDac);
  return w;
}

// Replay one frame and render each item ALONE, so the worst radius can be
// attributed to a specific primitive. "Something reaches 1249" is not
// actionable; "item 47, a Text, reaches 1249" is.
void explain(const char* what, faces::RenderFn fn, int frame, int pct) {
  static const char* kType[] = {"End","Text","Line","Circle","Clock","Hand"};
  ClockState clk;
  clk.hour = 23; clk.minute = 59; clk.second = 59; clk.rtcPresent = true;
  simSetMillis((uint32_t)frame * kFrameMs);
  DrawList d;
  fn(clk, d);
  if (pct != 100) scaleList(d, pct);

  int bestI = -1; int32_t bestR = 0;
  for (uint8_t i = 0; i < d.count; ++i) {
    DrawList one;
    one.items[0] = d.items[i];
    one.count = 1;
    sim::reset();
    vec::renderFrame(one);
    const int32_t r = sim::get().maxR;
    if (r > bestR) { bestR = r; bestI = i; }
  }
  if (bestI < 0) return;
  const Item& it = d.items[bestI];
  printf("  %s worst item: #%d %s at (%d,%d) scale %d%s%s -> %d dac = %d device\n",
         what, bestI, kType[(int)it.type], it.x, it.y, it.scale,
         it.str ? " \"" : "", it.str ? it.str : "",
         bestR, (int)(bestR * 2 / 3));
}

// 12 contacts with the longest label the wire allows, at the rim. `spread`
// puts them round the whole face; otherwise they are packed inside a quarter
// turn, which is the real worst case for text — the face only labels contacts
// the sweep has just passed, so clustering them means all twelve are named in
// the same frame.
void seedRadar(bool spread) {
  uint8_t p[240];
  const char* label = "abcdefghijklm";      // 13, the most rdr::Contact holds
  const uint8_t n = rdr::kMax;
  p[0] = n;
  uint16_t at = 1;
  for (uint8_t i = 0; i < n; ++i) {
    p[at++] = spread ? (uint8_t)(i * (256 / n)) : (uint8_t)(i * 2);
    p[at++] = 255;                          // at the rim, the worst place to be
    p[at++] = (uint8_t)(rdr::FlagNew | rdr::FlagSelf);   // both decorations on
    p[at++] = 13;
    memcpy(p + at, label, 13); at += 13;
  }
  const char* foot = "192.168.1.0/24  12 UP";
  memcpy(p + at, foot, strlen(foot)); at += strlen(foot);
  rdr::set(p, (uint8_t)at);
}

void seedGauges() {
  uint8_t p[240];
  p[0] = 4;
  uint16_t at = 1;
  for (uint8_t i = 0; i < 4; ++i) {
    p[at++] = 100; p[at++] = 8;
    memcpy(p + at, "GAUGE-88", 8); at += 8;
  }
  const char* foot = "5H RESETS IN 12h34m  9999 CR";
  memcpy(p + at, foot, strlen(foot)); at += strlen(foot);
  gauge::set(p, (uint8_t)at);
}


// Worst case for the visualisers is a LOUD room: every band pinned, which is
// the longest every bar gets and the widest the spiral swings.
void seedAudioLoud() {
  uint8_t p[3 + afx::kBands];
  p[0]=255; p[1]=255; p[2]=afx::kBands;
  for (int b=0;b<afx::kBands;++b) p[3+b]=255;
  afx::setBands(p,(uint8_t)(3+afx::kBands));
  uint8_t w[1+64]; w[0]=64;
  for (int i=0;i<64;++i) w[1+i]=(uint8_t)(int8_t)(i&1?127:-127);   // full-scale
  afx::setWave(w,65);
}

} // namespace

int main() {
  vec::init();

  printf("%d frames at %dms each (%.1fs of motion)\n\n",
         kFrames, kFrameMs, kFrames * kFrameMs / 1000.0);

  // 70 is DeviceState::kDefaultScale — what a face is actually drawn at unless
  // someone has turned the knob. 100 is the authored size, which is the bound
  // a face is written against.
  constexpr int kDefaultScale = 70;

  seedGauges();
  sweep("gauges (control)", faces::impl::gauges, 100);
  const Worst ctl = sweep("gauges (control)", faces::impl::gauges, kDefaultScale);

  seedRadar(true);
  const Worst a100 = sweep("radar, spread", faces::impl::radar, 100);
  explain("radar, spread", faces::impl::radar, a100.rFrame, 100);
  const Worst a = sweep("radar, spread", faces::impl::radar, kDefaultScale);

  seedRadar(false);
  const Worst b100 = sweep("radar, clustered", faces::impl::radar, 100);
  explain("radar, clustered", faces::impl::radar, b100.rFrame, 100);
  const Worst b = sweep("radar, clustered", faces::impl::radar, kDefaultScale);

  // The audio visualisers, at full-scale deflection: every band pinned, which
  // is the longest every bar gets and the widest the spiral swings.
  struct { const char* n; faces::RenderFn f; } av[4] = {
    {"spectrum", faces::impl::spectrum}, {"scope", faces::impl::scope},
    {"vumeter",  faces::impl::vumeter},  {"waterfall", faces::impl::waterfall}};
  for (int i = 0; i < 4; ++i) {
    seedAudioLoud();
    const Worst w100 = sweep(av[i].n, av[i].f, 100);
    if (w100.maxR > sim::kRimDac) explain(av[i].n, av[i].f, w100.rFrame, 100);
    seedAudioLoud();
    const Worst w70 = sweep(av[i].n, av[i].f, kDefaultScale);
    printf("%-20s          vs gauges: %.0f%% dots, %.0f%% strokes\n", av[i].n,
           ctl.dots ? 100.0*w70.dots/ctl.dots : 0.0,
           ctl.moves ? 100.0*w70.moves/ctl.moves : 0.0);
  }

  const uint32_t worst = a.dots > b.dots ? a.dots : b.dots;
  const uint32_t wmove = a.moves > b.moves ? a.moves : b.moves;
  printf("\nradar vs gauges: %.0f%% of the dots, %.0f%% of the strokes\n",
         ctl.dots  ? 100.0 * worst / ctl.dots  : 0.0,
         ctl.moves ? 100.0 * wmove / ctl.moves : 0.0);
  return 0;
}
