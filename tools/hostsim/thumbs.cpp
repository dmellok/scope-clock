// thumbs.cpp — bake a short animation of every face into a header the bridge
// serves to the control page.
//
// A live preview is not reachable over the link and that is worth writing down
// so nobody tries again: MAX_PAYLOAD is 240 bytes against draw lists of up to
// 192 items, the device's send() DROPS a frame when the buffer is full rather
// than blocking, and chunked transfer only exists host->device. Composing a
// face out of band would also double-step the ones that keep their own
// animation state (life, pong, asteroids, starfield, matrix).
//
// So the frames are baked here, from the REAL face code compiled against the
// fake DAC — the same trick the sizing harness uses. Captured at the beam, not
// at the draw list, so text glyphs and circles arrive as plain polylines and
// the page needs no glyph table and no circle generator of its own.
//
// Output is bridge-esp32/src/thumbs.h. Regenerate with:
//     ./tools/hostsim/build.sh && ./tools/hostsim/thumbs
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "face.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"
#include "gauges.h"
#include "radar.h"
#include "nowplaying.h"
#include "hostdata.h"
#include "zones.h"
#include "hal/midi.h"
#include "audiofx.h"
#include "sim.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <cmath>

// ---- the one HAL a face touches -------------------------------------------
// faces_midi renders from this and nothing else, so a couple of sounding
// voices is the whole stub. A fifth: the interval the scope draws as 3:2.
namespace hal { namespace midi {
static MidiState g;
void init() {}
void poll() {}
const MidiState& state() { return g; }
void seed() {
  g.v[0] = { 57, 100, 1024, true };     // A3
  g.v[1] = { 64, 96, 900, true };       // E4, a fifth above
  g.v[2] = { 69, 80, 700, true };       // A4
  g.sounding = 3; g.lowest = 57; g.sustain = false; g.lastEventMs = 1;
}
}}

namespace {

constexpr int kFrames  = 8;      // per face
constexpr int kStepMs  = 180;    // between frames; playback matches
constexpr int kMaxPts  = 60;     // per stroke after simplifying
constexpr int kWarm    = 40;     // frames rendered but not captured, first
constexpr double kEps  = 14.0;   // DP tolerance, DAC counts (~1 device unit)

// ---- seed the faces the host normally feeds -------------------------------
void put(std::vector<uint8_t>& b, const char* s) {
  while (*s) b.push_back((uint8_t)*s++);
}

void seedHostFaces() {
  {   // gauges
    std::vector<uint8_t> p{3};
    const char* n[3] = {"5H","7D","EXTRA"};
    const uint8_t v[3] = {62, 38, 12};
    for (int i = 0; i < 3; ++i) { p.push_back(v[i]); p.push_back((uint8_t)strlen(n[i])); put(p,n[i]); }
    put(p, "5H RESETS IN 2h10m");
    gauge::set(p.data(), (uint8_t)p.size());
  }
  {   // now playing
    std::vector<uint8_t> p{1};
    const char* t="Windowlicker"; const char* a="Aphex Twin"; const char* al="Windowlicker";
    p.push_back(0x0e); p.push_back(0x01);          // 270s
    p.push_back(0x6e); p.push_back(0x00);          // 110s in
    p.push_back((uint8_t)strlen(t)); p.push_back((uint8_t)strlen(a));
    put(p,t); put(p,a); put(p,al);
    np::set(p.data(), (uint8_t)p.size());
  }
  {   // weather: 21.5C, part cloud
    std::vector<uint8_t> p;
    p.push_back(215 & 0xFF); p.push_back(215 >> 8);
    p.push_back(1);
    p.push_back(9); put(p,"MELBOURNE"); put(p,"14/23");
    host::setWeather(p.data(), (uint8_t)p.size());
  }
  {   // ticker
    std::vector<uint8_t> p; put(p,"THE BEAM IS THE ONLY THING THAT DRAWS  ");
    host::setTicker(p.data(), (uint8_t)p.size());
  }
  {   // world clock
    std::vector<uint8_t> p{3};
    struct { int16_t d; const char* n; } z[3] = {{-180,"LONDON"},{120,"AUCKLAND"},{-870,"NEW YORK"}};
    for (auto& e : z) {
      p.push_back((uint8_t)(e.d & 0xFF)); p.push_back((uint8_t)((e.d >> 8) & 0xFF));
      p.push_back((uint8_t)strlen(e.n)); put(p,e.n);
    }
    zones::set(p.data(), (uint8_t)p.size());
  }
  {   // radar
    std::vector<uint8_t> p{7};
    struct { uint8_t b,r,f; const char* n; } c[7] = {
      {154,30,2,"tofu"},{40,96,0,"router"},{96,120,1,"nas"},{200,150,0,"printer"},
      {18,168,0,"tv"},{230,190,0,"phone"},{130,220,0,"laptop"}};
    for (auto& e : c) {
      p.push_back(e.b); p.push_back(e.r); p.push_back(e.f);
      p.push_back((uint8_t)strlen(e.n)); put(p,e.n);
    }
    put(p, "192.168.1.0/24  7 UP");
    rdr::set(p.data(), (uint8_t)p.size());
  }
}

// ---- Douglas-Peucker, on the captured beam path ---------------------------
// The beam puts a dot every 2 DAC counts, so a straight stroke arrives as
// hundreds of collinear points. This is what turns it back into two.
void dp(const std::vector<sim::Pt>& in, int a, int b, std::vector<bool>& keep) {
  double best = -1; int bi = -1;
  const double ax = in[a].x, ay = in[a].y, bx = in[b].x, by = in[b].y;
  const double dx = bx-ax, dy = by-ay, dd = dx*dx+dy*dy;
  for (int i = a+1; i < b; ++i) {
    double t = dd ? ((in[i].x-ax)*dx + (in[i].y-ay)*dy)/dd : 0.0;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    const double qx = ax+t*dx-in[i].x, qy = ay+t*dy-in[i].y, d = qx*qx+qy*qy;
    if (d > best) { best = d; bi = i; }
  }
  if (best > kEps*kEps && bi > 0) { keep[bi] = true; dp(in,a,bi,keep); dp(in,bi,b,keep); }
}

std::vector<sim::Pt> simplify(const std::vector<sim::Pt>& in) {
  if (in.size() < 3) return in;
  std::vector<bool> keep(in.size(), false);
  keep.front() = keep.back() = true;
  dp(in, 0, (int)in.size()-1, keep);
  std::vector<sim::Pt> out;
  for (size_t i = 0; i < in.size(); ++i) if (keep[i]) out.push_back(in[i]);
  // A cap so one pathological stroke cannot dominate the blob; evenly dropped
  // rather than truncated, which would lop the end off the shape.
  if ((int)out.size() > kMaxPts) {
    std::vector<sim::Pt> t;
    for (int i = 0; i < kMaxPts; ++i) t.push_back(out[(size_t)i*(out.size()-1)/(kMaxPts-1)]);
    out.swap(t);
  }
  return out;
}


// The visualisers read a live microphone, which a build machine does not have.
// Fed a synthetic spectrum that moves, so the baked frames show the face doing
// its job rather than eight copies of NO SIGNAL. Re-fed every frame because
// afx::live() times out after 2s and the decay would otherwise flatten it.
void feedAudio(int k) {
  uint8_t p[3 + afx::kBands];
  double ph = k * 0.55;
  int lvl = (int)(150 + 70 * sin(ph * 0.7));
  p[0] = (uint8_t)lvl; p[1] = (uint8_t)(lvl + 30 > 255 ? 255 : lvl + 30);
  p[2] = afx::kBands;
  for (int b = 0; b < afx::kBands; ++b) {
    // A tilted spectrum with a peak wandering through it: what a room actually
    // looks like, rather than a flat bar chart.
    double tilt = 1.0 - b / 20.0;
    double bump = exp(-pow(b - (5 + 4 * sin(ph * 0.45)), 2) / 6.0);
    int v = (int)(255 * tilt * (0.28 + 0.72 * bump) * (0.55 + 0.45 * sin(ph + b * 0.3)));
    p[3 + b] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
  }
  afx::setBands(p, (uint8_t)(3 + afx::kBands));

  uint8_t w[1 + 64];
  w[0] = 64;
  for (int i = 0; i < 64; ++i) {
    double t = i / 64.0;
    double y = sin(2 * M_PI * (2 * t) + ph) * 0.6 + sin(2 * M_PI * (5 * t) + ph * 1.7) * 0.3;
    int v = (int)(y * 110);
    w[1 + i] = (uint8_t)(int8_t)(v < -127 ? -127 : (v > 127 ? 127 : v));
  }
  afx::setWave(w, 65);
}

} // namespace

int main() {
  vec::init();
  hal::midi::seed();
  seedHostFaces();

  // A readable, deliberately un-round time. The clock faces bake static — they
  // are clocks, and a thumbnail of one does not need to tick.
  ClockState clk;
  clk.year=26; clk.month=8; clk.day=11; clk.wday=2;
  clk.hour=10; clk.minute=9; clk.second=36;
  clk.rtcPresent=true; clk.everSet=true;

  DeviceState dev;
  const int n = faces::count();

  std::vector<uint8_t> blob;
  std::vector<size_t>  off(n), len(n);

  for (int f = 0; f < n; ++f) {
    dev.faceId = (uint8_t)f;
    faces::RenderFn fn = faces::current(dev);
    off[f] = blob.size();
    blob.push_back((uint8_t)kFrames);

    // Warm-up. Some faces ACCUMULATE, and capturing from t=0 caught them before
    // they had anything to show: the Lorenz attractor has no trail until it has
    // been integrated for a while, and the digital rain starts with empty
    // columns. Both baked as eight blank frames until this was added.
    for (int w = 0; w < kWarm; ++w) {
      feedAudio(w); DrawList wd; fn(clk, wd); vec::renderFrame(wd); simStepMillis(kStepMs);
    }

    for (int k = 0; k < kFrames; ++k) {
      feedAudio(kWarm + k);
      DrawList d;
      fn(clk, d);
      // Baked at the authored size. The page fits the disc to the field, so a
      // face that fills 1200 fills the disc whatever its per-face scale is set
      // to on the device — the thumbnail is what the face IS, not how it is
      // currently sized.
      sim::captureBegin();
      vec::renderFrame(d);
      sim::captureEnd();

      std::vector<std::vector<sim::Pt>> strokes;
      for (int i = 0; i < sim::strokeCount(); ++i) {
        std::vector<sim::Pt> s;
        for (int j = 0; j < sim::strokePoints(i); ++j) s.push_back(sim::strokePoint(i,j));
        std::vector<sim::Pt> t = simplify(s);
        if (t.size() >= 2) strokes.push_back(t);
      }
      if (strokes.size() > 255) strokes.resize(255);
      blob.push_back((uint8_t)strokes.size());
      for (auto& s : strokes) {
        blob.push_back((uint8_t)s.size());
        for (auto& p : s) {
          // DAC counts /16 into a signed byte: the rim is 1800, so +-112 covers
          // the glass with room for the overlay allowance, and 16 counts is a
          // third of a pixel on a 178px disc.
          int x = p.x/16, y = p.y/16;
          x = x < -127 ? -127 : (x > 127 ? 127 : x);
          y = y < -127 ? -127 : (y > 127 ? 127 : y);
          blob.push_back((uint8_t)(int8_t)x);
          blob.push_back((uint8_t)(int8_t)y);
        }
      }
      simStepMillis(kStepMs);
    }
    len[f] = blob.size() - off[f];
  }

  FILE* o = fopen("bridge-esp32/src/thumbs.h", "w");
  if (!o) { fprintf(stderr, "thumbs: cannot write bridge-esp32/src/thumbs.h\n"); return 1; }
  fprintf(o,
    "// thumbs.h — GENERATED by tools/hostsim/thumbs.cpp. Do not edit.\n"
    "//\n"
    "// A short baked animation of every face, captured from the real face code\n"
    "// at the beam, so text and circles are already polylines. Served per face\n"
    "// by /api/thumb?i=<index>; the index is the face's position in the\n"
    "// registry, which is what /api/faces returns in order.\n"
    "//\n"
    "// Format, all little-endian, coordinates signed bytes of DAC/16:\n"
    "//   u8 frames\n"
    "//   per frame:  u8 strokes\n"
    "//   per stroke: u8 points, then points x (i8 x, i8 y)\n"
    "// SPDX-License-Identifier: GPL-2.0-or-later\n"
    "#pragma once\n#include <pgmspace.h>\n#include <stdint.h>\n\n");
  fprintf(o, "static const uint8_t THUMB_DATA[] PROGMEM = {\n");
  for (size_t i = 0; i < blob.size(); ++i)
    fprintf(o, "%u,%s", blob[i], (i%24==23) ? "\n" : "");
  fprintf(o, "\n};\n\nstatic const uint32_t THUMB_OFF[] PROGMEM = {\n");
  for (int f = 0; f < n; ++f) fprintf(o, "%zu,%s", off[f], (f%12==11) ? "\n" : "");
  fprintf(o, "\n};\nstatic const uint16_t THUMB_LEN[] PROGMEM = {\n");
  for (int f = 0; f < n; ++f) fprintf(o, "%zu,%s", len[f], (f%12==11) ? "\n" : "");
  fprintf(o, "\n};\nstatic const uint8_t THUMB_COUNT = %d;\n", n);
  fclose(o);

  size_t big = 0; int bigf = -1;
  for (int f = 0; f < n; ++f) if (len[f] > big) { big = len[f]; bigf = f; }
  printf("%d faces x %d frames -> %zu bytes (%.1f KB), largest face %d at %zu bytes\n",
         n, kFrames, blob.size(), blob.size()/1024.0, bigf, big);
  return 0;
}
