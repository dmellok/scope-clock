// faces_gen.cpp — generative animations: the things a scope is famous for.
//
// These are all parametric curves drawn as a chain of short segments. The chain
// matters: consecutive segments share an endpoint, so the beam does not travel
// between them and the blanked hop costs nothing but the fixed settling delay.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include <math.h>

namespace faces { namespace impl {
namespace {

constexpr int kMask = vec::kSteps - 1;

// Walk a parametric curve, emitting one line per step. `f` returns a point for
// a parameter in table units.
template <typename F>
void curve(DrawList& d, int segs, F f) {
  int px = 0, py = 0;
  for (int i = 0; i <= segs; ++i) {
    int x, y;
    f((i * vec::kSteps) / segs, x, y);
    if (i) d.line(px, py, x, y);
    px = x; py = y;
  }
}

uint32_t rng = 0x1234567u;
inline uint32_t xr() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

} // namespace

// The figure an oscilloscope makes from two sines, and the reason anyone points
// a camera at one. A drifting phase makes it fold through itself rather than
// sit still.
void lissajous(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  const int phase = (int)(t * 3);
  curve(d, 96, [&](int u, int& x, int& y) {
    x = (int)((1080 * vec::sinT(3 * u + phase)) >> 16);
    y = (int)((1080 * vec::cosT(4 * u)) >> 16);
  });
}

// Two damped pendulums per axis — a real harmonograph. The decay is what makes
// it a drawing rather than a loop: the figure spirals inward, and when it has
// collapsed the parameters are rerolled and it starts a new one.
void harmonograph(const ClockState&, DrawList& d) {
  static uint16_t age = 0;
  static int f1 = 2, f2 = 3, f3 = 3, f4 = 2, ph = 0;
  if (age == 0) {                          // new figure
    f1 = 1 + (int)(xr() % 4); f2 = 1 + (int)(xr() % 5);
    f3 = 1 + (int)(xr() % 4); f4 = 1 + (int)(xr() % 5);
    ph = (int)(xr() & kMask);
  }
  if (++age > 900) age = 0;                // ~15s per drawing at 60Hz

  // Amplitude decays along the curve, not over time, so the whole spiral is on
  // screen at once and simply breathes as the phase advances.
  const int drift = (int)(age * 2);
  curve(d, 140, [&](int u, int& x, int& y) {
    const int32_t decay = 255 - (int32_t)(u * 150 / vec::kSteps);   // 255 -> 105
    const int32_t a = (620 * decay) >> 8, b = (560 * decay) >> 8;
    x = (int)(((a * vec::sinT(f1 * u + drift)) >> 16) +
              ((b * vec::sinT(f2 * u + ph)) >> 16));
    y = (int)(((a * vec::cosT(f3 * u)) >> 16) +
              ((b * vec::cosT(f4 * u + drift)) >> 16));
  });
}

// Hypotrochoid: a pen in a small gear rolling inside a big one. The ratio walks
// slowly, so the figure keeps reorganising into new symmetries.
void spirograph(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  const int k = 2 + (int)((t / 420) % 6);      // inner gear, changes every ~7s
  const int R = 780, r = R / (k + 1), pen = r + 260;
  curve(d, 150, [&](int u, int& x, int& y) {
    const int32_t big = R - r;
    const int a = u, b = (int)((int32_t)u * big / r);
    x = (int)(((big * vec::cosT(a)) >> 16) + ((pen * vec::cosT(b + t)) >> 16));
    y = (int)(((big * vec::sinT(a)) >> 16) - ((pen * vec::sinT(b + t)) >> 16));
  });
}

// r = A cos(k*theta). Petals bloom, split and reorganise as k walks.
void rose(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  const int k = 2 + (int)((t / 480) % 7);
  const int spin = (int)(t / 2);
  curve(d, 132, [&](int u, int& x, int& y) {
    const int32_t r = (1080 * vec::cosT(k * u)) >> 16;    // signed: petals cross
    x = (int)((r * vec::cosT(u + spin)) >> 16);
    y = (int)((r * vec::sinT(u + spin)) >> 16);
  });
}

// The Lorenz attractor, drawn as a trail the head keeps extending.
//
// The only float in the whole firmware, and it earns it: the MK66 has a
// single-precision FPU, and this is a handful of operations per frame against
// integer maths that would need careful scaling to stay stable.
void lorenz(const ClockState&, DrawList& d) {
  constexpr int N = 150;
  static float px[N], py[N];
  static int head = 0, filled = 0;
  static float x = 0.1f, y = 0.0f, z = 0.0f;

  // A few steps per frame: enough that the head visibly moves, few enough that
  // the trail stays a curve rather than a scribble.
  for (int s = 0; s < 3; ++s) {
    constexpr float dt = 0.006f, sg = 10.0f, rh = 28.0f, be = 8.0f / 3.0f;
    const float dx = sg * (y - x);
    const float dy = x * (rh - z) - y;
    const float dz = x * y - be * z;
    x += dx * dt; y += dy * dt; z += dz * dt;
  }
  px[head] = x * 46.0f;             // the attractor spans roughly +/-20
  py[head] = (z - 27.0f) * 46.0f;   // centre the z range on the screen
  head = (head + 1) % N;
  if (filled < N) ++filled;

  int lx = 0, ly = 0;
  for (int i = 0; i < filled; ++i) {
    const int idx = (head + N - filled + i) % N;
    const int cx = (int)px[idx], cy = (int)py[idx];
    if (i) d.line(lx, ly, cx, cy);
    lx = cx; ly = cy;
  }
}

// {n/k} star polygons: n points on a circle, joined every k-th.
//
// n and k must be coprime, or the figure falls apart into gcd(n,k) separate
// polygons — {6/2} is two triangles, not a hexagram, and reads as a mistake.
// The valid pairs are listed rather than searched, which is both smaller than a
// gcd loop and lets the sequence be ordered by how good each one looks.
void starpoly(const ClockState&, DrawList& d) {
  static const uint8_t kStars[][2] = {
    {5,2},{7,2},{7,3},{8,3},{9,2},{9,4},{11,3},{11,4},{11,5},{12,5},{13,5}
  };
  constexpr int kCount = (int)(sizeof(kStars) / sizeof(kStars[0]));
  static uint16_t t = 0;
  ++t;
  const int idx = (int)((t / 360) % kCount);       // a new star every ~6s
  const int n = kStars[idx][0], k = kStars[idx][1];
  const int spin = (int)(t / 3);
  const int R = 1000;

  // One closed stroke: stepping by k lands back on the start after exactly n
  // hops, so the beam never lifts.
  int lx = 0, ly = 0;
  for (int i = 0; i <= n; ++i) {
    const int a = ((i * k % n) * vec::kSteps) / n + spin;
    const int x = (int)((R * vec::sinT(a)) >> 16);
    const int y = (int)((R * vec::cosT(a)) >> 16);
    if (i) d.line(lx, ly, x, y);
    lx = x; ly = y;
  }
}

// Each star is the streak between where it was and where it is — both how it
// reads as motion, and how you draw a point on a display that only knows lines.
//
// Sparse is dim: brightness is beam-on time per refresh, so a few short ticks
// leave the tube idle however long the dwell gets. Hence many stars, and a tail
// longer than one frame's travel, decoupled from speed so brightening the field
// does not make everything rush past.
void starfield(const ClockState&, DrawList& d) {
  constexpr int N = 64, FOCAL = 700, SPEED = 30, TAIL = 150;
  constexpr int NEAR = 260, FAR = 2600, EDGE = 1180;
  static int32_t sx[N], sy[N], sz[N];
  static bool seeded = false;

  auto respawn = [&](int i) {
    sx[i] = (int32_t)(xr() % 2401) - 1200;
    sy[i] = (int32_t)(xr() % 2401) - 1200;
    sz[i] = NEAR + (int32_t)(xr() % (FAR - NEAR));
  };
  if (!seeded) { for (int i = 0; i < N; ++i) respawn(i); seeded = true; }

  for (int i = 0; i < N; ++i) {
    sz[i] -= SPEED;
    if (sz[i] <= NEAR) { respawn(i); continue; }
    const int32_t z1 = sz[i], z0 = z1 + TAIL;
    const int x0 = (int)(sx[i] * FOCAL / z0), y0 = (int)(sy[i] * FOCAL / z0);
    const int x1 = (int)(sx[i] * FOCAL / z1), y1 = (int)(sy[i] * FOCAL / z1);
    // Off the edge means it has passed the viewer. Clamping would smear it
    // along the border, which reads as a bug rather than a star.
    if (x1 < -EDGE || x1 > EDGE || y1 < -EDGE || y1 > EDGE) { respawn(i); continue; }
    d.line(x0, y0, x1, y1);
  }
}

// Concentric polygons receding, each twisted a little further than the last —
// the structured cousin of the starfield.
void tunnel(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  constexpr int RINGS = 8, SIDES = 6, EYE = 900;
  for (int r = 0; r < RINGS; ++r) {
    // Depth cycles so rings stream toward the viewer and recycle at the back.
    const int32_t z = 300 + ((r * 340 + (int32_t)(t * 8)) % (RINGS * 340));
    const int32_t rad = (int32_t)620 * EYE / z;
    // Past us at one end, and at the other a knot of overlapping hexagons a few
    // dots across that costs beam time and reads as a smudge.
    if (rad > 1150 || rad < 130) continue;
    const int twist = (int)((z / 3) + t * 2);
    int lx = 0, ly = 0;
    for (int i = 0; i <= SIDES; ++i) {
      const int a = (i * vec::kSteps) / SIDES + twist;
      const int x = (int)((rad * vec::sinT(a)) >> 16);
      const int y = (int)((rad * vec::cosT(a)) >> 16);
      if (i) d.line(lx, ly, x, y);
      lx = x; ly = y;
    }
  }
}

// Digital rain. The one effect everybody asks a green phosphor tube for, and it
// suits this one: no colour is needed because the depth cue that matters on a
// CRT is redraw count, so the leading glyph is simply drawn twice and burns
// brighter than its tail exactly as it should.
//
// Cost is the constraint. kLineStride is 1, so a dot lands on every unit of beam
// travel and a glyph costs roughly its own perimeter — a few hundred dots. Nine
// columns of six is about as much rain as the frame budget will carry; the sim
// is what settled those two numbers.
void matrix(const ClockState&, DrawList& d) {
  constexpr int COLS = 12, TRAIL = 7, SCALE = 6;
  constexpr int CELL = SCALE * 20 + 26;      // glyph height plus a little air
  // TOP bounds the BASELINE, and ink runs upward from there by the cell height,
  // so the limit is the edge less one glyph — at 1150 the tops of the highest
  // characters were being clipped off at 1270. The edge in question is the
  // ±1200 working one a FACE must respect, not the ±1250 an overlay may use.
  constexpr int TOP = 1200 - SCALE * 20 - 8, BOT = -1195;
  // Katakana is what the film used and this font has none, so: digits and the
  // angular half of the alphabet, which keeps the texture busy and legible.
  static const char kGlyphs[] = "0123456789ABCDEFHJKLMNPRSTVWXYZ*+=<>/\\|";
  constexpr int NG = (int)(sizeof(kGlyphs) - 1);

  static int16_t headY[COLS], speed[COLS];
  static char    cell[COLS][TRAIL][2];
  static bool    seeded = false;

  auto respawn = [&](int c, bool stagger) {
    // Staggered on the first frame only, so the rain does not start as one
    // horizontal rank marching down the screen together.
    headY[c] = (int16_t)(TOP + (stagger ? (int)(xr() % 2600) : (int)(xr() % 600)));
    speed[c] = (int16_t)(9 + (int)(xr() % 22));
    for (int r = 0; r < TRAIL; ++r) {
      cell[c][r][0] = kGlyphs[xr() % NG];
      cell[c][r][1] = '\0';
    }
  };
  if (!seeded) { for (int c = 0; c < COLS; ++c) respawn(c, true); seeded = true; }

  for (int c = 0; c < COLS; ++c) {
    headY[c] = (int16_t)(headY[c] - speed[c]);
    // Gone once the whole trail has cleared the bottom edge.
    if (headY[c] + TRAIL * CELL < BOT) { respawn(c, false); continue; }

    // One glyph somewhere in the column flickers to something else each frame.
    // That shimmer is most of what sells the effect, and it costs nothing.
    if ((xr() & 3) == 0) {
      const int r = (int)(xr() % TRAIL);
      cell[c][r][0] = kGlyphs[xr() % NG];
    }

    const int x = -1080 + c * (2160 / (COLS - 1));
    for (int r = 0; r < TRAIL; ++r) {
      const int y = headY[c] + r * CELL;      // r 0 is the head, at the bottom
      if (y < BOT || y > TOP) continue;
      d.text(x, y, SCALE, cell[c][r]);
      if (r == 0) d.text(x, y, SCALE, cell[c][r]);   // the head burns brighter
    }
  }
}

}}  // namespace faces::impl
