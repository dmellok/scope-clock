// text.h — font/segment rendering + centering. Port b_font / d_drawing / Center().
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
struct DrawList;

namespace txt {
  void  drawString(int x, int y, int scale, const char* s);
  int   measure(int scale, const char* s);   // width in display units, for centering
  void  centerLines(DrawList& list);          // fills in x for line-centered items
}
