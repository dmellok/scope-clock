// faces_sky.cpp — the real sky: one constellation at a time, and the whole
// celestial sphere turning.
//
// Both faces read the same generated table (stars.h), and that is the point of
// storing stars as UNIT VECTORS rather than as right ascension and declination.
// A chart and a globe are the same data seen through two different rotations,
// so neither face needs a single trigonometric call on a star: the chart's
// rotation was baked at build time, and the globe's is four table lookups a
// frame no matter how many stars it draws.
//
// The tube helps here in a way a raster display cannot. A star IS a dot of
// light with no shape, which is exactly what a beam parked for one dwell makes,
// and the sphere under orthographic projection is a disc — the same shape as
// the glass. Almost nothing is wasted off the phosphor.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"
#include "stars.h"
#include <Arduino.h>

namespace faces { namespace impl {
namespace {

using sky::kUnit;

// The tube is round, so the bottom of the field is a CHORD: at y = -1000 there
// are only about 560 units either side of centre, not 1200. The figure is
// therefore fitted small (in the generator) and lifted clear, and the name is
// fitted to the chord rather than assumed to fit.
constexpr int kFieldR = 1200;          // what a face may use
constexpr int kLift   = 200;           // figure shifted up by this
constexpr int kNameY  = -1000;         // name baseline
constexpr int kNameSc = 8;             // never larger than this
constexpr int kDescend = 60;           // ink below the baseline, worst case

// A globe that reached the field edge would clip once tickWobble walks the
// whole image round its 45-count circle, and the brightest stars carry a cross
// that sticks out further still.
constexpr int kGlobeR = 1120;

constexpr uint32_t kDwellMs = 9000;    // per constellation, when cycling

// The list silently stops accepting past CAP, so the budget is spent in order
// of importance and the stars — which are sorted brightest first — take what
// is left. Running out drops the faintest, which is what a shorter exposure
// would do anyway.
constexpr uint8_t kGlobeBudget = 180;
constexpr uint8_t kGlobeFigs   = 12;   // brightest constellations only

// 0 cycles; 1..kConCount pins one. Same shape as the atom face's element.
uint8_t pinned = 0;

// A star has no extent, so magnitude has to become size. Three steps is all the
// eye resolves at this dot spacing, and the brightest few earning a cross is
// what makes Sirius and Betelgeuse findable at a glance.
void mark(DrawList& d, int x, int y, int mag10) {
  if (mag10 <= 12) {
    d.line(x - 28, y, x + 28, y);
    d.line(x, y - 28, x, y + 28);
  } else if (mag10 <= 28) {
    d.line(x - 14, y, x + 14, y);
  } else {
    d.line(x, y, x, y);            // len<=0 in vec::line still puts one dot down
  }
}

// Integer square root, for the chord half-width at a given height. Newton
// converges in a handful of steps and keeps this off the FPU.
int32_t isqrt(int32_t v) {
  if (v <= 0) return 0;
  int32_t x = v, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + v / x) / 2; }
  return x;
}

// Component of a star along one row of a view basis. Both operands are scaled
// by kUnit, so the product is kUnit^2 and one division brings it back.
inline int32_t along(const sky::Star& s, int16_t ax, int16_t ay, int16_t az) {
  return ((int32_t)s.x * ax + (int32_t)s.y * ay + (int32_t)s.z * az) / kUnit;
}

}  // namespace

void setConstellation(uint8_t idx) { pinned = idx <= sky::kConCount ? idx : 0; }
uint8_t constellationCount() { return sky::kConCount; }
const char* constellationName(uint8_t i) {
  return i < sky::kConCount ? sky::kCons[i].name : "";
}

// ---------------------------------------------------------------------------
// The chart. Gnomonic projection, which is what a paper star atlas uses: it is
// the only projection that keeps great circles straight, so the figure's lines
// stay the straight lines the eye expects them to be.
//
// The basis and the fitting scale come out of the table already computed, so
// this is three multiply-adds and a divide per star and nothing else.
void constell(const ClockState&, DrawList& d) {
  const uint8_t idx = pinned ? (uint8_t)(pinned - 1)
                             : (uint8_t)((millis() / kDwellMs) % sky::kConCount);
  const sky::Constellation& k = sky::kCons[idx];

  // Project one star. Returns false behind the observer or so near the horizon
  // of the projection that the divide would fling it off the glass — nothing in
  // a real figure is, but the guard costs one comparison.
  auto project = [&](uint16_t si, int& ox, int& oy) -> bool {
    const sky::Star& s = sky::kStars[si];
    const int32_t z = along(s, k.wx, k.wy, k.wz);
    if (z < kUnit / 5) return false;
    ox = (int)(along(s, k.ux, k.uy, k.uz) * k.scale / z);
    oy = (int)(along(s, k.vx, k.vy, k.vz) * k.scale / z) + kLift;
    return true;
  };

  // The name goes down first. It is the one item that must survive a figure
  // dense enough to reach CAP, because a chart of an unnamed pattern is a
  // puzzle rather than a viewer.
  //
  // Its width is bounded by the chord at the LOWEST ink, not at the baseline —
  // Sagitta and Puppis have tails, and a name sized to the baseline would put
  // its own descenders past the glass. Ink width is linear in scale below 40,
  // so the fitting scale is a division rather than a search.
  const int low  = kNameY - kDescend;
  const int half = (int)isqrt((int32_t)kFieldR * kFieldR - (int32_t)low * low);
  int ns = kNameSc;
  const int unit = txt::inkWidth(1, k.name);
  if (unit > 0) {
    ns = (2 * half - 60) / unit;         // 60 units of margin off the rim
    if (ns > kNameSc) ns = kNameSc;
    if (ns < 4) ns = 4;
  }
  d.text(-txt::inkWidth(ns, k.name) / 2, kNameY, ns, k.name);

  // Then the figure, then the stars — the lines are what makes it recognisable,
  // the dots are decoration.
  uint16_t seen[64];
  uint8_t nseen = 0;
  for (uint16_t i = 0; i < k.count; ++i) {
    const uint16_t a = sky::kLines[(k.first + i) * 2];
    const uint16_t b = sky::kLines[(k.first + i) * 2 + 1];
    int ax, ay, bx, by;
    if (!project(a, ax, ay) || !project(b, bx, by)) continue;
    d.line(ax, ay, bx, by);
    const uint16_t ends[2] = { a, b };
    for (uint8_t e = 0; e < 2; ++e) {
      bool have = false;
      for (uint8_t j = 0; j < nseen; ++j) have |= (seen[j] == ends[e]);
      if (!have && nseen < 64) seen[nseen++] = ends[e];
    }
  }

  for (uint8_t i = 0; i < nseen; ++i) {
    int sx, sy;
    if (!project(seen[i], sx, sy)) continue;
    mark(d, sx, sy, sky::kStars[seen[i]].mag10);
  }
}

// ---------------------------------------------------------------------------
// The globe. The sphere spins about the celestial pole — which is the way the
// sky actually moves — while the pole itself nods, so the thing reads as a
// solid object rather than a flat swirl. Orthographic, so the limb is a true
// circle and lands exactly on the glass.
void starglobe(const ClockState&, DrawList& d) {
  const uint32_t t = millis();
  const int spin = (int)((t / 24) % vec::kSteps);
  // A full nod would put the pole through the viewer twice a cycle and flatten
  // the sphere to a line on the way; a third of a turn keeps it oblique.
  const int nod  = (int)(vec::kSteps / 4 + ((vec::sinT((int)((t / 190) % vec::kSteps)) * (vec::kSteps / 6)) >> 16));

  const int32_t sA = vec::sinT(spin), cA = vec::cosT(spin);
  const int32_t sT = vec::sinT(nod),  cT = vec::cosT(nod);

  // Spin about the pole, then tilt. Screen is (x, y) of the result and the near
  // hemisphere is z > 0. Components stay under 10^4, the trig under 2^16, so
  // every product here is comfortably inside int32.
  auto rot = [&](int32_t x, int32_t y, int32_t z, int32_t& ox, int32_t& oy, int32_t& oz) {
    const int32_t x1 = (x * cA - y * sA) >> 16;
    const int32_t y1 = (x * sA + y * cA) >> 16;
    ox = x1;
    oy = (y1 * cT - z * sT) >> 16;
    oz = (y1 * sT + z * cT) >> 16;
  };
  auto screen = [&](int32_t v) { return (int)(v * kGlobeR / kUnit); };

  // The direction the viewer looks along, expressed in star coordinates: the
  // third row of the same rotation. Used to decide which figures face us
  // without transforming all their stars first.
  // Both factors are already 16.16, so their product is 32.32 and overflows an
  // int32 outright — everywhere else in this file one side is a coordinate
  // under 10^4 and there is room. Three 64-bit multiplies once a frame is
  // nothing; getting it wrong put the view vector in the wrong hemisphere.
  const int32_t vx = (int32_t)(((int64_t)sA * sT) >> 16);
  const int32_t vy = (int32_t)(((int64_t)cA * sT) >> 16);
  const int32_t vz = cT;

  d.circle(0, 0, kGlobeR);   // the limb; one item, and it sells the sphere

  // The celestial equator, dashed so it is not mistaken for a figure. Only the
  // near half is drawn, which halves both the items and the beam travel.
  constexpr int kSegs = 48;
  for (int i = 0; i < kSegs; i += 2) {
    int32_t ax, ay, az, bx, by, bz;
    const int a0 = i * vec::kSteps / kSegs, a1 = (i + 1) * vec::kSteps / kSegs;
    rot((vec::cosT(a0) * kUnit) >> 16, (vec::sinT(a0) * kUnit) >> 16, 0, ax, ay, az);
    rot((vec::cosT(a1) * kUnit) >> 16, (vec::sinT(a1) * kUnit) >> 16, 0, bx, by, bz);
    if (az <= 0 || bz <= 0) continue;
    d.line(screen(ax), screen(ay), screen(bx), screen(by));
  }

  // Figures for the brightest constellations currently facing us. Three or four
  // qualify at once, which is enough to recognise what is coming round without
  // turning the sphere into a net.
  for (uint8_t ci = 0; ci < kGlobeFigs && ci < sky::kConCount; ++ci) {
    const sky::Constellation& k = sky::kCons[ci];
    // w is scaled by kUnit and v by 65536, so the shift — not a divide by
    // kUnit — is what leaves the answer back on kUnit's scale.
    const int32_t facing = ((int32_t)k.wx * vx + (int32_t)k.wy * vy + (int32_t)k.wz * vz) >> 16;
    if (facing < kUnit / 3) continue;
    for (uint16_t i = 0; i < k.count; ++i) {
      int32_t ax, ay, az, bx, by, bz;
      const sky::Star& sa = sky::kStars[sky::kLines[(k.first + i) * 2]];
      const sky::Star& sb = sky::kStars[sky::kLines[(k.first + i) * 2 + 1]];
      rot(sa.x, sa.y, sa.z, ax, ay, az);
      rot(sb.x, sb.y, sb.z, bx, by, bz);
      if (az <= 0 || bz <= 0) continue;
      d.line(screen(ax), screen(ay), screen(bx), screen(by));
    }
  }

  // Stars last, brightest first — kStars is sorted, so running out of budget
  // drops the faintest, which is exactly what a shorter exposure would do.
  for (uint16_t i = 0; i < sky::kGlobeCount && d.count < kGlobeBudget; ++i) {
    const sky::Star& s = sky::kStars[i];
    int32_t x, y, z;
    rot(s.x, s.y, s.z, x, y, z);
    if (z <= 0) continue;
    mark(d, screen(x), screen(y), s.mag10);
  }
}

}}  // namespace faces::impl
