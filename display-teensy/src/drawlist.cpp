// drawlist.cpp — decode a PushList payload from the host into a DrawList.
//
// Every byte here arrived over the wire. The CRC catches corruption but not a
// buggy or hostile host, so lengths are checked against what is actually left
// rather than trusted, and the string arena is bounded. The rule throughout:
// never advance past `end`, never write past `arenaCap`.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "drawlist.h"

namespace {

// Little-endian, and explicitly not a cast through int16_t* — the payload sits
// at an arbitrary offset in the receive buffer, so it need not be aligned.
inline int16_t rd16(const uint8_t* p) {
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

} // namespace

bool decodePushList(const uint8_t* payload, uint8_t len,
                    DrawList& out, char* arena, uint16_t arenaCap) {
  out.clear();
  if (!payload || len < 1 || !arena || arenaCap == 0) return false;

  const uint8_t* p   = payload;
  const uint8_t* end = payload + len;

  const uint8_t count = *p++;
  if (count > DrawList::CAP) return false;      // would not fit the list

  uint16_t arenaUsed = 0;

  for (uint8_t i = 0; i < count; ++i) {
    if (p >= end) { out.clear(); return false; }
    const uint8_t tag = *p++;

    switch (static_cast<ItemType>(tag)) {
      case ItemType::Text: {
        if (end - p < 7) { out.clear(); return false; }   // x,y,scale,len
        const int16_t x     = rd16(p); p += 2;
        const int16_t y     = rd16(p); p += 2;
        const int16_t scale = rd16(p); p += 2;
        const uint8_t slen  = *p++;
        if (end - p < slen) { out.clear(); return false; }
        if (arenaUsed + slen + 1u > arenaCap) { out.clear(); return false; }

        char* dst = arena + arenaUsed;
        for (uint8_t k = 0; k < slen; ++k) dst[k] = (char)p[k];
        dst[slen] = '\0';                 // the wire form is not terminated
        arenaUsed += slen + 1u;
        p += slen;

        out.text(x, y, scale, dst);
        break;
      }
      case ItemType::Line: {
        if (end - p < 8) { out.clear(); return false; }
        const int16_t x0 = rd16(p); p += 2;
        const int16_t y0 = rd16(p); p += 2;
        const int16_t x1 = rd16(p); p += 2;
        const int16_t y1 = rd16(p); p += 2;
        out.line(x0, y0, x1, y1);
        break;
      }
      case ItemType::Circle: {
        if (end - p < 6) { out.clear(); return false; }
        const int16_t cx = rd16(p); p += 2;
        const int16_t cy = rd16(p); p += 2;
        const int16_t r  = rd16(p); p += 2;
        out.circle(cx, cy, r);
        break;
      }
      default:
        out.clear();                      // unknown tag: cannot know its length
        return false;
    }
  }
  return true;
}
