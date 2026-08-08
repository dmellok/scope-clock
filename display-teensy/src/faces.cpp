// faces.cpp — built-in clock faces that render from the RTC, so the clock is
// autonomous when the network is down. Ported from SCTVcode g_clocks.ino
// (DrawClk / DoHand / faceList / time6nList / makeTimeStrings).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "face.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"
#include <stdio.h>

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

// Dial labels, in clock order starting at twelve.
const char* const kRoman[12] = {
  "XII", "I", "II", "III", "IIII", "V", "VI", "VII", "VIII", "IX", "X", "XI"
};
const char* const kArabic[12] = {
  "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"
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
void numerals(DrawList& d, const char* const* label) {
  for (int h = 0; h < 12; ++h) {
    const int a  = (h * vec::kSteps) / 12;             // 0 = XII, clockwise
    const int cx = (int)((kDialRadius * vec::sinT(a)) >> 16);
    const int cy = (int)((kDialRadius * vec::cosT(a)) >> 16);
    d.text(cx - txt::inkWidth(kNumeralScale, label[h]) / 2,
           cy - txt::height(kNumeralScale) / 2,         // baseline, not centre
           kNumeralScale, label[h]);
  }
}

// Analog face: numeral ring, hub circle, three hands. The hour and minute
// hands are drawn twice so they come out brighter than the second hand — on a
// CRT, brightness is redraw count. The numerals carry real positions, which
// also opts this list out of txt::centerLines.
void dial(const ClockState& c, DrawList& d, const char* const* label) {
  numerals(d, label);
  d.circle(0, 0, 90);                                   // hub

  const int minAngle = c.second / 15 + c.minute * 4;
  const int hrAngle  = (c.hour % 12) * 20 + c.minute / 3;
  hand(d, 2500, c.second * 4);
  hand(d, 2000, minAngle);
  hand(d, 1500, hrAngle);
  hand(d, 2000, minAngle);                              // again, for brightness
  hand(d, 1500, hrAngle);
}

void hands(const ClockState& c, DrawList& d)   { dial(c, d, kRoman);  }
void numbers(const ClockState& c, DrawList& d) { dial(c, d, kArabic); }

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

// ---- date and time ---------------------------------------------------------
// Weekday, time, date — three rows laid out by txt::centerLines, which is what
// the '\n' terminators are for: the last item of a row carries it.
const char* const kWday[7] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
const char* const kMon[12] = {
  "Jan", "Feb", "March", "April", "May", "June",
  "July", "Aug", "Sept", "Oct", "Nov", "Dec"
};

void datetime(const ClockState& c, DrawList& d) {
  static char l1[16], l2[20], l3[32];
  const int h = c.hr12 ? to12(c.hour) : c.hour;

  snprintf(l1, sizeof l1, "%s\n", kWday[(unsigned)c.wday % 7]);
  snprintf(l2, sizeof l2, "%d:%02d:%02d\n", h, c.minute, c.second);
  snprintf(l3, sizeof l3, "%d %s 20%02d\n",
           c.day, kMon[(unsigned)(c.month - 1) % 12], c.year);

  // 22 not 26 on the time row: eight characters at 26 measure ~2700 units
  // against a 2500 field, so it would run off both edges.
  d.text(0, 0, 12, l1);
  d.text(0, 0, 22, l2);
  d.text(0, 0, 12, l3);
}

// ---- Lissajous -------------------------------------------------------------
// The figure an oscilloscope makes when you feed it two sines, and the reason
// anyone points a camera at one. Drawn as a chain of short segments; the ends
// meet, so the blanked hop between them costs no beam travel.
constexpr int kLisSegs = 96;
constexpr int kLisAmp  = 1080;

void lissajous(const ClockState& c, DrawList& d) {
  (void)c;
  static uint16_t tick = 0;
  ++tick;
  // A slowly drifting phase is what makes the figure appear to rotate and
  // fold through itself rather than sit still.
  const int phase = (int)(tick * 3);
  const int a = 3, b = 4;              // frequency ratio

  int px = 0, py = 0;
  for (int i = 0; i <= kLisSegs; ++i) {
    const int t = (i * vec::kSteps) / kLisSegs;
    const int x = (int)((kLisAmp * vec::sinT(a * t + phase)) >> 16);
    const int y = (int)((kLisAmp * vec::cosT(b * t)) >> 16);
    if (i) d.line(px, py, x, y);
    px = x; py = y;
  }
}

// ---- starfield -------------------------------------------------------------
// Each star is drawn as the streak between where it was and where it is, which
// is both how it reads as motion and how you draw a point on a display that
// only knows lines.
// Sparse is dim: brightness is beam-on time per refresh, and 30 short streaks
// leave the tube idle most of the frame however long the dwell gets. So: more
// stars, and a tail longer than one frame's travel so each streak is a streak
// rather than a tick. The tail is decoupled from the speed on purpose —
// lengthening it brightens the field without making everything rush past.
constexpr int kStars     = 64;
constexpr int kStarFocal = 700;
constexpr int kStarSpeed = 30;
constexpr int kStarTail  = 150;
constexpr int kStarNear  = 260;
constexpr int kStarFar   = 2600;
constexpr int kStarEdge  = 1180;      // past this it has flown by

int32_t stx[kStars], sty[kStars], stz[kStars];
uint32_t srng = 0x1234567u;

inline uint32_t sxr() {               // xorshift: deterministic and tiny
  srng ^= srng << 13; srng ^= srng >> 17; srng ^= srng << 5; return srng;
}

void respawn(int i) {
  stx[i] = (int32_t)(sxr() % 2401) - 1200;
  sty[i] = (int32_t)(sxr() % 2401) - 1200;
  stz[i] = kStarNear + (int32_t)(sxr() % (kStarFar - kStarNear));
}

void starfield(const ClockState& c, DrawList& d) {
  (void)c;
  static bool seeded = false;
  if (!seeded) { for (int i = 0; i < kStars; ++i) respawn(i); seeded = true; }

  for (int i = 0; i < kStars; ++i) {
    stz[i] -= kStarSpeed;
    if (stz[i] <= kStarNear) { respawn(i); continue; }
    const int32_t z1 = stz[i];
    const int32_t z0 = z1 + kStarTail;

    const int x0 = (int)(stx[i] * kStarFocal / z0);
    const int y0 = (int)(sty[i] * kStarFocal / z0);
    const int x1 = (int)(stx[i] * kStarFocal / z1);
    const int y1 = (int)(sty[i] * kStarFocal / z1);

    // Off the edge means it has passed the viewer. Clamping instead would
    // smear it along the border, which reads as a bug rather than a star.
    if (x1 < -kStarEdge || x1 > kStarEdge || y1 < -kStarEdge || y1 > kStarEdge) {
      respawn(i);
      continue;
    }
    d.line(x0, y0, x1, y1);
  }
}

RenderFn kFaces[] = { hands, numbers, digital, datetime, cube, lissajous, starfield };

// Contiguous runs of kFaces above. Keep the two in step.
struct Family { uint8_t first, count; };
const Family kFamilies[] = {
  { 0, 2 },   // analog:    roman dial, numbered dial
  { 2, 2 },   // digital:   time, time with date
  { 4, 3 },   // animation: cube, Lissajous, starfield
};
constexpr uint8_t kFamilyCount = sizeof(kFamilies) / sizeof(kFamilies[0]);
uint8_t lastVariant[kFamilyCount] = { 0, 0, 0 };

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
