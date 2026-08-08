// faces_wire.cpp — rotating wireframes that are not polyhedra.
//
// faces_3d.cpp covers the solids whose edges fall out of a vertex table. These
// are the ones described by a formula or a profile instead: a surface of
// revolution, a knot, a one-sided band. They share a projector rather than each
// rolling their own, and every one of them is a chain of polylines, which is the
// cheap shape on this display — consecutive segments share an endpoint, so the
// beam never lifts between them.
//
// Everything is integer. Coordinates are held x1000 so a profile can be written
// as decimals without a float appearing anywhere in the frame path.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"

namespace faces { namespace impl {
namespace {

constexpr int kEye = 2600;      // eye distance in device units

struct View { int32_t sy, cy, sp, cp; };

// Two axes at different rates, so a shape tumbles rather than spinning flat.
View spinView(uint16_t t, int yawRate = 2, int pitchRate = 1) {
  const int yaw   = (int)((t * yawRate)   & (vec::kSteps - 1));
  const int pitch = (int)((t * pitchRate) & (vec::kSteps - 1));
  return View{ vec::sinT(yaw), vec::cosT(yaw), vec::sinT(pitch), vec::cosT(pitch) };
}

// Spun about Y with a FIXED tilt. Anything with an up — a teapot — wants to be
// looked down on slightly rather than tumbled: at zero pitch the eye is in the
// plane of every parallel, so each ring collapses to a horizontal line and the
// body reads as a stack of dashes.
View tiltView(uint16_t t, int yawRate, int pitch) {
  const int yaw = (int)((t * yawRate) & (vec::kSteps - 1));
  return View{ vec::sinT(yaw), vec::cosT(yaw), vec::sinT(pitch), vec::cosT(pitch) };
}

// Model space -> screen. Yaw about Y, pitch about X, then a perspective divide.
void project(const View& v, int32_t x, int32_t y, int32_t z, int16_t& ox, int16_t& oy) {
  const int32_t x1 = (x * v.cy - z * v.sy) >> 16;
  const int32_t z1 = (x * v.sy + z * v.cy) >> 16;
  const int32_t y2 = (y * v.cp - z1 * v.sp) >> 16;
  const int32_t z2 = (y * v.sp + z1 * v.cp) >> 16;
  const int32_t den = kEye + z2;              // eye at -kEye, never zero
  ox = (int16_t)((x1 * kEye) / den);
  oy = (int16_t)((y2 * kEye) / den);
}

// A closed or open ring at height y, radius r, as a polygon of n sides.
void ring(DrawList& d, const View& v, int32_t r, int32_t y, int n) {
  int16_t lx = 0, ly = 0;
  for (int i = 0; i <= n; ++i) {
    const int a = (i % n) * vec::kSteps / n;   // i == n wraps, closing the ring
    int16_t px, py;
    project(v, (r * vec::cosT(a)) >> 16, y, (r * vec::sinT(a)) >> 16, px, py);
    if (i) d.line(lx, ly, px, py);
    lx = px; ly = py;
  }
}

// ---- the Utah teapot -------------------------------------------------------
// Not the Bezier patch set — 32 patches is thousands of segments and this list
// holds 192. What makes a teapot recognisable is its silhouette, so the body is
// that profile swept around Y, with the spout and handle as swept outlines.
// Coordinates x1000, bottom to top: the foot, the belly, the shoulder, the rim,
// then the lid and its knob.
const int16_t kPotProfile[][2] = {   // {radius, height}
  {   0, -620}, { 480, -640}, { 700, -520}, { 830, -300}, { 880,  -40},
  { 830,  180}, { 690,  330}, { 500,  420}, { 430,  450}, { 400,  480},
  { 300,  560}, { 150,  620}, { 110,  680}, { 210,  740}, {   0,  810}
};
constexpr int kPotPts = (int)(sizeof(kPotProfile) / sizeof(kPotProfile[0]));

// Spout and handle as pairs of rails. Two curves apiece is all it takes to read
// as a spout, and it costs eight items rather than eighty — but the rails have
// to MEET at the spout's tip, or it draws as two diverging spikes.
// NO leading zeros: in C a leading zero means octal, so a tidily aligned -060 is
// quietly -48, and -0960 does not compile at all. Both happened here while
// padding the columns to line up.
const int16_t kSpoutHi[4][2] = { {760, 100}, {1050, 180}, {1300, 300}, {1450, 460} };
const int16_t kSpoutLo[4][2] = { {790, -160}, {1100, -60}, {1320, 70}, {1430, 270} };
const int16_t kHandleOut[5][2] = {
  {-700, 350}, {-1110, 310}, {-1280, 50}, {-1100, -230}, {-790, -320} };
const int16_t kHandleIn[5][2] = {
  {-690, 215}, {-960, 195}, {-1090, 30}, {-950, -140}, {-760, -205} };

void curve2d(DrawList& d, const View& v, const int16_t (*pts)[2], int n, int32_t scale,
             int16_t* endX = nullptr, int16_t* endY = nullptr) {
  int16_t lx = 0, ly = 0;
  for (int i = 0; i < n; ++i) {
    int16_t px, py;
    project(v, (int32_t)pts[i][0] * scale / 1000, (int32_t)pts[i][1] * scale / 1000, 0, px, py);
    if (i) d.line(lx, ly, px, py);
    lx = px; ly = py;
  }
  if (endX) { *endX = lx; *endY = ly; }
}

} // namespace

void teapot(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  const View v = tiltView(t, 2, 58);     // ~20 degrees down; a teapot has an up
  constexpr int32_t S = 620;             // model units 1.000 -> device
  constexpr int kMerid = 6, kRingSides = 10;

  // Meridians over a FULL turn. Half a turn looks like it should suffice for a
  // surface of revolution, but the view rotates, so the missing half shows as a
  // body that is lopsided from most angles.
  for (int m = 0; m < kMerid; ++m) {
    const int a = m * vec::kSteps / kMerid;
    const int32_t ca = vec::cosT(a), sa = vec::sinT(a);
    int16_t lx = 0, ly = 0;
    for (int i = 0; i < kPotPts; ++i) {
      const int32_t r = (int32_t)kPotProfile[i][0] * S / 1000;
      const int32_t y = (int32_t)kPotProfile[i][1] * S / 1000;
      int16_t px, py;
      project(v, (r * ca) >> 16, y, (r * sa) >> 16, px, py);
      if (i) d.line(lx, ly, px, py);
      lx = px; ly = py;
    }
  }
  // Parallels at the few heights that carry the shape: foot, belly, shoulder,
  // rim and knob.
  const int rows[] = { 2, 4, 6, 10 };
  for (int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); ++i) {
    const int k = rows[i];
    ring(d, v, (int32_t)kPotProfile[k][0] * S / 1000,
               (int32_t)kPotProfile[k][1] * S / 1000, kRingSides);
  }
  int16_t ax, ay, bx, by;
  curve2d(d, v, kSpoutHi, 4, S, &ax, &ay);
  curve2d(d, v, kSpoutLo, 4, S, &bx, &by);
  d.line(ax, ay, bx, by);                  // close the tip
  curve2d(d, v, kHandleOut, 5, S);
  curve2d(d, v, kHandleIn, 5, S);
}

// A globe: meridians and parallels, which is the whole of it.
void sphere(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  const View v = spinView(t, 2, 1);
  constexpr int32_t R = 900;
  constexpr int kMerid = 6, kPar = 5, kSeg = 12;

  for (int m = 0; m < kMerid; ++m) {
    const int a = m * vec::kSteps / (kMerid * 2);
    const int32_t ca = vec::cosT(a), sa = vec::sinT(a);
    int16_t lx = 0, ly = 0;
    for (int i = 0; i <= kSeg; ++i) {
      const int u = i * (vec::kSteps / 2) / kSeg - vec::kSteps / 4;   // pole to pole
      const int32_t r = (R * vec::cosT(u)) >> 16, y = (R * vec::sinT(u)) >> 16;
      int16_t px, py;
      project(v, (r * ca) >> 16, y, (r * sa) >> 16, px, py);
      if (i) d.line(lx, ly, px, py);
      lx = px; ly = py;
    }
  }
  for (int p = 1; p <= kPar; ++p) {
    const int u = p * (vec::kSteps / 2) / (kPar + 1) - vec::kSteps / 4;
    ring(d, v, (R * vec::cosT(u)) >> 16, (R * vec::sinT(u)) >> 16, kSeg);
  }
}

// A trefoil: the simplest knot that is actually knotted, and one closed stroke,
// so the beam never lifts for the whole figure.
void knot(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  const View v = spinView(t, 2, 1);
  constexpr int N = 90, S = 300;
  int16_t lx = 0, ly = 0;
  for (int i = 0; i <= N; ++i) {
    const int u = i * vec::kSteps / N;
    // x = sin u + 2 sin 2u,  y = cos u - 2 cos 2u,  z = -sin 3u
    // Scale BEFORE the Q16 shift: shifting first throws the fraction away and
    // leaves a knot three units across.
    const int32_t x = ((vec::sinT(u) + 2 * vec::sinT(2 * u)) * S) >> 16;
    const int32_t y = ((vec::cosT(u) - 2 * vec::cosT(2 * u)) * S) >> 16;
    const int32_t z = ((-vec::sinT(3 * u)) * S) >> 16;
    int16_t px, py;
    project(v, x, y, z, px, py);
    if (i) d.line(lx, ly, px, py);
    lx = px; ly = py;
  }
}

// A Mobius band: one edge, one side. Drawn as the two rails and a ladder of
// rungs, which is what makes the half twist visible — without the rungs it is
// just two loops.
void mobius(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  const View v = spinView(t, 2, 1);
  constexpr int N = 28;
  constexpr int32_t R = 700, W = 260;

  int16_t la[2] = {0, 0}, lb[2] = {0, 0};
  for (int i = 0; i <= N; ++i) {
    const int u = (i % N) * vec::kSteps / N;
    const int h = u / 2;                    // half twist over a full turn
    const int32_t ch = vec::cosT(h), sh = vec::sinT(h);
    const int32_t cu = vec::cosT(u), su = vec::sinT(u);
    int16_t p[2][2];
    for (int e = 0; e < 2; ++e) {
      const int32_t w = e ? W : -W;
      const int32_t rr = R + ((w * ch) >> 16);
      project(v, (rr * cu) >> 16, (w * sh) >> 16, (rr * su) >> 16, p[e][0], p[e][1]);
    }
    if (i) {
      d.line(la[0], la[1], p[0][0], p[0][1]);
      d.line(lb[0], lb[1], p[1][0], p[1][1]);
      if ((i & 1) == 0) d.line(p[0][0], p[0][1], p[1][0], p[1][1]);   // rung
    }
    la[0] = p[0][0]; la[1] = p[0][1];
    lb[0] = p[1][0]; lb[1] = p[1][1];
  }
}

// Two helices and their rungs. The rungs are the point: two bare helices read as
// a pair of wavy lines until something ties them together.
void helix(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  const View v = spinView(t, 2, 0);
  constexpr int N = 34;
  constexpr int32_t R = 420, H = 1500;      // radius and total height
  const int drift = (int)(t * 4);           // the whole thing turns on its axis

  int16_t la[2] = {0, 0}, lb[2] = {0, 0};
  for (int i = 0; i <= N; ++i) {
    const int u = i * (vec::kSteps * 2) / N + drift;    // two full turns
    const int32_t y = (int32_t)i * H / N - H / 2;
    int16_t p[2][2];
    for (int s = 0; s < 2; ++s) {
      const int a = u + (s ? vec::kSteps / 2 : 0);      // strands half a turn apart
      project(v, (R * vec::cosT(a)) >> 16, y, (R * vec::sinT(a)) >> 16, p[s][0], p[s][1]);
    }
    if (i) {
      d.line(la[0], la[1], p[0][0], p[0][1]);
      d.line(lb[0], lb[1], p[1][0], p[1][1]);
      if (i % 3 == 0) d.line(p[0][0], p[0][1], p[1][0], p[1][1]);
    }
    la[0] = p[0][0]; la[1] = p[0][1];
    lb[0] = p[1][0]; lb[1] = p[1][1];
  }
}

}}  // namespace faces::impl
