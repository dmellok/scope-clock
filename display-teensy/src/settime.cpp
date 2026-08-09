// settime.cpp — set the clock from the knob, with nothing else attached.
//
// The RTC holds local time and the bridge was the only thing that could write
// it, so a clock with no bridge could not be corrected at all: not for drift,
// not for a flat backup cell, and not for summer time. Everything else about
// this device is autonomous; the one thing a clock has to be able to do was not.
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

}  // namespace

void overlaySetTime(DeviceState& dev, DrawList& list) {
  if (!dev.timeMode) return;
  list.clear();

  // One buffer, kept static: an Item holds the pointer it was handed, not a
  // copy, and this one has to outlive the call.
  static char buf[16];
  snprintf(buf, sizeof buf, "%02u:%02u:%02u",
           (unsigned)dev.timeEdit[0], (unsigned)dev.timeEdit[1],
           (unsigned)dev.timeEdit[2]);

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

  // Where each field starts, measured rather than assumed: the colon is
  // narrower than a digit in this face, so three equal thirds would put the
  // marker under the wrong place.
  static char pre[8];
  int fx = x0;
  if (dev.timeField) {
    const int n = dev.timeField * 3;                 // "HH:" or "HH:MM:"
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

  list.text(-txt::inkWidth(7, "SET TIME") / 2, kTitleY, 7, "SET TIME");
  static const char* const kHint[3] = { "TURN FOR HOURS",
                                        "TURN FOR MINUTES",
                                        "TURN FOR SECONDS" };
  const char* hint = kHint[dev.timeField < 3 ? dev.timeField : 0];
  list.text(-txt::inkWidth(6, hint) / 2, kHintY, 6, hint);
  list.text(-txt::inkWidth(5, "TAP FOR NEXT") / 2, kHintY - 190, 5,
            "TAP FOR NEXT");
}
