// radar.h — contacts on a bearing and a range, plus a footer line.
//
// Deliberately generic, for the same reason gauges is: the immediate use is
// hosts on the network placed by ping RTT, but the payload says nothing about
// what a contact is, so anything with a direction and a distance can drive it.
//
// The sweep is NOT in here. The host sends contacts when they change and the
// device runs the sweep itself off millis(), which is the bargain nowplaying
// already makes with its progress ring — a scan every few seconds down a link
// that wedges is the thing that pattern exists to avoid.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace rdr {

// Twelve is a frame-budget number, not an arbitrary one. Every contact costs a
// blip, and the ones the sweep has just passed also cost a label; see the face
// for why only a slice of them are labelled at once.
constexpr uint8_t kMax = 12;

enum : uint8_t { FlagNew = 0x01, FlagSelf = 0x02 };

struct Contact {
  uint8_t bearing = 0;    // 0..255 over a full turn, 0 = north, clockwise
  uint8_t range   = 0;    // 0..255 as a fraction of the outer ring
  uint8_t flags   = 0;
  char    label[14] = {0};
};

struct Set {
  uint8_t count = 0;
  Contact c[kMax];        // one label buffer PER contact; see gauges.cpp
  char    footer[40] = {0};
  bool    valid = false;
};

void set(const uint8_t* payload, uint8_t len);
const Set& get();

}  // namespace rdr
