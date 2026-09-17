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
#include "seed.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <cmath>

namespace {

constexpr int kFrames  = 8;      // per face
constexpr int kStepMs  = 180;    // between frames; playback matches
constexpr int kMaxPts  = 60;     // per stroke after simplifying
constexpr int kWarm    = 40;     // frames rendered but not captured, first
constexpr double kEps  = 14.0;   // DP tolerance, DAC counts (~1 device unit)

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
      txt::centerLines(d);     // as main.cpp does, before anything is drawn
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
