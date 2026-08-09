// zones.h — other people's clocks.
//
// The offsets are MINUTES RELATIVE TO THIS DEVICE'S LOCAL TIME, not to UTC, and
// that is the whole design. The RTC holds local time and the MCU is forbidden a
// timezone table (hard rule 4), so the host — which knows both its own zone and
// everyone else's, DST included — does the subtraction and sends a plain number
// of minutes to add. When a zone crosses into summer time the host re-sends; the
// device never has to know such a thing exists.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace zones {

constexpr uint8_t kMax = 5;

struct Zone {
  int16_t deltaMin = 0;
  char    label[14] = {0};
};

struct Set {
  uint8_t count = 0;
  Zone    z[kMax];
  bool    valid = false;
};

void set(const uint8_t* payload, uint8_t len);
const Set& get();

}  // namespace zones
