// faces.cpp — built-in clock faces that render from the RTC, so the clock is
// autonomous when the network is down. Ported from SCTVcode g_clocks.ino
// (DrawClk / DoHand / faceList / time6nList / makeTimeStrings).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "face.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"

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

// Analog face: Roman numerals at trial-and-error positions, a hub circle, and
// three hands. The hour and minute hands are drawn twice so they come out
// brighter than the second hand — on a CRT, brightness is redraw count.
// Having positions on the numerals also opts this list out of centring.
void hands(const ClockState& c, DrawList& d) {
  d.text(  490,  760, 10, "I");
  d.text(  820,  400, 10, "II");
  d.text(  900, -100, 10, "III");
  d.text(  740, -590, 10, "IIII");
  d.text(  400, -960, 10, "V");
  d.text( -100,-1080, 10, "VI");
  d.text( -600, -960, 10, "VII");
  d.text(-1000, -600, 10, "VIII");
  d.text(-1040, -100, 10, "IX");
  d.text( -940,  400, 10, "X");
  d.text( -600,  760, 10, "XI");
  d.text( -160,  880, 10, "XII");

  d.circle(0, 0, 90);                                   // hub

  const int minAngle = c.second / 15 + c.minute * 4;
  const int hrAngle  = (c.hour % 12) * 20 + c.minute / 3;
  hand(d, 2500, c.second * 4);
  hand(d, 2000, minAngle);
  hand(d, 1500, hrAngle);
  hand(d, 2000, minAngle);                              // again, for brightness
  hand(d, 1500, hrAngle);
}

RenderFn kFaces[] = { hands, digital };

} // namespace

void     registerBuiltins() { /* static table for now */ }
uint8_t  count() { return sizeof(kFaces) / sizeof(kFaces[0]); }
RenderFn current(const DeviceState& dev) {
  return kFaces[dev.faceId < count() ? dev.faceId : 0];
}

} // namespace faces
