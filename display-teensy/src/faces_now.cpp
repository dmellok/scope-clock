// faces_now.cpp — what is playing, drawn for a round tube.
//
// The progress ring is the reason this face suits this display: a bar would
// waste the shape, whereas an arc sweeping clockwise from twelve is exactly
// what a circular screen is for, and it reads at a glance from across a room
// even when the titles are too small to make out.
//
// Album art is deliberately absent. It is a URL of a photograph, and a vector
// tube draws strokes; tracing one per track is minutes of work per song for a
// result no better than the text.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"
#include "nowplaying.h"
#include "gauges.h"
#include <stdio.h>

namespace faces { namespace impl {
namespace {

constexpr int kRing   = 1120;   // progress ring radius
constexpr int kWide   = 1900;   // widest any line may be drawn
constexpr int kMinSc  = 5;

// Ink width is exactly linear in scale below 40, so the scale that just fits is
// a division rather than a search. Same trick as the notification strips.
int fitScale(const char* s, int want, int wide) {
  const int unit = txt::inkWidth(1, s);
  if (unit <= 0) return want;
  int v = wide / unit;
  if (v > want)   v = want;
  if (v < kMinSc) v = kMinSc;
  return v;
}

void centred(DrawList& d, int y, int want, const char* s, int wide = kWide) {
  if (!s || !s[0]) return;
  const int sc = fitScale(s, want, wide);
  d.text(-txt::inkWidth(sc, s) / 2, y, sc, s);
}

void mmss(char* out, size_t n, uint16_t secs) {
  snprintf(out, n, "%u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
}

} // namespace

void nowplaying(const ClockState& c, DrawList& d) {
  const np::Track& t = np::track();

  if (!t.valid) {
    // Nothing has ever been sent. Say so rather than leave a blank tube, which
    // is indistinguishable from a broken face.
    centred(d, -70, 9, "NOTHING PLAYING");
    return;
  }

  // The ring. Drawn as a polyline because the circle primitive works in whole
  // 45-degree octants, which cannot express an arbitrary fraction of a song.
  const uint16_t el = np::elapsed();
  if (t.durationS) {
    constexpr int kSegs = 64;                 // a full ring; a part-ring costs less
    const int segs = 1 + (int)((uint32_t)el * kSegs / t.durationS);
    int lx = 0, ly = 0;
    for (int i = 0; i <= segs; ++i) {
      // Clockwise from twelve: the trig tables run anticlockwise from east, so
      // sin and cos swap places on the way out.
      const int a = (int)(((uint32_t)i * el * vec::kSteps) / ((uint32_t)t.durationS * segs));
      const int x = (int)((kRing * vec::sinT(a)) >> 16);
      const int y = (int)((kRing * vec::cosT(a)) >> 16);
      if (i) d.line(lx, ly, x, y);
      lx = x; ly = y;
    }
    // A tick at twelve, so an almost-empty ring still shows where it starts.
    d.line(0, kRing - 90, 0, kRing + 90);
  }

  centred(d, 250, 13, t.title);
  centred(d, 10, 10, t.artist);
  centred(d, -220, 8, t.album);

  // Elapsed and total, with a pause marker when it is stopped — otherwise a
  // paused track and a playing one look identical between ring updates.
  static char line[24], a[8], b[8];
  mmss(a, sizeof a, el);
  mmss(b, sizeof b, t.durationS);
  snprintf(line, sizeof line, t.playing ? "%s / %s" : "%s / %s  ||", a, b);
  centred(d, -650, 8, line);
  (void)c;
}

// Concentric arcs, one per gauge, longest outermost. Same idea as the sector
// clock: a ring you can read the fill of at a glance, with the full circle
// behind it so the fraction is obvious rather than a bare arc floating in space.
void gauges(const ClockState&, DrawList& d) {
  const gauge::Set& g = gauge::get();
  if (!g.valid || !g.count) {
    centred(d, -70, 9, "NO DATA");
    return;
  }

  constexpr int kOuter = 1120, kStep = 250;
  for (uint8_t i = 0; i < g.count; ++i) {
    const int r = kOuter - i * kStep;
    d.circle(0, 0, r);                       // the track

    // The arc lies on the track, so the beam passes twice over the filled part
    // and once over the rest — the contrast is free, no second pass needed.
    if (g.pct[i]) {
      const int segs = 1 + (g.pct[i] * 40) / 100;
      int lx = 0, ly = 0;
      for (int s = 0; s <= segs; ++s) {
        const int a = (int)(((int32_t)s * g.pct[i] * vec::kSteps) / (100 * segs));
        const int x = (int)((r * vec::sinT(a)) >> 16);
        const int y = (int)((r * vec::cosT(a)) >> 16);
        if (s) d.line(lx, ly, x, y);
        lx = x; ly = y;
      }
    }
    // One buffer PER gauge. An Item keeps the char* it was given, not a copy, so
    // a single shared buffer would leave all three labels pointing at whichever
    // was formatted last — which is exactly what it did.
    static char lab[gauge::kMax][24];
    snprintf(lab[i], sizeof lab[i], "%s %u%%", g.label[i], (unsigned)g.pct[i]);
    // Stacked as a legend rather than pinned to each ring: at the top of a ring
    // the left-hand side of a label is already off the glass, and reading order
    // outer-to-inner is unambiguous anyway.
    const int sc = 7;
    d.text(-txt::inkWidth(sc, lab[i]) - 80, 200 - i * 250, sc, lab[i]);
  }

  if (g.footer[0]) centred(d, -kOuter + 60, 7, g.footer);
}

}}  // namespace faces::impl
