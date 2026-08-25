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
#include "radar.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

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

// A sweep over contacts placed by bearing and range.
//
// This is the one face whose layout the tube actually wanted: it is polar, so
// unlike an animation laid out in a rectangle it spends no beam time in corners
// that have no phosphor. The scope is drawn whether or not anything has been
// sent — an empty radar still reads as a working instrument, where a blank tube
// is indistinguishable from a broken face.
//
// Nothing here needs the host. The sweep runs off millis(), and a contact's
// brightness-substitute is its SIZE: the tube has one global beam dwell, so a
// blip cannot be drawn dimmer, but it can be drawn smaller. Blips therefore
// bloom as the sweep passes and shrink until it comes round again, which reads
// as phosphor decay without needing per-item brightness the render path does
// not have.
void radar(const ClockState&, DrawList& d) {
  const rdr::Set& r = rdr::get();

  constexpr int kOuter  = 1120;   // outer range ring
  constexpr int kInner   = 620;
  constexpr int kPeriod = 4000;   // ms per revolution
  constexpr int kMask   = vec::kSteps - 1;
  constexpr int kBlipHi = 50, kBlipLo = 12;

  // Clockwise from north. The trig tables run anticlockwise from east, so sin
  // and cos swap places on the way out — same as the nowplaying ring.
  auto px = [](int a, int rad) { return (int)((rad * vec::sinT(a & kMask)) >> 16); };
  auto py = [](int a, int rad) { return (int)((rad * vec::cosT(a & kMask)) >> 16); };

  const int sweep = (int)(((uint32_t)(millis() % kPeriod) * vec::kSteps) / kPeriod);

  d.circle(0, 0, kOuter);
  d.circle(0, 0, kInner);

  // Ticks at the cardinals rather than full crosshairs: two lines across the
  // whole face cost 4480 units of beam travel for orientation that four short
  // strokes give for a tenth of it.
  for (int q = 0; q < 4; ++q) {
    const int a = q * (vec::kSteps / 4);
    d.line(px(a, kOuter - 70), py(a, kOuter - 70), px(a, kOuter), py(a, kOuter));
  }

  // The sweep, with two shorter trailing arms behind it to suggest the decay.
  d.line(0, 0, px(sweep, kOuter), py(sweep, kOuter));
  d.line(0, 0, px(sweep - 22, (kOuter * 4) / 5), py(sweep - 22, (kOuter * 4) / 5));
  d.line(0, 0, px(sweep - 44, (kOuter * 3) / 5), py(sweep - 44, (kOuter * 3) / 5));

  if (!r.valid || !r.count) {
    centred(d, -kOuter + 60, 7, r.valid ? "NO CONTACTS" : "NO DATA");
    return;
  }

  // One label buffer PER contact. An Item keeps the char* it was handed, so a
  // single shared buffer leaves every label pointing at whichever was formatted
  // last — the bug this codebase has already had twice.
  static char lab[rdr::kMax][16];

  // A radar names what it has just painted, and only a few at a time. The cap
  // is not cosmetic: without it this face's cost depends on how the contacts
  // happen to be ARRANGED, because twelve hosts clustered inside the label
  // window all get named in the same frame. Measured, that was 469 beam
  // strokes against gauges' 344 — about 9ms of a 16.7ms frame in settling
  // alone. Naming only the four most recently swept makes the worst frame the
  // same shape whatever the network looks like.
  constexpr int kMaxLabels = 4;
  int  age[rdr::kMax] = {0};
  bool named[rdr::kMax] = {false};
  const uint8_t n = r.count < rdr::kMax ? r.count : rdr::kMax;
  for (uint8_t i = 0; i < n; ++i)
    age[i] = (sweep - r.c[i].bearing * (vec::kSteps / 256)) & kMask;
  for (int k = 0; k < kMaxLabels; ++k) {
    int best = -1;
    for (uint8_t i = 0; i < n; ++i) {
      if (named[i] || age[i] > vec::kSteps / 4) continue;
      if (best < 0 || age[i] < age[best]) best = i;
    }
    if (best < 0) break;
    named[best] = true;
  }

  for (uint8_t i = 0; i < n; ++i) {
    const rdr::Contact& c = r.c[i];
    const int a   = c.bearing * (vec::kSteps / 256);
    const int rad = (c.range * kOuter) / 255;
    const int x = px(a, rad), y = py(a, rad);

    // age[i] is steps since the sweep last crossed this bearing, 0 = just lit.
    const int bl = kBlipLo + ((kBlipHi - kBlipLo) * (vec::kSteps - age[i])) / vec::kSteps;

    d.circle(x, y, bl);
    if (c.flags & rdr::FlagSelf) d.circle(x, y, bl / 2);   // this host, ringed twice
    // The "new" tick points INWARD. Hung outward it sits exactly where a rim
    // contact has no glass left, which is what pushed the worst radius past the
    // authored field; inward it costs nothing and reads the same.
    if (c.flags & rdr::FlagNew) {
      const int t0 = rad - bl - 25 > 0 ? rad - bl - 25 : 0;
      const int t1 = rad - bl - 75 > 0 ? rad - bl - 75 : 0;
      if (t0 != t1) d.line(px(a, t0), py(a, t0), px(a, t1), py(a, t1));
    }

    if (!named[i]) continue;
    snprintf(lab[i], sizeof lab[i], "%s", c.label);
    if (!lab[i][0]) continue;

    // Placed on the centre side of the blip, and fitted to the CHORD at its own
    // height rather than to the field width. That distinction is the whole
    // trap: a name that sits well inside +-1200 in x and y still puts its far
    // corner off the glass near the top, because at y = 1090 the chord is only
    // about 500 either side. Measured at the authored size this was the one
    // thing reaching 1500 device units.
    const int ly = y - 30;
    int sc = 6, w = 0, room = 0;
    for (;; --sc) {
      // Measured at the ink's WORST height, not the baseline: ink runs a full
      // cell above the baseline and a descender below it, so the chord that
      // matters for a label above centre is the one at its top edge.
      const int top = ly >= 0 ? ly + txt::height(sc) : ly - txt::height(sc) / 4;
      const int32_t h2 = 1200L * 1200L - (int32_t)top * top;
      if (h2 > 0) {
        room = x >= 0 ? (x - bl - 30) + (int)sqrtf((float)h2)
                      : (int)sqrtf((float)h2) - (x + bl + 30);
        w = txt::inkWidth(sc, lab[i]);
        if (w <= room && room > 0) break;
      }
      if (sc <= 4) { w = room + 1; break; }      // will not fit at any size
    }
    if (w > room) continue;                      // say nothing rather than lie
    const int lx = x >= 0 ? x - bl - 30 - w : x + bl + 30;
    d.text(lx, ly, sc, lab[i]);
  }

  // Fitted to the chord at its own height, not the field width — the same trap
  // as the contact labels, and the one thing still reaching past the authored
  // field once those were fixed. At y = -1060 the glass is about 520 either
  // side, where kWide would have allowed 950.
  // Fitted here rather than through centred(), which cannot help: fitScale()
  // floors at kMinSc and so clamps back UP when the chord asks for something
  // smaller, drawing past the rim instead of shrinking. Measured, that put this
  // string's ink at x = -697 where the chord allowed 523.
  if (r.footer[0]) {
    const int fy = -kOuter + 60;
    const int lo = fy - 20;                        // allow for the descender
    const int32_t h2 = 1200L * 1200L - (int32_t)lo * lo;
    if (h2 > 0) {
      const int wide = 2 * (int)sqrtf((float)h2);
      int sc = 7, w = txt::inkWidth(sc, r.footer);
      while (w > wide && sc > 3) { --sc; w = txt::inkWidth(sc, r.footer); }
      if (w <= wide) d.text(-w / 2, fy, sc, r.footer);
    }
  }
}

}}  // namespace faces::impl
