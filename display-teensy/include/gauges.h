// gauges.h — up to four labelled percentages, plus a footer line.
//
// Deliberately generic. The immediate use is Claude's usage figures, but the
// shape of that data — a few things each some fraction full, and a line of
// context underneath — is the shape of most things worth putting on a clock,
// so the message says nothing about where the numbers came from.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace gauge {

constexpr uint8_t kMax = 4;

struct Set {
  uint8_t count = 0;
  uint8_t pct[kMax] = {0};
  char    label[kMax][10] = {{0}};
  char    footer[40] = {0};
  bool    valid = false;
};

void set(const uint8_t* payload, uint8_t len);
const Set& get();

}  // namespace gauge
