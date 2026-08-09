// drawlist.cpp — decode a PushList payload from the host into a DrawList.
//
// Every byte here arrived over the wire. The CRC catches corruption but not a
// buggy or hostile host, so lengths are checked against what is actually left
// rather than trusted, and the string arena is bounded. The rule throughout:
// never advance past `end`, never write past `arenaCap`.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "drawlist.h"
#include "state.h"
#include "vector.h"

namespace {

// Little-endian, and explicitly not a cast through int16_t* — the payload sits
// at an arbitrary offset in the receive buffer, so it need not be aligned.
inline int16_t rd16(const uint8_t* p) {
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

} // namespace

bool decodePushList(const uint8_t* payload, uint16_t len,
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
      case ItemType::Clock: {
        // Same shape as Text; the string is a format, resolved per frame.
        if (end - p < 7) { out.clear(); return false; }
        const int16_t x     = rd16(p); p += 2;
        const int16_t y     = rd16(p); p += 2;
        const int16_t scale = rd16(p); p += 2;
        const uint8_t slen  = *p++;
        if (end - p < slen) { out.clear(); return false; }
        if (arenaUsed + slen + 1u > arenaCap) { out.clear(); return false; }
        char* dst = arena + arenaUsed;
        for (uint8_t k = 0; k < slen; ++k) dst[k] = (char)p[k];
        dst[slen] = '\0';
        arenaUsed += slen + 1u;
        p += slen;
        if (out.count < DrawList::CAP)
          out.items[out.count++] = Item{ItemType::Clock, x, y, 0, 0, scale, 0, dst};
        break;
      }
      case ItemType::Hand: {
        if (end - p < 9) { out.clear(); return false; }
        const int16_t cx = rd16(p); p += 2;
        const int16_t cy = rd16(p); p += 2;
        const int16_t r0 = rd16(p); p += 2;
        const int16_t r1 = rd16(p); p += 2;
        const uint8_t src = *p++;
        if (src > 2) { out.clear(); return false; }   // only sec/min/hour exist
        out.hand(cx, cy, r0, r1, src);
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

// ---- template expansion ----------------------------------------------------

namespace {

// Two digits, or one when `pad` is false — matching how the built-in digital
// face drops the leading zero from a 12-hour reading.
inline uint16_t put2(char* out, uint16_t cap, uint16_t at, int v, bool pad) {
  if (v < 0) v = 0;
  if (v >= 10 || pad) { if (at < cap) out[at++] = (char)('0' + (v / 10) % 10); }
  if (at < cap) out[at++] = (char)('0' + v % 10);
  return at;
}

int to12(int h) { return h == 0 ? 12 : (h > 12 ? h - 12 : h); }

// Clock angles are in 240ths of a turn, 0 = North, clockwise — the same
// convention the built-in analog face uses.
int handAngle240(const ClockState& c, int src) {
  switch (src) {
    case 1:  return c.second / 15 + c.minute * 4;
    case 2:  return (c.hour % 12) * 20 + c.minute / 3;
    default: return c.second * 4;
  }
}

} // namespace

void expandTemplate(DrawList& list, const ClockState& clk,
                    char* scratch, uint16_t scratchCap) {
  uint16_t used = 0;

  for (uint8_t i = 0; i < list.count; ++i) {
    Item& it = list.items[i];

    if (it.type == ItemType::Hand) {
      const int src = it.scale;
      const int a   = (handAngle240(clk, src) * vec::kSteps / 240) % vec::kSteps;
      // Swap sin/cos to turn "CCW from east" into "clockwise from north".
      const int32_t ux = vec::sinT(a), uy = vec::cosT(a);
      const int r0 = it.x2, r1 = it.y2;
      const int cx = it.x,  cy = it.y;
      it.type = ItemType::Line;
      it.x  = (int16_t)(cx + ((r0 * ux) >> 16));
      it.y  = (int16_t)(cy + ((r0 * uy) >> 16));
      it.x2 = (int16_t)(cx + ((r1 * ux) >> 16));
      it.y2 = (int16_t)(cy + ((r1 * uy) >> 16));
      it.scale = 0;
      continue;
    }

    if (it.type != ItemType::Clock) continue;

    // Scratch exhausted: emit nothing rather than a terminator past the end.
    // A list of 32 wide formats does run it out, and the old guard only
    // clamped the characters — the trailing NUL still landed one past `used`,
    // which by then was outside the buffer.
    if (used >= scratchCap) {
      it.type = ItemType::Text;
      it.str  = "";
      continue;
    }

    const char* f = it.str;
    char* dst = scratch + used;
    uint16_t at = 0;
    const uint16_t cap = (uint16_t)(scratchCap - used - 1);   // room before the NUL

    for (; f && *f && at < cap; ++f) {
      if (*f != '%') { dst[at++] = *f; continue; }
      switch (*++f) {
        case 'H': at = put2(dst, cap, at, clk.hour, true);            break;
        case 'I': at = put2(dst, cap, at, to12(clk.hour), false);     break;
        case 'M': at = put2(dst, cap, at, clk.minute, true);          break;
        case 'S': at = put2(dst, cap, at, clk.second, true);          break;
        case 'd': at = put2(dst, cap, at, clk.day, true);             break;
        case 'm': at = put2(dst, cap, at, clk.month, true);           break;
        case 'y': at = put2(dst, cap, at, clk.year, true);            break;
        case '%': dst[at++] = '%';                                    break;
        case '\0': --f;                                              break;
        default:  break;      // unknown token expands to nothing
      }
    }
    dst[at] = '\0';
    used += at + 1;

    it.type = ItemType::Text;
    it.str  = dst;
  }
}

// Scale about the origin. Text carries its size in `scale`, so that has to move
// with the geometry or the letters stay put while the layout grows around them.
void scaleList(DrawList& list, int pct) {
  if (pct == 100) return;
  auto s16 = [&](int16_t v) { return (int16_t)((int32_t)v * pct / 100); };
  for (uint8_t i = 0; i < list.count; ++i) {
    Item& it = list.items[i];
    it.x = s16(it.x); it.y = s16(it.y);
    switch (it.type) {
      case ItemType::Line:
      case ItemType::Hand:   it.x2 = s16(it.x2); it.y2 = s16(it.y2); break;
      case ItemType::Circle: it.x2 = s16(it.x2); break;      // x2 is the radius
      case ItemType::Text:
      case ItemType::Clock:  it.scale = s16(it.scale); if (it.scale < 1) it.scale = 1; break;
      default: break;
    }
  }
}
