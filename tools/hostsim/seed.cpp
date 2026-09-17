// seed.cpp — fixtures for the host harnesses. Lifted out of thumbs.cpp when
// websim needed the same ones; see seed.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "seed.h"
#include "gauges.h"
#include "nowplaying.h"
#include "hostdata.h"
#include "zones.h"
#include "radar.h"
#include "audiofx.h"
#include "hal/midi.h"
#include <string.h>
#include <vector>
#include <cmath>

// ---- the one HAL a face touches -------------------------------------------
// faces_midi renders from this and nothing else, so a couple of sounding
// voices is the whole stub.
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
void put(std::vector<uint8_t>& b, const char* s) {
  while (*s) b.push_back((uint8_t)*s++);
}
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
