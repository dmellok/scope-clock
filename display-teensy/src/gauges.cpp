// gauges.cpp — decode SetGauges.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gauges.h"

namespace gauge {
namespace {
Set cur;
void field(char* dst, uint8_t cap, const uint8_t* src, uint8_t n) {
  const uint8_t k = n < (uint8_t)(cap - 1) ? n : (uint8_t)(cap - 1);
  for (uint8_t i = 0; i < k; ++i) dst[i] = (char)src[i];
  dst[k] = '\0';
}
} // namespace

// [n:u8]  then n x [pct:u8][labelLen:u8][label]  then the footer to the end.
//
// Every length is host supplied, so each one is checked against what is left of
// the payload before it is used — a truncated frame stops the parse rather than
// reading past the end of it.
void set(const uint8_t* p, uint8_t len) {
  if (len < 1) return;
  Set out;
  const uint8_t n = p[0] > kMax ? kMax : p[0];
  uint16_t at = 1;
  for (uint8_t i = 0; i < n; ++i) {
    if (at + 2 > len) return;                 // no room for pct + length
    const uint8_t pct = p[at], ll = p[at + 1];
    at += 2;
    if (at + ll > len) return;                // label runs past the frame
    out.pct[i] = pct > 100 ? 100 : pct;
    field(out.label[i], sizeof out.label[i], p + at, ll);
    at += ll;
    out.count = (uint8_t)(i + 1);
  }
  if (at < len) field(out.footer, sizeof out.footer, p + at, (uint8_t)(len - at));
  out.valid = true;
  cur = out;
}

const Set& get() { return cur; }

}  // namespace gauge
