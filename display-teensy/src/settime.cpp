// settime.cpp — set the clock and the calendar from the knob, with nothing
// else attached.
//
// The RTC holds local time and the bridge was the only thing that could write
// it, so a clock with no bridge could not be corrected at all: not for drift,
// not for a flat backup cell, and not for summer time. Everything else about
// this device is autonomous; the one thing a clock has to be able to do was not.
//
// One editor serves both kinds. They are the same interaction — three fields,
// turn to change, tap for the next — and the only real difference is that a
// day has to be checked against the month it lands in.
//
// This REPLACES the face rather than sitting over it. An editor you can only
// half see is worse than none — you need to know which field the knob is on
// before you turn it, and a dial behind the digits makes that ambiguous.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "settime.h"
#include "state.h"
#include "drawlist.h"
#include "text.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint8_t kSeg = 1;        // seven segment: this is a clock being set
constexpr int kDigitsY = -80;
constexpr int kTitleY  = 640;
constexpr int kHintY   = -800;
constexpr int kMaxSc   = 22;
constexpr int kWide    = 1700;     // the digits stay well inside the rim

const char* const kTimeHint[3] = { "TURN FOR HOURS", "TURN FOR MINUTES",
                                   "TURN FOR SECONDS" };
const char* const kDateHint[3] = { "TURN FOR DAY", "TURN FOR MONTH",
                                   "TURN FOR YEAR" };

}  // namespace

// Days in a month, with the leap rule. The device only ever holds a two-digit
// year against a hardcoded century, so the "divisible by 400" arm of the rule
// cannot be reached from here — but 2100 is not a leap year and someone will
// eventually run one of these in 2100, so it is written correctly rather than
// as `year % 4`.
uint8_t daysInMonth(uint8_t month, uint8_t year2) {
  static const uint8_t k[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  if (month < 1 || month > 12) return 31;
  if (month != 2) return k[month - 1];
  const int y = 2000 + year2;
  const bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
  return leap ? 29 : 28;
}

// The inclusive range a field may take. The day's upper bound moves with the
// month and the year, which is the whole reason this is a function.
void editRange(const DeviceState& dev, uint8_t field, int& lo, int& hi) {
  if (dev.edit == DeviceState::Edit::Date) {
    switch (field) {
      case 0: lo = 1; hi = daysInMonth(dev.editVal[1], dev.editVal[2]); return;
      case 1: lo = 1; hi = 12; return;
      default: lo = 0; hi = 99; return;      // 20xx; the century is fixed
    }
  }
  lo = 0;
  hi = field == 0 ? 23 : 59;
}

// Called after any change, because moving off a 31st into February has to put
// the day somewhere real rather than leave the RTC holding the 31st of Feb.
void clampDay(DeviceState& dev) {
  if (dev.edit != DeviceState::Edit::Date) return;
  const uint8_t max = daysInMonth(dev.editVal[1], dev.editVal[2]);
  if (dev.editVal[0] > max) dev.editVal[0] = max;
  if (dev.editVal[0] < 1)   dev.editVal[0] = 1;
}

void overlaySetTime(DeviceState& dev, DrawList& list) {
  if (dev.edit == DeviceState::Edit::None) return;
  list.clear();

  const bool date = dev.edit == DeviceState::Edit::Date;

  // One buffer, kept static: an Item holds the pointer it was handed, not a
  // copy, and this one has to outlive the call.
  static char buf[16];
  if (date)
    snprintf(buf, sizeof buf, "%02u-%02u-%02u",
             (unsigned)dev.editVal[0], (unsigned)dev.editVal[1],
             (unsigned)dev.editVal[2]);
  else
    snprintf(buf, sizeof buf, "%02u:%02u:%02u",
             (unsigned)dev.editVal[0], (unsigned)dev.editVal[1],
             (unsigned)dev.editVal[2]);

  // Ink width is exactly linear in scale below 40, so the fitting scale is a
  // division rather than a search — same trick as the notification strips.
  int sc = kMaxSc;
  const int unit = txt::inkWidth(1, buf, kSeg);
  if (unit > 0) {
    sc = kWide / unit;
    if (sc > kMaxSc) sc = kMaxSc;
    if (sc < 4) sc = 4;
  }

  const int total = txt::inkWidth(sc, buf, kSeg);
  const int x0 = -total / 2;
  list.text(x0, kDigitsY, sc, buf, kSeg);

  // Where each field starts, measured rather than assumed: the separator is
  // narrower than a digit in this face, so three equal thirds would put the
  // marker under the wrong place.
  static char pre[8];
  int fx = x0;
  if (dev.editField) {
    const int n = dev.editField * 3;                 // "DD-" or "DD-MM-"
    memcpy(pre, buf, (size_t)n);
    pre[n] = '\0';
    fx = x0 + txt::measure(sc, pre, kSeg);
  }
  static char two[3];
  two[0] = buf[0]; two[1] = buf[1]; two[2] = '\0';
  const int fw = txt::inkWidth(sc, two, kSeg);

  // Blinking, because a static underline reads as decoration. Two thirds on so
  // it is never absent long enough to look broken.
  if ((millis() % 900) < 600) {
    const int uy = kDigitsY - 70;
    list.line(fx, uy, fx + fw, uy);
  }

  txt::centredFit(list, kTitleY, 7, date ? "SET DATE" : "SET TIME");
  const uint8_t f = dev.editField < 3 ? dev.editField : 0;
  txt::centredFit(list, kHintY, 6, date ? kDateHint[f] : kTimeHint[f]);
  txt::centredFit(list, kHintY - 190, 5, "TAP FOR NEXT");
}
