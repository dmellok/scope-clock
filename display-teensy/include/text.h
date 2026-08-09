// text.h — font/segment rendering + centering. Port b_font / d_drawing / Center().
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
struct DrawList;

namespace txt {
  // font 0 is the stroke face (and the only one with katakana); font 1 is
  // seven segment, which covers digits, hex letters and little else on purpose.
  void  drawString(int x, int y, int scale, const char* s, uint8_t font = 0);
  int   measure(int scale, const char* s, uint8_t font = 0);  // incl. trailing kern
  int   kern(int scale);                     // inter-character advance
  int   height(int scale);                   // cell height, for vertical centring

  // Ink width: what measure() reports less the trailing kern, i.e. the extent
  // actually drawn. This is the one to halve when centring on a point.
  inline int inkWidth(int scale, const char* s, uint8_t font = 0) {
    return measure(scale, s, font) - kern(scale);
  }
  void  centerLines(DrawList& list);          // fills in x for line-centered items
}
