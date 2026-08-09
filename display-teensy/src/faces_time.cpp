// faces_time.cpp — the faces that actually tell the time, from the RTC, so the
// clock stays useful with no network at all.
// Ported in part from SCTVcode g_clocks.ino (DrawClk / DoHand / makeTimeStrings).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"
#include <stdio.h>

namespace faces { namespace impl {
namespace {

// Clock angles are in 240ths of a turn, 0 = North, increasing clockwise — which
// is neither the trig tables' origin (East) nor their direction (CCW). Swapping
// sin and cos on the way out fixes both at once.
inline int stepsOf(int angle240) {
  return (angle240 * vec::kSteps / 240) % vec::kSteps;
}
inline int minAngle(const ClockState& c) { return c.second / 15 + c.minute * 4; }
inline int hrAngle(const ClockState& c)  { return (c.hour % 12) * 20 + c.minute / 3; }

void hand(DrawList& d, int len, int angle240) {
  const int a  = stepsOf(angle240);
  const int ux = (int)(vec::sinT(a) / 500);       // unit vector, ~+/-131
  const int uy = (int)(vec::cosT(a) / 500);
  d.line(ux * 10 / 13, uy * 10 / 13,              // start at the hub's edge
         (len * ux) >> 8, (len * uy) >> 8);
}

// A radial mark between two radii, at a clock angle.
void spoke(DrawList& d, int r0, int r1, int angle240) {
  const int a = stepsOf(angle240);
  const int32_t sx = vec::sinT(a), cy = vec::cosT(a);
  d.line((int)((r0 * sx) >> 16), (int)((r0 * cy) >> 16),
         (int)((r1 * sx) >> 16), (int)((r1 * cy) >> 16));
}

const char* const kRoman[12]  = {"XII","I","II","III","IIII","V","VI","VII","VIII","IX","X","XI"};
const char* const kArabic[12] = {"12","1","2","3","4","5","6","7","8","9","10","11"};
constexpr int kDialRadius = 1000, kNumeralScale = 10;

// Each numeral centred on its own hour mark. The original carried hand-tuned
// left-edge coordinates that were not quite square — IX sat 55 units right of
// its mark, V 40 left — errors that cancelled, so the ring read as centred but
// irregular. Deriving position from the angle and the glyph's own ink width
// makes it exact, and self-adjusting if the scale or radius change.
void numerals(DrawList& d, const char* const* label) {
  for (int h = 0; h < 12; ++h) {
    const int a  = (h * vec::kSteps) / 12;
    const int cx = (int)((kDialRadius * vec::sinT(a)) >> 16);
    const int cy = (int)((kDialRadius * vec::cosT(a)) >> 16);
    d.text(cx - txt::inkWidth(kNumeralScale, label[h]) / 2,
           cy - txt::height(kNumeralScale) / 2,        // baseline, not centre
           kNumeralScale, label[h]);
  }
}

// The hour and minute hands are drawn twice so they come out brighter than the
// second hand: on a CRT, brightness is redraw count.
void dial(const ClockState& c, DrawList& d, const char* const* label) {
  numerals(d, label);
  d.circle(0, 0, 90);
  hand(d, 2500, c.second * 4);
  hand(d, 2000, minAngle(c));
  hand(d, 1500, hrAngle(c));
  hand(d, 2000, minAngle(c));
  hand(d, 1500, hrAngle(c));
}

} // namespace

void hands(const ClockState& c, DrawList& d)   { dial(c, d, kRoman);  }
void numbers(const ClockState& c, DrawList& d) { dial(c, d, kArabic); }

// A proper watch dial: sixty minute ticks with the hour marks longer and drawn
// twice, so the twelve read heavier without needing a second stroke width the
// display does not have.
void tickdial(const ClockState& c, DrawList& d) {
  for (int i = 0; i < 60; ++i) {
    const int a = i * 4;                        // 240ths of a turn
    if (i % 5 == 0) { spoke(d, 940, 1140, a); spoke(d, 940, 1140, a); }
    else              spoke(d, 1065, 1140, a);
  }
  d.circle(0, 0, 70);
  hand(d, 2300, c.second * 4);
  hand(d, 1900, minAngle(c));
  hand(d, 1350, hrAngle(c));
  hand(d, 1900, minAngle(c));
  hand(d, 1350, hrAngle(c));
}

// An orrery: three bodies on their own rings, seconds outermost. Reads as an
// instrument rather than a clock, which is the appeal.
void orbit(const ClockState& c, DrawList& d) {
  struct Body { int r, angle240, size; };
  const Body b[3] = {
    { 1080, c.second * 4, 60 },
    {  780, minAngle(c),  75 },
    {  480, hrAngle(c),   90 },
  };
  for (int i = 0; i < 3; ++i) d.circle(0, 0, b[i].r);       // the rings
  for (int i = 0; i < 3; ++i) {
    const int a = stepsOf(b[i].angle240);
    const int x = (int)((b[i].r * vec::sinT(a)) >> 16);
    const int y = (int)((b[i].r * vec::cosT(a)) >> 16);
    d.circle(x, y, b[i].size);
    d.circle(x, y, b[i].size);                              // twice: the body
  }                                                          // should read solid
  d.circle(0, 0, 55);
}

// Three arcs whose LENGTH is the time: a full ring at 59 seconds, empty at 0.
// Drawn as polylines because the arc primitive works in 45-degree octants,
// which is all the stroke font ever needed.
void sector(const ClockState& c, DrawList& d) {
  struct Ring { int r, num, den; };
  const Ring rings[3] = {
    { 1090, c.second, 60 },
    {  840, c.minute, 60 },
    {  590, c.hour % 12, 12 },
  };
  // The full ring first, then the arc over the top of it. Without the reference
  // ring an arc is just an arc — you cannot see what fraction of the way round
  // it is, which is the only thing this face has to say.
  //
  // The arc needs no second pass to stand out: it lies exactly on the ring, so
  // the beam already traces the elapsed portion twice and the remainder once.
  // Drawing it twice put the face at 95% of the frame budget at 23:59:59, which
  // is where the refresh rate starts to sag.
  for (int i = 0; i < 3; ++i) d.circle(0, 0, rings[i].r);
  for (int i = 0; i < 3; ++i) {
    const Ring& g = rings[i];
    if (g.num == 0) continue;
    // One segment per ~9 degrees, so a full arc is 40 items and the three arcs
    // plus their rings stay inside the 192 the list holds.
    const int segs = 1 + (g.num * 40) / g.den;
    int lx = 0, ly = 0;
    for (int s = 0; s <= segs; ++s) {
      const int a = (int)(((int32_t)s * g.num * vec::kSteps) / (g.den * segs));
      const int x = (int)((g.r * vec::sinT(a)) >> 16);
      const int y = (int)((g.r * vec::cosT(a)) >> 16);
      if (s) d.line(lx, ly, x, y);
      lx = x; ly = y;
    }
    // A radial tick at the leading end. The brightness step alone marks where
    // the arc stops, but only once your eye has adjusted; the tick says it
    // outright, and it is three short strokes.
    spoke(d, g.r - 85, g.r + 85, (g.num * 240) / g.den);
  }
  d.circle(0, 0, 60);
}

// Digital: HH:MM large with SS below. The leading empty row is a spacer that
// drops the block slightly, which reads as better centred than true centring.
void digital(const ClockState& c, DrawList& d) {
  static char hh[3], mm[4], ss[4];
  const int h = c.hr12 ? to12(c.hour) : c.hour;
  hh[0] = (char)('0' + h / 10); hh[1] = (char)('0' + h % 10); hh[2] = 0;
  if (c.hr12 && hh[0] == '0') { hh[0] = hh[1]; hh[1] = 0; }   // no leading zero
  // The minutes CARRY the row break, rather than a separate "\n" item doing it.
  // strWidth returns a negative width for a lone newline — it subtracts one kern
  // and has no cells to add — so an empty terminator item dragged the whole row
  // off centre by half a kern.
  mm[0] = (char)('0' + c.minute / 10); mm[1] = (char)('0' + c.minute % 10);
  mm[2] = '\n'; mm[3] = 0;
  ss[0] = (char)('0' + c.second / 10); ss[1] = (char)('0' + c.second % 10);
  ss[2] = '\n'; ss[3] = 0;

  // Seven segment, because that is what a digital clock is. The stroke face is
  // still what datetime and every other text face uses; this is the one place
  // the shape of the numerals is the point.
  // Scale 30, not the 40 the stroke face used: a segment glyph advances 14 cells
  // against the stroke font's 12, so the same digits are a sixth wider and
  // "10:08" at 40 ran 180 counts past the rim.
  constexpr uint8_t kSeg = 1;
  d.text(0, 0, 10, "\n", kSeg);
  d.text(0, 0, 30, hh, kSeg);
  d.text(0, 0, 30, ":", kSeg);
  d.text(0, 0, 30, mm, kSeg);
  d.text(0, 0, 22, ss, kSeg);
}

namespace {
const char* const kWday[7] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
const char* const kMon[12] = {"Jan","Feb","March","April","May","June",
                              "July","Aug","Sept","Oct","Nov","Dec"};
}

// Weekday, time, date — three rows laid out by txt::centerLines, which is what
// the '\n' terminators are for: the last item of a row carries it.
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

// "IT IS TWENTY FIVE PAST TEN" — the time as a sentence, rounded to five
// minutes the way a person would say it.
void wordclock(const ClockState& c, DrawList& d) {
  static const char* const kMins[12] = {
    "", "FIVE", "TEN", "QUARTER", "TWENTY", "TWENTY FIVE", "HALF",
    "TWENTY FIVE", "TWENTY", "QUARTER", "TEN", "FIVE"
  };
  static const char* const kHours[12] = {
    "TWELVE","ONE","TWO","THREE","FOUR","FIVE",
    "SIX","SEVEN","EIGHT","NINE","TEN","ELEVEN"
  };
  static char l2[20], l3[12], l4[16];

  const int slot = ((c.minute + 2) / 5) % 12;      // nearest five minutes
  // Past the half hour the sentence flips to "TO", and the hour it refers to
  // becomes the next one — "twenty to eleven", not "twenty to ten".
  const bool to = c.minute >= 33;
  int h = c.hour % 12;
  if (to) h = (h + 1) % 12;

  d.text(0, 0, 13, "IT IS\n");
  if (slot == 0) {
    snprintf(l3, sizeof l3, "%s\n", kHours[h]);
    d.text(0, 0, 26, l3);
    d.text(0, 0, 13, "O'CLOCK\n");
  } else {
    snprintf(l2, sizeof l2, "%s\n", kMins[slot]);
    snprintf(l4, sizeof l4, "%s\n", kHours[h]);
    d.text(0, 0, 20, l2);
    d.text(0, 0, 13, to ? "TO\n" : "PAST\n");
    d.text(0, 0, 26, l4);
  }
}

// BCD: a column per digit, a dot per bit, filled for one and hollow for zero.
// A vector display has no fill, so "filled" is a small circle drawn twice —
// brightness standing in for solidity.
void binary(const ClockState& c, DrawList& d) {
  const int digits[6] = { c.hour / 10, c.hour % 10, c.minute / 10,
                          c.minute % 10, c.second / 10, c.second % 10 };
  constexpr int kCol = 340, kRow = 300, kOn = 78, kOff = 26;
  for (int col = 0; col < 6; ++col) {
    const int x = (col - 3) * kCol + kCol / 2;
    for (int bit = 0; bit < 4; ++bit) {
      if (col % 2 == 0 && bit > 2) continue;      // tens digits never exceed 5
      const int y = (bit - 2) * kRow + kRow / 2;
      if (digits[col] & (1 << bit)) { d.circle(x, y, kOn); d.circle(x, y, kOn); }
      else                            d.circle(x, y, kOff);
    }
  }
}

}}  // namespace faces::impl
