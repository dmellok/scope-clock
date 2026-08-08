// debug.h — console breadcrumbs on the front USB jack's serial interface.
//
// Non-blocking by construction: every write is gated on availableForWrite(), so
// a console nobody has opened costs nothing and can never stall the refresh.
// That is the same rule the bridge link follows, and for the same reason.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Arduino.h>

namespace dbg {

inline void say(const char* s) {
  const int n = (int)strlen(s);
  if (Serial.availableForWrite() < n + 2) return;   // console absent or behind
  Serial.println(s);
}

inline void sayf(const char* fmt, ...) {
  char buf[96];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  say(buf);
}

// MK66 reset status registers. Bit names from the reference manual, section
// 20.3 — the ones that matter here are WDOG (our watchdog fired, which after a
// hard fault is what a hang looks like) and POR/PIN (a real power-up or reset).
inline void resetCause() {
  const uint8_t s0 = RCM_SRS0, s1 = RCM_SRS1;
  sayf("boot: SRS0=%02x SRS1=%02x%s%s%s%s%s", s0, s1,
       (s0 & 0x20) ? " WDOG"   : "",
       (s0 & 0x80) ? " POR"    : "",
       (s0 & 0x40) ? " PIN"    : "",
       (s1 & 0x02) ? " LOCKUP" : "",
       (s1 & 0x04) ? " SW"     : "");
}

}  // namespace dbg
