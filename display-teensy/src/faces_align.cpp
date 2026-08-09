// faces_align.cpp — the centring target.
//
// The tube's usable radius was originally measured by pushing concentric rings
// over the wire and looking at which one touched the glass. This is that, made
// permanent: rings at known radii, so "is the picture centred and how much of
// the phosphor am I using" is answerable at a glance instead of by pushing a
// scene.
//
// Two things have to be switched off for it to mean anything, and both are
// handled by the main loop rather than here: the per-face scale, because a
// reference whose size depends on a setting is not a reference, and the
// anti-burn-in drift, because aligning against a target that is itself
// wandering 45 counts round a circle is pointless.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"
#include <stdio.h>

namespace faces { namespace impl {
namespace {

// Device units. 1200 is the working edge, which the render scales by 3/2 to
// land on the tube's measured 1800-count rim — so the outer ring should sit
// exactly on the glass. If it does not, that is the answer.
constexpr int kRings[] = { 400, 800, 1200 };

}  // namespace

void align(const ClockState&, DrawList& d) {
  for (int r : kRings) d.circle(0, 0, r);

  // Centre cross, small enough that it is clearly the middle and not a face.
  d.line(-160, 0, 160, 0);
  d.line(0, -160, 0, 160);

  // Ticks at the cardinals, drawn inward from the rim so a clipped one tells
  // you which way the picture has moved.
  constexpr int kT = 120;
  d.line( 1200 - kT, 0,  1200, 0);
  d.line(-1200 + kT, 0, -1200, 0);
  d.line(0,  1200 - kT, 0,  1200);
  d.line(0, -1200 + kT, 0, -1200);

  // The offsets currently applied, so the page and the tube agree. One buffer
  // is enough because it is one line; an Item keeps the pointer it is handed.
  static char lab[24];
  snprintf(lab, sizeof lab, "X %+d  Y %+d", vec::alignX(), vec::alignY());
  txt::centredFit(d, -560, 7, lab);
  txt::centredFit(d, 480, 6, "CENTRING");
}

}}  // namespace faces::impl
