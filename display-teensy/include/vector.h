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

// The host's centring offset, in DAC counts, on top of the pots. The pots are
// inside the case; this is the same adjustment from the config page, and it is
// additive so turning a pot still works. Persisted by the bridge.
void setAlign(int x, int y);
int  alignX();
int  alignY();

// Hold the anti-burn-in drift still without changing the user's setting. The
// calibration target is a fixed reference, and aligning against something that
// is itself wandering 45 counts round a circle is pointless.
void holdWobble(bool held);

// Beam dwell per dot, which IS brightness on a vector tube: the longer the beam
// sits on a dot, the more energy the phosphor takes. 255 keeps the beam drawing
// for nearly the whole refresh period; lower values finish sooner and leave the
// tube dark for the remainder, which reads as dim and flickery.
void setBrightness(uint8_t b);

// Feed back how long the last frame actually took, and how long it had. The
// dwell that produces a given brightness depends on how many dots the frame
// contains, so it cannot be a constant — this steers it toward filling the
// refresh period, whatever is being drawn.
void tuneDwell(uint32_t frameUs, uint32_t budgetUs);
void updateScreenSaver(int hour);  // nudges the display once an hour, anti-burn-in

// Continuous anti-burn-in drift, which supersedes the hourly nudge when on.
// tickWobble() must be called every loop, in every mode — burn does not care
// which mode put the image there.
void setWobble(bool on);
bool wobble();
void tickWobble();

} // namespace vec
