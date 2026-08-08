// nowplaying.cpp — decode and hold the current track.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "nowplaying.h"
#include <Arduino.h>

namespace np {
namespace {
Track cur;

// Copy at most cap-1 bytes and always terminate. The two length bytes are host
// supplied, so they are the only thing here worth distrusting.
void field(char* dst, uint8_t cap, const uint8_t* src, uint8_t n) {
  const uint8_t k = n < (uint8_t)(cap - 1) ? n : (uint8_t)(cap - 1);
  for (uint8_t i = 0; i < k; ++i) dst[i] = (char)src[i];
  dst[k] = '\0';
}
} // namespace

// [flags:u8][durationS:u16][progressS:u16][titleLen:u8][artistLen:u8]
// [title][artist][album]
void set(const uint8_t* p, uint8_t len) {
  if (len < 7) return;
  const uint8_t tl = p[5], al = p[6];
  // Two lengths that must both fit inside what is left of the payload, or the
  // album read would start past the end.
  if ((uint16_t)tl + al + 7 > len) return;

  cur.playing   = (p[0] & 1) != 0;
  cur.durationS = (uint16_t)(p[1] | (p[2] << 8));
  cur.progressS = (uint16_t)(p[3] | (p[4] << 8));
  field(cur.title,  sizeof cur.title,  p + 7, tl);
  field(cur.artist, sizeof cur.artist, p + 7 + tl, al);
  field(cur.album,  sizeof cur.album,  p + 7 + tl + al, (uint8_t)(len - 7 - tl - al));
  cur.stampMs = millis();
  cur.valid = true;
}

const Track& track() { return cur; }

uint16_t elapsed() {
  if (!cur.valid) return 0;
  uint32_t e = cur.progressS;
  if (cur.playing) e += (millis() - cur.stampMs) / 1000;
  if (cur.durationS && e > cur.durationS) e = cur.durationS;
  return (uint16_t)e;
}

}  // namespace np
