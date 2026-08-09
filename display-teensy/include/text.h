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

  // Which face font 0 resolves to: 0 regular, 1 seven segment, 2 condensed,
  // 3 wide, 4 italic, 5 bold. A face that names a font explicitly is unaffected,
  // so this is a default rather than an override.
  void    setDefaultFace(uint8_t id);
  uint8_t defaultFaceId();
  uint8_t faceCount();

  // Ink width: what measure() reports less the trailing kern, i.e. the extent
  // actually drawn. This is the one to halve when centring on a point.
  inline int inkWidth(int scale, const char* s, uint8_t font = 0) {
    return measure(scale, s, font) - kern(scale);
  }
  void  centerLines(DrawList& list);          // fills in x for line-centered items

  // Centre a line on the ROUND field at height y, shrinking until its ink fits
  // the chord there rather than the full width — the bound near the top and
  // bottom of the glass is the chord, and it is far narrower than the diameter.
  // Measured at the lowest ink, because descenders hang below the baseline.
  // Returns the scale actually used, or 0 if nothing was drawn.
  int   centredFit(DrawList& list, int y, int maxScale, const char* s,
                   uint8_t font = 0, int fieldR = 1200);
}
