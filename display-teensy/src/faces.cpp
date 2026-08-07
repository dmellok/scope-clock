// faces.cpp — built-in clock faces that render from the RTC, so the clock is
// autonomous when the network is down. Ported from SCTVcode g_clocks.ino
// (DrawClk / DoHand / faceList / time6nList / makeTimeStrings).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "face.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"

namespace faces {
namespace {

// Clock angles are in 240ths of a turn, 0 = North, increasing clockwise —
// which is neither the trig tables' origin (East) nor their direction (CCW).
// Swapping sin and cos on the way out fixes both at once.
void hand(DrawList& d, int len, int angle240) {
  const int a  = (angle240 * vec::kSteps / 240) % vec::kSteps;
  const int ux = (int)(vec::sinT(a) / 500);   // unit vector, ~+/-131
  const int uy = (int)(vec::cosT(a) / 500);
  d.line(ux * 10 / 13, uy * 10 / 13,          // start at the hub's edge
         (len * ux) >> 8, (len * uy) >> 8);
}

int to12(int h) {
  if (h == 0) return 12;
  return h > 12 ? h - 12 : h;
}

// Digital face: HH:MM large with SS below, laid out by txt::centerLines. The
// leading empty row is a spacer that drops the whole block slightly, which
// reads as better centred than true centring does. (time6nList)
void digital(const ClockState& c, DrawList& d) {
  static char hh[3], mm[3], ss[4];

  const int h = c.hr12 ? to12(c.hour) : c.hour;
  hh[0] = (char)('0' + h / 10); hh[1] = (char)('0' + h % 10); hh[2] = 0;
  if (c.hr12 && hh[0] == '0') { hh[0] = hh[1]; hh[1] = 0; }   // no leading zero
  mm[0] = (char)('0' + c.minute / 10); mm[1] = (char)('0' + c.minute % 10); mm[2] = 0;
  ss[0] = (char)('0' + c.second / 10); ss[1] = (char)('0' + c.second % 10);
  ss[2] = '\n'; ss[3] = 0;

  d.text(0, 0, 10, "\n");     // spacer row
  d.text(0, 0, 40, hh);
  d.text(0, 0, 40, ":");
  d.text(0, 0, 40, mm);
  d.text(0, 0, 40, "\n");     // ends the HH:MM row
  d.text(0, 0, 30, ss);
}

// Roman numerals, in clock order starting at XII.
const char* const kNumerals[12] = {
  "XII", "I", "II", "III", "IIII", "V", "VI", "VII", "VIII", "IX", "X", "XI"
};
constexpr int kDialRadius  = 1000;
constexpr int kNumeralScale = 10;

// Place each numeral centred on its own hour mark.
//
// The original carried hand-tuned left-edge coordinates, and they are not
// quite square: measured against their true hour angles, IX sits 55 units
// right of its mark and V 40 units left. The errors happen to cancel — the
// ring's mean centre is within 3 units of the origin — so it reads as centred
// but slightly irregular. Deriving the position from the angle and the glyph's
// own ink width instead makes it exact, and self-adjusting if the scale or
// dial radius ever change.
void numerals(DrawList& d) {
  for (int h = 0; h < 12; ++h) {
    const int a  = (h * vec::kSteps) / 12;             // 0 = XII, clockwise
    const int cx = (int)((kDialRadius * vec::sinT(a)) >> 16);
    const int cy = (int)((kDialRadius * vec::cosT(a)) >> 16);
    d.text(cx - txt::inkWidth(kNumeralScale, kNumerals[h]) / 2,
           cy - txt::height(kNumeralScale) / 2,         // baseline, not centre
           kNumeralScale, kNumerals[h]);
  }
}

// Analog face: numeral ring, hub circle, three hands. The hour and minute
// hands are drawn twice so they come out brighter than the second hand — on a
// CRT, brightness is redraw count. The numerals carry real positions, which
// also opts this list out of txt::centerLines.
void hands(const ClockState& c, DrawList& d) {
  numerals(d);
  d.circle(0, 0, 90);                                   // hub

  const int minAngle = c.second / 15 + c.minute * 4;
  const int hrAngle  = (c.hour % 12) * 20 + c.minute / 3;
  hand(d, 2500, c.second * 4);
  hand(d, 2000, minAngle);
  hand(d, 1500, hrAngle);
  hand(d, 2000, minAngle);                              // again, for brightness
  hand(d, 1500, hrAngle);
}

// ---- spinning cube ---------------------------------------------------------
// The thing a vector display does that a raster one cannot: twelve straight
// strokes, no fill, no pixels. Rotation runs off the same Q16 sin/cos tables
// the beam stepper uses, so there is no floating point anywhere in the frame
// path — 8 vertices through two rotations and a perspective divide is well
// under a millisecond.
constexpr int kCubeHalf  = 560;    // half-edge in display units
constexpr int kCubeFocal = 2200;   // eye distance; smaller = stronger perspective

const int8_t kCubeVerts[8][3] = {
  {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},   // back face
  {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}    // front face
};
const uint8_t kCubeEdges[12][2] = {
  {0,1},{1,2},{2,3},{3,0},        // back
  {4,5},{5,6},{6,7},{7,4},        // front
  {0,4},{1,5},{2,6},{3,7}         // struts
};

void cube(const ClockState& c, DrawList& d) {
  (void)c;
  // Free-running rather than time-derived: the two axes advance at different
  // rates so it tumbles instead of spinning flat. 512 frames per yaw turn is
  // about 8.5s at 60Hz.
  static uint16_t tick = 0;
  ++tick;
  const int yaw   = (int)((tick * 2) & (vec::kSteps - 1));
  const int pitch = (int)( tick       & (vec::kSteps - 1));

  const int32_t sy = vec::sinT(yaw),   cy = vec::cosT(yaw);
  const int32_t sp = vec::sinT(pitch), cp = vec::cosT(pitch);

  int16_t px[8], py[8];
  for (int i = 0; i < 8; ++i) {
    const int32_t x0 = kCubeVerts[i][0] * kCubeHalf;
    const int32_t y0 = kCubeVerts[i][1] * kCubeHalf;
    const int32_t z0 = kCubeVerts[i][2] * kCubeHalf;

    const int32_t x1 = (x0 * cy - z0 * sy) >> 16;    // yaw about Y
    const int32_t z1 = (x0 * sy + z0 * cy) >> 16;
    const int32_t y2 = (y0 * cp - z1 * sp) >> 16;    // pitch about X
    const int32_t z2 = (y0 * sp + z1 * cp) >> 16;

    // Eye sits at -kCubeFocal, so the divisor cannot reach zero: |z2| tops out
    // at half-edge * sqrt(3) ~= 970, well inside 2200.
    const int32_t den = kCubeFocal + z2;
    px[i] = (int16_t)((x1 * kCubeFocal) / den);
    py[i] = (int16_t)((y2 * kCubeFocal) / den);
  }

  for (int e = 0; e < 12; ++e) {
    const uint8_t a = kCubeEdges[e][0], b = kCubeEdges[e][1];
    d.line(px[a], py[a], px[b], py[b]);
  }
}

RenderFn kFaces[] = { hands, digital, cube };

} // namespace

void     registerBuiltins() { /* static table for now */ }
uint8_t  count() { return sizeof(kFaces) / sizeof(kFaces[0]); }
RenderFn current(const DeviceState& dev) {
  return kFaces[dev.faceId < count() ? dev.faceId : 0];
}

} // namespace faces
