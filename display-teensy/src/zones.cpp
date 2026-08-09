// zones.cpp — decode SetZones.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "zones.h"

namespace zones {
namespace {
Set cur;
} // namespace

// [n:u8] then n x [deltaMin:i16][labelLen:u8][label]
//
// Every length is host supplied, so each is checked against what is left of the
// payload before it is used; a truncated frame stops the parse rather than
// reading past the end of it.
void set(const uint8_t* p, uint8_t len) {
  if (len < 1) return;
  Set out;
  const uint8_t n = p[0] > kMax ? kMax : p[0];
  uint16_t at = 1;
  for (uint8_t i = 0; i < n; ++i) {
    if (at + 3 > len) return;
    const int16_t d = (int16_t)(p[at] | (p[at + 1] << 8));
    const uint8_t ll = p[at + 2];
    at += 3;
    if (at + ll > len) return;
    out.z[i].deltaMin = d;
    const uint8_t k = ll < (uint8_t)(sizeof(out.z[i].label) - 1)
                    ? ll : (uint8_t)(sizeof(out.z[i].label) - 1);
    for (uint8_t j = 0; j < k; ++j) out.z[i].label[j] = (char)p[at + j];
    out.z[i].label[k] = '\0';
    at += ll;
    out.count = (uint8_t)(i + 1);
  }
  out.valid = true;
  cur = out;
}

const Set& get() { return cur; }

}  // namespace zones
