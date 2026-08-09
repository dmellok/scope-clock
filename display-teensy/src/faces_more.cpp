// faces_more.cpp — faces that compute themselves from the RTC or from nothing.
//
// The solar system and the moon are real: both derive from the date, so they are
// right without anyone telling them anything, which is the same reason the clock
// keeps its own RTC. Pong and Life are simulations that need no input at all.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"
#include "hostdata.h"
#include <Arduino.h>
#include <stdio.h>

namespace faces { namespace impl {
namespace {

uint32_t rngM = 0x9e3779b9u;
inline uint32_t xrM() { rngM ^= rngM << 13; rngM ^= rngM >> 17; rngM ^= rngM << 5; return rngM; }

// Days since 2000-01-01, by Howard Hinnant's days-from-civil. Exact, no tables,
// no leap-year special cases to get wrong.
int32_t daysSince2000(const ClockState& c) {
  int32_t y = 2000 + c.year, m = c.month, d = c.day;
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy = (uint32_t)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 730485;      // 730485 = 2000-01-01
}

// A ring as a polyline. d.circle() would do, but a polyline lets the segment
// count fall with the radius, and eight orbits at full resolution is most of a
// frame spent on empty circles.
void ringPoly(DrawList& d, int32_t r, int segs) {
  int lx = 0, ly = 0;
  for (int i = 0; i <= segs; ++i) {
    const int a = (i % segs) * vec::kSteps / segs;
    const int x = (int)((r * vec::cosT(a)) >> 16);
    const int y = (int)((r * vec::sinT(a)) >> 16);
    if (i) d.line(lx, ly, x, y);
    lx = x; ly = y;
  }
}

} // namespace

// ---- the solar system ------------------------------------------------------
// Orbits to scale in ANGLE, not in distance: real spacing puts Mercury inside
// the sun's own circle and Neptune thirty times further out than Saturn, which
// on a 1200-unit radius is four invisible dots and one ring. The periods are
// real, so the planets line up when the planets line up.
void solar(const ClockState& c, DrawList& d) {
  struct Planet { int16_t r; uint16_t periodD; uint8_t size; };
  static const Planet kP[] = {
    { 210,   88, 22}, { 320,  225, 30}, { 430,  365, 32}, { 545,  687, 26},
    { 690, 4333, 52}, { 840,10759, 46}, { 980,30687, 34}, {1110,60190, 32},
  };
  constexpr int kN = (int)(sizeof(kP) / sizeof(kP[0]));

  const int32_t day = daysSince2000(c);
  // Sub-day motion, so the inner planets visibly move rather than stepping once
  // a day: seconds through the day, as a fraction.
  const int32_t secs = (int32_t)c.hour * 3600 + c.minute * 60 + c.second;

  d.circle(0, 0, 90);
  d.circle(0, 0, 90);                            // the sun, drawn twice

  for (int i = 0; i < kN; ++i) {
    // Segments fall with radius: an inner orbit needs far fewer to look round.
    // Segments fall with radius, and sparsely: eight full rings is most of a
    // frame, and an orbit is a guide line rather than the subject.
    // Not finer than this: at 12 + r/70 the orbits are smooth and the face is
    // 184 items, and a notification on top would push it past the 192 the list
    // holds and be silently dropped.
    ringPoly(d, kP[i].r, 10 + kP[i].r / 110);
    // 64-bit throughout: Neptune's 30687-day period times 86400 overflows a
    // 32-bit int, which UBSan caught and which would have shown up as a planet
    // in the wrong place rather than as a crash.
    const int64_t p = (int64_t)kP[i].periodD * 86400;
    const int64_t t = (int64_t)day * 86400 + secs;
    const int a = (int)((t * vec::kSteps / p) & (vec::kSteps - 1));
    const int x = (int)((kP[i].r * vec::cosT(a)) >> 16);
    const int y = (int)((kP[i].r * vec::sinT(a)) >> 16);
    d.circle(x, y, kP[i].size);
    d.circle(x, y, kP[i].size);                  // twice: a planet is not a hole
  }
}

// ---- the moon --------------------------------------------------------------
// The terminator is an ellipse, not an arc: the lit edge of a sphere seen from
// an angle projects to a half-ellipse whose width is the cosine of the phase.
// That is why a gibbous moon bulges and a crescent is thin but still round at
// both ends — an arc would draw a lens, which is the wrong shape entirely.
void moon(const ClockState& c, DrawList& d) {
  constexpr int32_t R = 780;
  // 2000-01-06 18:14 UTC was a new moon; the synodic month is 29.530588 days.
  // Scaled by 1000 to stay in integers: age in thousandths of a cycle.
  const int32_t day = daysSince2000(c);
  const int32_t mins = (int32_t)c.hour * 60 + c.minute;
  const int64_t since = (int64_t)(day - 6) * 1440 + mins - 1094;   // minutes
  int32_t phase = (int32_t)(((since % 42524) + 42524) % 42524);    // 29.5306 d
  const int a = (int)((int64_t)phase * vec::kSteps / 42524);       // 0..kSteps

  d.circle(0, 0, R);                              // the limb

  // Terminator: x radius is R*cos(phase). Negative means the bulge is the other
  // way round, which is exactly what waxing versus waning looks like.
  const int32_t xr = (R * vec::cosT(a)) >> 16;
  int lx = 0, ly = 0;
  constexpr int kSeg = 28;
  for (int i = 0; i <= kSeg; ++i) {
    const int u = i * (vec::kSteps / 2) / kSeg - vec::kSteps / 4;  // -90..+90
    const int x = (int)((xr * vec::cosT(u)) >> 16);
    const int y = (int)((R  * vec::sinT(u)) >> 16);
    if (i) d.line(lx, ly, x, y);
    lx = x; ly = y;
  }

  // Illuminated fraction is (1 - cos)/2, which needs no trigonometry beyond the
  // cosine already computed.
  const int lit = (int)((65536 - vec::cosT(a)) * 100 / 131072);
  static const char* const kName[8] = {
    "NEW", "WAXING CRESCENT", "FIRST QUARTER", "WAXING GIBBOUS",
    "FULL", "WANING GIBBOUS", "LAST QUARTER", "WANING CRESCENT"
  };
  static char line[40];
  snprintf(line, sizeof line, "%s  %d%%", kName[(a * 8 / vec::kSteps + 8) % 8], lit);
  d.text(-txt::inkWidth(7, line) / 2, -1120, 7, line);
}

// ---- pong ------------------------------------------------------------------
// The original firmware had it, and a scope is where the game was born. It plays
// itself: each paddle tracks the ball with a lag and a deliberate aiming error,
// which is what keeps a rally going instead of producing a perfect stalemate.
void pong(const ClockState&, DrawList& d) {
  constexpr int32_t W = 1020, H = 780;            // half court
  constexpr int32_t PAD = 170, PADX = W - 40;
  static int32_t bx = 0, by = 0, vx = 17, vy = 11;
  static int32_t p1 = 0, p2 = 0;
  static uint8_t s1 = 0, s2 = 0;
  static int32_t err1 = 0, err2 = 0;

  bx += vx; by += vy;
  if (by > H - 20 || by < -H + 20) { vy = -vy; by += vy; }

  // Paddles chase the ball, but only the one it is heading towards hurries.
  const int32_t want1 = (vx < 0) ? by + err1 : 0;
  const int32_t want2 = (vx > 0) ? by + err2 : 0;
  p1 += (want1 - p1) / 7;
  p2 += (want2 - p2) / 7;
  if (p1 >  H - PAD) p1 =  H - PAD;
  if (p1 < -H + PAD) p1 = -H + PAD;
  if (p2 >  H - PAD) p2 =  H - PAD;
  if (p2 < -H + PAD) p2 = -H + PAD;

  auto serve = [&](int dir) {
    bx = 0; by = 0; vx = dir * (15 + (int)(xrM() % 6)); vy = 8 + (int)(xrM() % 9);
    if (xrM() & 1) vy = -vy;
    // A fresh aiming error each rally: too small and nobody ever scores, too
    // large and the paddles flail.
    err1 = (int32_t)(xrM() % 220) - 110;
    err2 = (int32_t)(xrM() % 220) - 110;
  };
  if (bx > PADX - 20) {
    if (by > p2 - PAD && by < p2 + PAD) { vx = -vx; bx = PADX - 20; }
    else { s1 = (uint8_t)((s1 + 1) % 100); serve(-1); }
  } else if (bx < -PADX + 20) {
    if (by > p1 - PAD && by < p1 + PAD) { vx = -vx; bx = -PADX + 20; }
    else { s2 = (uint8_t)((s2 + 1) % 100); serve(1); }
  }

  d.line(-W, H, W, H);
  d.line(-W, -H, W, -H);
  for (int y = -H; y < H; y += 180) d.line(0, y, 0, y + 90);   // the net
  d.line(-PADX, p1 - PAD, -PADX, p1 + PAD);
  d.line( PADX, p2 - PAD,  PADX, p2 + PAD);
  d.circle((int)bx, (int)by, 26);
  d.circle((int)bx, (int)by, 26);

  static char sc[12];
  snprintf(sc, sizeof sc, "%u   %u", (unsigned)s1, (unsigned)s2);
  d.text(-txt::inkWidth(10, sc) / 2, H + 90, 10, sc);
}

// ---- Conway's Life ---------------------------------------------------------
// Live cells are drawn as horizontal RUNS, not as individual cells: a glider is
// five items either way, but a soup of 90 cells is 90 items as dots and about 35
// as runs, and the list holds 192.
void life(const ClockState&, DrawList& d) {
  constexpr int W = 22, H = 22;
  constexpr int32_t CELL = 96;                    // device units per cell
  static uint8_t a[W * H], b[W * H];
  static bool seeded = false;
  static uint16_t t = 0, age = 0;

  auto reseed = [&]() {
    for (int i = 0; i < W * H; ++i) a[i] = (xrM() & 3) == 0;
    age = 0;
  };
  if (!seeded) { reseed(); seeded = true; }

  // Two frames a step: fast enough to watch, slow enough to follow.
  if (++t >= 12) {
    t = 0;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        int n = 0;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            // Wrapped edges, so gliders leave one side and arrive at the other
            // instead of dissolving against a wall.
            const int nx = (x + dx + W) % W, ny = (y + dy + H) % H;
            n += a[ny * W + nx];
          }
        const uint8_t alive = a[y * W + x];
        b[y * W + x] = (alive && (n == 2 || n == 3)) || (!alive && n == 3);
      }
    for (int i = 0; i < W * H; ++i) a[i] = b[i];
    // Soups settle into still lifes and blinkers within a couple of hundred
    // generations, and a frozen board is not an animation.
    if (++age > 260) reseed();
  }

  const int32_t ox = -(W * CELL) / 2 + CELL / 2, oy = -(H * CELL) / 2 + CELL / 2;
  for (int y = 0; y < H; ++y) {
    int run = -1;
    for (int x = 0; x <= W; ++x) {
      const bool on = x < W && a[y * W + x];
      if (on && run < 0) run = x;
      if (!on && run >= 0) {
        const int32_t yy = oy + y * CELL;
        d.line(ox + run * CELL - CELL / 3, yy,
               ox + (x - 1) * CELL + CELL / 3, yy);
        run = -1;
      }
    }
  }
}

// ---- clock with a phosphor trail -------------------------------------------
// The second hand leaves a wake: a tapering line of marks behind it, longest and
// brightest at the hand and dying away over the previous few seconds. On a real
// phosphor this is what a fast stroke does anyway; drawing it deliberately makes
// it visible at 60Hz, where the tube's own decay is far too quick.
void trailclock(const ClockState& c, DrawList& d) {
  constexpr int32_t R = 1080;
  for (int i = 0; i < 12; ++i) {                  // hour marks
    const int a = i * vec::kSteps / 12;
    const int32_t s = vec::sinT(a), k = vec::cosT(a);
    d.line((int)(((R - 90) * s) >> 16), (int)(((R - 90) * k) >> 16),
           (int)((R * s) >> 16), (int)((R * k) >> 16));
  }

  // The wake, oldest first so the newest marks are drawn last and overlap.
  constexpr int kTrail = 9;
  for (int i = kTrail; i >= 1; --i) {
    const int sec = (c.second - i + 60) % 60;
    const int a = (sec * 4 * vec::kSteps / 240) % vec::kSteps;
    const int32_t len = 60 + (int32_t)(kTrail - i) * 22;    // tapers away
    const int32_t r0 = R - 130, r1 = r0 - len;
    d.line((int)((r0 * vec::sinT(a)) >> 16), (int)((r0 * vec::cosT(a)) >> 16),
           (int)((r1 * vec::sinT(a)) >> 16), (int)((r1 * vec::cosT(a)) >> 16));
  }

  const int minA = c.second / 15 + c.minute * 4;
  const int hrA  = (c.hour % 12) * 20 + c.minute / 3;
  auto hand = [&](int len, int angle240) {
    const int a = (angle240 * vec::kSteps / 240) % vec::kSteps;
    d.line(0, 0, (int)((len * vec::sinT(a)) >> 16), (int)((len * vec::cosT(a)) >> 16));
  };
  hand(950, c.second * 4);
  hand(820, minA); hand(600, hrA);
  hand(820, minA); hand(600, hrA);                // twice: hour and minute lead
  d.circle(0, 0, 60);
}

// ---- weather ---------------------------------------------------------------
// The glyphs are deliberately few. A vector tube can tell sun from cloud from
// rain at a glance and cannot usefully distinguish drizzle from light rain, so
// the host maps its own vocabulary onto seven shapes and the display draws them
// properly rather than drawing twenty badly.
void weather(const ClockState&, DrawList& d) {
  const host::Weather& w = host::weather();
  if (!w.valid) {
    static const char* kNo = "NO WEATHER";
    d.text(-txt::inkWidth(9, kNo) / 2, -70, 9, kNo);
    return;
  }

  constexpr int32_t CY = 380;                    // the glyph sits above centre
  const uint8_t sky = w.sky;

  if (sky == host::Clear || sky == host::PartCloud) {
    const int32_t r = sky == host::Clear ? 190 : 150;
    const int32_t cx = sky == host::Clear ? 0 : -170;
    d.circle((int)cx, CY, (int)r);
    for (int i = 0; i < 8; ++i) {                // rays
      const int a = i * vec::kSteps / 8;
      const int32_t s = vec::sinT(a), k = vec::cosT(a);
      d.line((int)(cx + (((r + 60) * s) >> 16)), (int)(CY + (((r + 60) * k) >> 16)),
             (int)(cx + (((r + 150) * s) >> 16)), (int)(CY + (((r + 150) * k) >> 16)));
    }
  }
  if (sky != host::Clear) {
    // A cloud is three overlapping arcs on a flat base — the shape reads at any
    // size, which a single blob does not.
    const int32_t cx = sky == host::PartCloud ? 120 : 0;
    d.circle((int)(cx - 150), CY - 30, 130);
    d.circle((int)(cx + 10),  CY + 40, 160);
    d.circle((int)(cx + 180), CY - 20, 120);
    d.line((int)(cx - 270), CY - 110, (int)(cx + 290), CY - 110);
  }
  if (sky == host::Rain || sky == host::Storm) {
    for (int i = 0; i < 4; ++i) {
      const int32_t x = -190 + i * 130;
      d.line((int)x, CY - 170, (int)(x - 45), CY - 300);
    }
  }
  if (sky == host::Snow) {
    for (int i = 0; i < 3; ++i) {
      const int32_t x = -140 + i * 140;
      d.line((int)(x - 40), CY - 230, (int)(x + 40), CY - 230);
      d.line((int)x, CY - 270, (int)x, CY - 190);
    }
  }
  if (sky == host::Storm) {                       // a bolt through the rain
    d.line(60, CY - 150, -20, CY - 260);
    d.line(-20, CY - 260, 70, CY - 270);
    d.line(70, CY - 270, -20, CY - 400);
  }
  if (sky == host::Fog) {
    for (int i = 0; i < 4; ++i)
      d.line(-280, CY - 120 + i * 90, 280, CY - 120 + i * 90);
  }

  static char t[16];
  const int whole = w.tempC10 / 10, frac = (w.tempC10 < 0 ? -w.tempC10 : w.tempC10) % 10;
  snprintf(t, sizeof t, "%d.%d", whole, frac);
  d.text(-txt::inkWidth(20, t) / 2, -420, 20, t);
  if (w.place[0])  d.text(-txt::inkWidth(8, w.place) / 2, -820, 8, w.place);
  if (w.detail[0]) d.text(-txt::inkWidth(7, w.detail) / 2, -1080, 7, w.detail);
}

// ---- ticker ----------------------------------------------------------------
// A marquee, and the interesting part is what it does NOT draw. The device has
// no clipping: a 160-character string handed to d.text() costs the beam every
// glyph, including the hundred that are off the screen. So the visible window is
// worked out from the font's own advances and only those characters are drawn.
void ticker(const ClockState&, DrawList& d) {
  const host::Ticker& tk = host::ticker();
  if (!tk.valid) {
    static const char* kNo = "NO TICKER TEXT";
    d.text(-txt::inkWidth(9, kNo) / 2, -70, 9, kNo);
    return;
  }
  constexpr int SC = 12, HALF = 1000;             // window is +/-HALF
  const int32_t total = txt::inkWidth(SC, tk.text);
  // Pixels a second, as device units: a comfortable reading pace.
  const int32_t sp = 260;
  const int32_t span = total + 2 * HALF;
  const int32_t travelled = (int32_t)((uint64_t)(millis() - tk.stampMs) * sp / 1000 % span);
  const int32_t left = HALF - travelled;          // x of the first character

  // Walk the string, accumulating advances, and copy out only the run that lands
  // inside the window.
  static char win[40];
  int n = 0;
  int32_t x = left, startX = left;
  for (const char* p = tk.text; *p && n < (int)sizeof(win) - 1; ++p) {
    char one[2] = { *p, 0 };
    const int32_t adv = txt::inkWidth(SC, one) + SC * 3;
    if (x + adv > -HALF && x < HALF) {
      if (!n) startX = x;
      win[n++] = *p;
    }
    x += adv;
  }
  win[n] = '\0';
  // Twice. A single line of text is 3000 dots, which leaves the tube idle for
  // most of the refresh however far the dwell is pushed — on this display
  // brightness is redraw count, so the cheapest fix is to draw it again.
  if (n) { d.text((int)startX, -70, SC, win); d.text((int)startX, -70, SC, win); }

  // A rule above and below, so a lone line of text does not float.
  d.line(-1050, 320, 1050, 320);
  d.line(-1050, -420, 1050, -420);
}

}}  // namespace faces::impl
