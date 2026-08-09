// faces.cpp — the face registry and the knob/button navigation over it.
//
// The renderers themselves live in faces_time.cpp, faces_3d.cpp and
// faces_gen.cpp; this file only decides what exists and how you move between
// them. Ported from SCTVcode g_clocks.ino (faceList / time6nList).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "face.h"
#include "faces_impl.h"
#include "state.h"

namespace faces {
namespace {

using namespace impl;

// The knob walks families, the button walks variants within one. Order here IS
// the wire face id — the bridge's kFaceNames[] and the Home Assistant discovery
// options must match it index for index.
RenderFn kFaces[] = {
  // analog
  hands, numbers, tickdial, orbit, sector,
  // digital
  digital, datetime, wordclock, binary,
  // solids
  tetra, cube, octa, icosa, dodeca, tesseract, torus,
  // curves
  lissajous, harmonograph, spirograph, rose, lorenz, starpoly,
  // motion
  starfield, tunnel,
  // live — driven by whatever is plugged into the front USB jack
  midiscope, midichord,
  // effects
  matrix,
  // host-fed
  nowplaying, gauges,
  // wireframes
  teapot, sphere, knot, mobius, helix,
  // science
  atom,
  // sky and games
  solar, moon, weather,
  pong, life,
  // clocks and text
  trailclock, ticker, worldclock,
  // arcade
  asteroids,
};

// Contiguous runs of kFaces above. Keep the two in step.
struct Family { uint8_t first, count; };
const Family kFamilies[] = {
  {  0, 5 },   // analog:  roman, numbered, tick dial, orrery, sectors
  {  5, 4 },   // digital: time, with date, in words, in BCD
  {  9, 7 },   // solids:  the five Platonics, plus 4D and a torus
  { 16, 6 },   // curves:  Lissajous, harmonograph, spirograph, rose, Lorenz, {n/k}
  { 22, 2 },   // motion:  starfield, tunnel
  { 24, 2 },   // live:    MIDI scope figure, MIDI chord wheel
  { 26, 1 },   // effects: digital rain
  { 27, 2 },   // data:    now playing, gauges
  { 29, 5 },   // wire:    teapot, sphere, knot, mobius, helix
  { 34, 1 },   // science: Bohr atom
  { 35, 3 },   // sky:     solar system, moon, weather
  { 38, 2 },   // games:   pong, life
  { 40, 3 },   // extra:   trail clock, ticker, world clock
  { 43, 1 },   // arcade:  asteroids
};
constexpr uint8_t kFamilyCount = sizeof(kFamilies) / sizeof(kFamilies[0]);

// Each family remembers the variant you last left it on, so knobbing away and
// back returns you to the dial you chose rather than resetting to the first.
uint8_t lastVariant[kFamilyCount] = {};

uint8_t familyOf(uint8_t faceId) {
  for (uint8_t f = 0; f < kFamilyCount; ++f)
    if (faceId >= kFamilies[f].first &&
        faceId <  kFamilies[f].first + kFamilies[f].count) return f;
  return 0;
}

} // namespace

void     registerBuiltins() { /* static table for now */ }
uint8_t  count() { return sizeof(kFaces) / sizeof(kFaces[0]); }
RenderFn current(const DeviceState& dev) {
  return kFaces[dev.faceId < count() ? dev.faceId : 0];
}

uint8_t nextFamily(uint8_t faceId, int dir) {
  const uint8_t f = familyOf(faceId);
  lastVariant[f] = (uint8_t)(faceId - kFamilies[f].first);   // remember where we were
  int n = (int)f + (dir >= 0 ? 1 : -1);
  if (n < 0) n = kFamilyCount - 1;
  if (n >= kFamilyCount) n = 0;
  const Family& fam = kFamilies[n];
  const uint8_t v = lastVariant[n] < fam.count ? lastVariant[n] : 0;
  return (uint8_t)(fam.first + v);
}

uint8_t nextVariant(uint8_t faceId) {
  const uint8_t f = familyOf(faceId);
  const Family& fam = kFamilies[f];
  const uint8_t v = (uint8_t)((faceId - fam.first + 1) % fam.count);
  lastVariant[f] = v;
  return (uint8_t)(fam.first + v);
}

} // namespace faces
