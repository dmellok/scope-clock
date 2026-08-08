// faces_3d.cpp — rotating wireframes.
//
// All integer: vertices are stored x100 so the golden ratio the icosahedron and
// dodecahedron are built from is just 162, and rotation runs off the same Q16
// sin/cos tables the beam stepper uses. Twenty vertices through two rotations
// and a perspective divide costs well under a millisecond, which matters
// because this runs inside the refresh.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"

namespace faces { namespace impl {
namespace {

constexpr int kFocal = 2400;      // eye distance; smaller = stronger perspective
constexpr int16_t PHI = 162;      // golden ratio, x100
constexpr int16_t IPHI = 62;      // its reciprocal

struct Solid {
  const int16_t (*v)[3];
  uint8_t nv;
  int16_t half;                   // display units per unit of model space
};

// Two axes at different rates, so everything tumbles rather than spinning flat.
// ~8.5s per yaw turn at 60Hz.
void spin(int& yaw, int& pitch) {
  static uint16_t t = 0;
  ++t;
  yaw   = (int)((t * 2) & (vec::kSteps - 1));
  pitch = (int)( t      & (vec::kSteps - 1));
}

// Edges are derived rather than typed in: every pair of vertices at the minimum
// separation is an edge, which is what "edge" means for these solids. Entering
// thirty pairs by hand for a dodecahedron is a transcription error waiting to
// happen, and this cannot disagree with the vertex table.
void drawSolid(const Solid& s, DrawList& d, int yaw, int pitch) {
  const int32_t sy = vec::sinT(yaw),   cy = vec::cosT(yaw);
  const int32_t sp = vec::sinT(pitch), cp = vec::cosT(pitch);

  int16_t px[20], py[20];
  for (uint8_t i = 0; i < s.nv; ++i) {
    const int32_t x0 = (int32_t)s.v[i][0] * s.half / 100;
    const int32_t y0 = (int32_t)s.v[i][1] * s.half / 100;
    const int32_t z0 = (int32_t)s.v[i][2] * s.half / 100;
    const int32_t x1 = (x0 * cy - z0 * sy) >> 16;
    const int32_t z1 = (x0 * sy + z0 * cy) >> 16;
    const int32_t y2 = (y0 * cp - z1 * sp) >> 16;
    const int32_t z2 = (y0 * sp + z1 * cp) >> 16;
    const int32_t den = kFocal + z2;          // eye at -kFocal, never zero
    px[i] = (int16_t)((x1 * kFocal) / den);
    py[i] = (int16_t)((y2 * kFocal) / den);
  }

  int32_t best = 0x7fffffff;
  for (uint8_t a = 0; a < s.nv; ++a)
    for (uint8_t b = (uint8_t)(a + 1); b < s.nv; ++b) {
      const int32_t dx = s.v[a][0]-s.v[b][0], dy = s.v[a][1]-s.v[b][1],
                    dz = s.v[a][2]-s.v[b][2];
      const int32_t q = dx*dx + dy*dy + dz*dz;
      if (q && q < best) best = q;
    }
  const int32_t lim = best + best / 8;        // slack for the integer phi

  for (uint8_t a = 0; a < s.nv; ++a)
    for (uint8_t b = (uint8_t)(a + 1); b < s.nv; ++b) {
      const int32_t dx = s.v[a][0]-s.v[b][0], dy = s.v[a][1]-s.v[b][1],
                    dz = s.v[a][2]-s.v[b][2];
      if (dx*dx + dy*dy + dz*dz <= lim) d.line(px[a], py[a], px[b], py[b]);
    }
}

const int16_t kTetraV[4][3] = {
  {100,100,100},{100,-100,-100},{-100,100,-100},{-100,-100,100}};
const int16_t kOctaV[6][3] = {
  {100,0,0},{-100,0,0},{0,100,0},{0,-100,0},{0,0,100},{0,0,-100}};
const int16_t kCubeV[8][3] = {
  {-100,-100,-100},{100,-100,-100},{100,100,-100},{-100,100,-100},
  {-100,-100,100},{100,-100,100},{100,100,100},{-100,100,100}};
const int16_t kIcosaV[12][3] = {
  {0,100,PHI},{0,-100,PHI},{0,100,-PHI},{0,-100,-PHI},
  {100,PHI,0},{-100,PHI,0},{100,-PHI,0},{-100,-PHI,0},
  {PHI,0,100},{PHI,0,-100},{-PHI,0,100},{-PHI,0,-100}};
const int16_t kDodecaV[20][3] = {
  {100,100,100},{100,100,-100},{100,-100,100},{100,-100,-100},
  {-100,100,100},{-100,100,-100},{-100,-100,100},{-100,-100,-100},
  {0,IPHI,PHI},{0,IPHI,-PHI},{0,-IPHI,PHI},{0,-IPHI,-PHI},
  {IPHI,PHI,0},{IPHI,-PHI,0},{-IPHI,PHI,0},{-IPHI,-PHI,0},
  {PHI,0,IPHI},{PHI,0,-IPHI},{-PHI,0,IPHI},{-PHI,0,-IPHI}};

// Sized by measuring the projected extent in the host sim, not by geometry:
// perspective magnifies whichever vertex is nearest the eye, so the on-screen
// radius is well above the model radius and differs per solid. Each of these
// peaks near +/-1000 across a full tumble.
const Solid kTetra {kTetraV, 4, 620};
const Solid kOcta  {kOctaV, 6, 1000};
const Solid kCube  {kCubeV, 8, 560};
const Solid kIcosa {kIcosaV, 12, 520};
const Solid kDodeca{kDodecaV, 20, 560};

} // namespace

void tetra (const ClockState&, DrawList& d){int y,p;spin(y,p);drawSolid(kTetra ,d,y,p);}
void octa  (const ClockState&, DrawList& d){int y,p;spin(y,p);drawSolid(kOcta  ,d,y,p);}
void cube  (const ClockState&, DrawList& d){int y,p;spin(y,p);drawSolid(kCube  ,d,y,p);}
void icosa (const ClockState&, DrawList& d){int y,p;spin(y,p);drawSolid(kIcosa ,d,y,p);}
void dodeca(const ClockState&, DrawList& d){int y,p;spin(y,p);drawSolid(kDodeca,d,y,p);}

// A hypercube: rotate in 4D, project to 3D, then to 2D. The inner cube turning
// itself inside out is the thing a raster display cannot really sell, and the
// reason this shape belongs on a vector tube.
void tesseract(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  const int a4 = (int)((t * 2) & (vec::kSteps - 1));   // XW plane
  const int a3 = (int)( t      & (vec::kSteps - 1));   // YZ plane
  const int32_t s4 = vec::sinT(a4), c4 = vec::cosT(a4);
  const int32_t s3 = vec::sinT(a3), c3 = vec::cosT(a3);

  constexpr int H = 340, WEYE = 950, EYE = 2400;
  int16_t px[16], py[16];
  for (int i = 0; i < 16; ++i) {
    int32_t x = (i & 1) ? H : -H, y = (i & 2) ? H : -H;
    int32_t z = (i & 4) ? H : -H, w = (i & 8) ? H : -H;
    int32_t x1 = (x * c4 - w * s4) >> 16;
    const int32_t w1 = (x * s4 + w * c4) >> 16;
    int32_t y1 = (y * c3 - z * s3) >> 16;
    int32_t z1 = (y * s3 + z * c3) >> 16;
    const int32_t k = (int32_t)WEYE * 256 / (WEYE + w1);      // 4D -> 3D
    x1 = (x1 * k) >> 8; y1 = (y1 * k) >> 8; z1 = (z1 * k) >> 8;
    const int32_t den = EYE + z1;                            // 3D -> 2D
    px[i] = (int16_t)(x1 * EYE / den);
    py[i] = (int16_t)(y1 * EYE / den);
  }
  // Two vertices are joined exactly when they differ in one coordinate, i.e.
  // their indices differ by a single bit. 32 edges, no table needed.
  for (int a = 0; a < 16; ++a)
    for (int b = a + 1; b < 16; ++b) {
      const int diff = a ^ b;
      if (!(diff & (diff - 1))) d.line(px[a], py[a], px[b], py[b]);
    }
}

// Rings of rings. BOTH families are drawn — cross-sections around the tube and
// longitudes around the hole — because either one alone reads as a scatter of
// unrelated ellipses rather than a surface. The moire where they cross is the
// whole point, and it is something the beam draws for free.
void torus(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  const int yaw  = (int)((t * 2) & (vec::kSteps - 1));
  const int tilt = (int)( t      & (vec::kSteps - 1));
  const int32_t sy = vec::sinT(yaw),  cyw = vec::cosT(yaw);
  const int32_t st = vec::sinT(tilt), ct  = vec::cosT(tilt);

  constexpr int R = 700, r = 270, NU = 6, NV = 6, STEP = 10, EYE = 2600;

  // u goes around the hole, v around the tube. Project the surface point for
  // whichever pair the caller asks about; the two loops below differ only in
  // which one they hold fixed.
  auto project = [&](int u, int v, int16_t& ox, int16_t& oy) {
    const int32_t rr = R + ((r * vec::cosT(v)) >> 16);
    const int32_t x = (rr * vec::cosT(u)) >> 16;
    const int32_t y = (r  * vec::sinT(v)) >> 16;
    const int32_t z = (rr * vec::sinT(u)) >> 16;
    const int32_t x1 = (x * cyw - z * sy) >> 16;
    const int32_t z1 = (x * sy  + z * cyw) >> 16;
    const int32_t y2 = (y * ct - z1 * st) >> 16;
    const int32_t z2 = (y * st + z1 * ct) >> 16;
    const int32_t den = EYE + z2;
    ox = (int16_t)(x1 * EYE / den);
    oy = (int16_t)(y2 * EYE / den);
  };

  for (int i = 0; i < NU; ++i) {                    // cross-sections
    const int u = i * vec::kSteps / NU;
    int16_t lx = 0, ly = 0;
    for (int j = 0; j <= STEP; ++j) {
      int16_t x, y; project(u, j * vec::kSteps / STEP, x, y);
      if (j) d.line(lx, ly, x, y);
      lx = x; ly = y;
    }
  }
  for (int i = 0; i < NV; ++i) {                    // longitudes
    const int v = i * vec::kSteps / NV;
    int16_t lx = 0, ly = 0;
    for (int j = 0; j <= STEP; ++j) {
      int16_t x, y; project(j * vec::kSteps / STEP, v, x, y);
      if (j) d.line(lx, ly, x, y);
      lx = x; ly = y;
    }
  }
}

}}  // namespace faces::impl
