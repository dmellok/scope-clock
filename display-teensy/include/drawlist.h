// drawlist.h — the draw list is both the internal render IR and the wire format.
// Extends the original `item` with first-class Line/Circle for wireframe/grid.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

enum class ItemType : uint8_t { End, Text, Line, Circle };

struct Item {
  ItemType type = ItemType::End;
  int16_t  x = 0, y = 0;     // Text: baseline start.  Line: start.  Circle: center.
  int16_t  x2 = 0, y2 = 0;   // Line: end.             Circle: (radius in x2)
  int16_t  scale = 10;
  const char* str = nullptr; // Text only
};

struct DrawList {
  static constexpr uint8_t CAP = 32;
  Item     items[CAP];
  uint8_t  count = 0;

  void clear() { count = 0; }
  void text(int x, int y, int scale, const char* s) {
    if (count < CAP) items[count++] = Item{ItemType::Text, (int16_t)x, (int16_t)y, 0, 0, (int16_t)scale, s};
  }
  void line(int x0, int y0, int x1, int y1) {
    if (count < CAP) items[count++] = Item{ItemType::Line, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, 0, nullptr};
  }
  void circle(int cx, int cy, int r) {
    if (count < CAP) items[count++] = Item{ItemType::Circle, (int16_t)cx, (int16_t)cy, (int16_t)r, 0, 0, nullptr};
  }
};

// Decode a PushList payload (see shared/protocol.h) into `out`, copying any
// text into `arena` and pointing the items at it — Item holds a pointer, and
// the link's receive buffer is overwritten by the next frame.
//
// This is the only place untrusted bytes become a structure, so it validates
// everything: item count, per-item length against the remaining payload, and
// arena space. On any malformed input it returns false having emptied `out`,
// which blanks the display rather than rendering something half-parsed.
bool decodePushList(const uint8_t* payload, uint8_t len,
                    DrawList& out, char* arena, uint16_t arenaCap);
