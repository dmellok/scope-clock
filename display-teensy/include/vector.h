// vector.h — the crown jewel: beam steering, circle generator, line drawer.
// Ported from SCTVcode d_drawing.ino (DoSeg) + SCTVcode.ino (sin/cos tables).
// The tuned beam timing (motionDelay/settlingDelay/glowDelay) lives in vector.cpp
// and must not be "cleaned up" — it is what keeps circles clean.
//
// Coordinates here are DISPLAY UNITS: signed, centred on (0,0), roughly
// +/-1250 visible.  vec:: adds the DAC midpoint and any centring/screensaver
// offset on the way out.  Nothing above this layer knows about DAC counts.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
struct DrawList;

namespace vec {

// Angular resolution of the sin/cos tables: one full turn = kSteps entries.
constexpr int kSteps = 1024;

void init();                              // fill sin/cos tables, park the beam
void renderFrame(const DrawList& list);   // draw every item once (one refresh)

// primitives (used by text.cpp and faces) — all take display units
void line(int x0, int y0, int x1, int y1);
void arc(int cx, int cy, int r);          // full circle, radius r

// Ellipse/arc segment.  firstO/lastO are the font's octant numbers: octant n
// starts at n*45 degrees measured CCW from East, and lastO is inclusive, so a
// full circle is 6..13.  Text glyphs are built almost entirely out of these.
void ellipseArc(int cx, int cy, int xrad, int yrad, int firstO, int lastO);

// Q16 trig, for faces that need to point something at an angle (clock hands).
// Index is 0..kSteps-1, CCW from East.  Result is -65536..+65536.
int32_t sinT(int idx);
int32_t cosT(int idx);

// Whole-display offsets, in display units. These two sum: the trim is the
// user's coarse centring, the screensaver a slow wander on top of it.
void setTrim(int x, int y);        // analog centring pots
void updateScreenSaver(int hour);  // nudges the display once an hour, anti-burn-in

} // namespace vec
