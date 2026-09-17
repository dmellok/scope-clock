// core.cpp — the firmware's real render path, one frame at a time.
//
// Same seam as tools/hostsim: the only firmware code that touches hardware is
// hal::dac::write/blank, so vector.cpp, text.cpp and every face compile for a
// host against a fake DAC that records the beam path instead of driving one.
// hostsim MEASURES that path and prints numbers; this captures it so something
// can draw it. Nothing about the geometry is reimplemented — what a browser
// draws from these bytes is what the tube would draw, dot for dot.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core.h"
#include "names.h"
#include "face.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"
#include "sim.h"
#include "seed.h"
#include <Arduino.h>
#include <time.h>

namespace core {
namespace {

// Real wall time, refreshed every frame, so the clock faces show the actual
// time rather than a frozen fixture. It is the one thing a baked thumbnail
// cannot show and a live viewer should. Under WebAssembly this is the
// browser's own clock, which is exactly what you want.
ClockState  g_clk;
DeviceState g_dev;
int         g_lastFace = -1;
int         g_audioStep = 0;
std::string g_buf;
std::string g_faces;

// Some faces ACCUMULATE: the Lorenz attractor has no trail until it has been
// integrated for a while and the digital rain starts with empty columns. Cold
// frames of those are blank, which reads as a broken viewer rather than a face
// that needs a moment, so a face change is warmed before it is shown.
constexpr int kWarmFrames = 40;

void refreshClock() {
  const time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  g_clk.year   = (int16_t)(lt.tm_year - 100);
  g_clk.month  = (int16_t)(lt.tm_mon + 1);
  g_clk.day    = (int16_t)lt.tm_mday;
  g_clk.wday   = (int16_t)lt.tm_wday;
  g_clk.hour   = (int16_t)lt.tm_hour;
  g_clk.minute = (int16_t)lt.tm_min;
  g_clk.second = (int16_t)lt.tm_sec;
  g_clk.rtcPresent = true;
  g_clk.everSet    = true;
}

void renderOnce(int faceId, int scalePct, uint32_t stepMs) {
  refreshClock();
  feedAudio(g_audioStep++);
  g_dev.faceId = (uint8_t)faceId;
  DrawList d;
  faces::current(g_dev)(g_clk, d);
  // The device's own order, from main.cpp: resolve text positions FIRST, then
  // apply the per-face size. Skipping centerLines is not a no-op for any face
  // that stacks rows at (0,0) and lets the layout place them — the digital
  // face's four items land on top of each other and right of centre. The
  // thumbnail generator had exactly that bug, which is how this was found.
  txt::centerLines(d);
  if (!faces::rawScale((uint8_t)faceId)) scaleList(d, scalePct);
  vec::renderFrame(d);
  simStepMillis(stepMs);
}

void put16(std::string& b, int v) {
  b.push_back((char)(v & 0xFF));
  b.push_back((char)((v >> 8) & 0xFF));
}
void put32(std::string& b, int32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back((char)((v >> (i * 8)) & 0xFF));
}

}  // namespace

void init() {
  vec::init();
  hal::midi::seed();
  seedHostFaces();

  g_faces = "[";
  for (int i = 0; i < kNameCount; ++i) {
    if (i) g_faces += ",";
    g_faces += "{\"n\":\""; g_faces += kNames[i][0];
    g_faces += "\",\"f\":\""; g_faces += kNames[i][1]; g_faces += "\"}";
  }
  g_faces += "]";
}

int faceCount() { return faces::count(); }
const std::string& facesJson() { return g_faces; }

const std::string& frame(int faceId, int scalePct, uint32_t stepMs) {
  if (faceId < 0 || faceId >= faces::count()) faceId = 0;
  if (scalePct < 20) scalePct = 20;
  if (scalePct > 100) scalePct = 100;

  if (faceId != g_lastFace) {
    for (int i = 0; i < kWarmFrames; ++i) renderOnce(faceId, scalePct, stepMs);
    g_lastFace = faceId;
  }

  sim::reset();
  sim::captureBegin();
  renderOnce(faceId, scalePct, stepMs);
  sim::captureEnd();
  const sim::Stats st = sim::get();

  g_buf.clear();
  const int n = sim::strokeCount();
  put16(g_buf, n);
  put32(g_buf, (int32_t)st.dots);
  put32(g_buf, (int32_t)st.moves);
  put32(g_buf, st.maxR);
  put16(g_buf, faceId);
  put16(g_buf, 0);
  for (int i = 0; i < n; ++i) {
    const int pts = sim::strokePoints(i);
    put16(g_buf, pts);
    for (int j = 0; j < pts; ++j) {
      const sim::Pt p = sim::strokePoint(i, j);
      put16(g_buf, p.x); put16(g_buf, p.y);
    }
  }
  return g_buf;
}

}  // namespace core
