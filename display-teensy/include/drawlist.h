// drawlist.h — the draw list is both the internal render IR and the wire format.
// Extends the original `item` with first-class Line/Circle for wireframe/grid.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
struct ClockState;

// Clock and Hand are the template primitives: they carry an intent rather than
// a finished shape, and are resolved against the RTC every refresh by
// expandTemplate() before anything is drawn.
enum class ItemType : uint8_t { End, Text, Line, Circle, Clock, Hand };

struct Item {
  ItemType type = ItemType::End;
  int16_t  x = 0, y = 0;     // Text: baseline start.  Line: start.  Circle: center.
  int16_t  x2 = 0, y2 = 0;   // Line: end.             Circle: (radius in x2)
  int16_t  scale = 10;
  uint8_t  font = 0;         // Text/Clock: 0 stroke, 1 seven segment
  const char* str = nullptr; // Text only
};

struct DrawList {
  // Curves need segments, and a Lissajous at 32 of them looks like a polygon.
  // Traced artwork needs more again: the cat outline is 129 segments at full
  // fidelity, and at CAP 128 the simplifier had to distort it to fit by one.
  // Chunked push (PushBegin/Chunk/Commit) is what makes lists this big
  // reachable over the wire at all.
  static constexpr uint8_t CAP = 192;
  Item     items[CAP];
  uint8_t  count = 0;

  void clear() { count = 0; }
  void text(int x, int y, int scale, const char* s, uint8_t font = 0) {
    if (count < CAP) items[count++] = Item{ItemType::Text, (int16_t)x, (int16_t)y, 0, 0, (int16_t)scale, font, s};
  }
  void line(int x0, int y0, int x1, int y1) {
    if (count < CAP) items[count++] = Item{ItemType::Line, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, 0, 0, nullptr};
  }
  void circle(int cx, int cy, int r) {
    if (count < CAP) items[count++] = Item{ItemType::Circle, (int16_t)cx, (int16_t)cy, (int16_t)r, 0, 0, 0, nullptr};
  }
  // scale carries the hand source (0 sec, 1 min, 2 hour); x2/y2 are the radii.
  void hand(int cx, int cy, int r0, int r1, int src) {
    if (count < CAP) items[count++] = Item{ItemType::Hand, (int16_t)cx, (int16_t)cy, (int16_t)r0, (int16_t)r1, (int16_t)src, 0, nullptr};
  }
};

// Scale a composed list about the origin, in percent. Applied to the FACE only,
// after centring and before the notification overlay, so a face can be sized to
// the tube without the overlay moving with it.
void scaleList(DrawList& list, int pct);

// Resolve a template in place: Clock items become Text with the RTC formatted
// into `scratch`, Hand items become Line with endpoints from the current time.
// Everything else is left alone, so a list with no template items is untouched.
//
// Done on a per-frame COPY of the retained list — the original keeps its format
// strings, which live in the arena and must survive being rendered.
void expandTemplate(DrawList& list, const ClockState& clk,
                    char* scratch, uint16_t scratchCap);

// Decode a PushList payload (see shared/protocol.h) into `out`, copying any
// text into `arena` and pointing the items at it — Item holds a pointer, and
// the link's receive buffer is overwritten by the next frame.
//
// This is the only place untrusted bytes become a structure, so it validates
// everything: item count, per-item length against the remaining payload, and
// arena space. On any malformed input it returns false having emptied `out`,
// which blanks the display rather than rendering something half-parsed.
bool decodePushList(const uint8_t* payload, uint16_t len,
                    DrawList& out, char* arena, uint16_t arenaCap);
