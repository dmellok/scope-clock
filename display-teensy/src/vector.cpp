// vector.cpp — ported from SCTVcode.ino (circle lookup tables, beam timing
// constants) and d_drawing.ino (DoSeg, updateScreenSaver).
//
// Every segment follows the same dance, and the delays between the steps are
// the tuned part — they exist because a CRT beam is a physical object:
//
//   1. move to the start point with the beam blanked
//   2. wait motion/motionDelay + settlingDelay  (long moves need longer)
//   3. unblank, wait glowDelay for the phosphor to come up
//   4. walk to the end point in fixed strides, one DAC write per dot
//   5. wait glowDelay again so the last dot is as bright as the rest, blank
//
// Each dot is written exactly once; the stride is chosen so the dots smear
// into an even line or arc. Change the delays and circles get lumpy.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vector.h"
#include "drawlist.h"
#include "text.h"
#include "hal/dac.h"
#include <Arduino.h>

namespace vec {
namespace {

// --- beam timing (SCTVcode.ino) — do not retune casually ------------------
constexpr int kMidDac        = 2048;  // DAC code at display centre
constexpr int kMotionDelay   = 15;    // us of settling per unit of beam travel
constexpr int kSettlingDelay = 8;     // us of fixed settling before unblanking
constexpr int kGlowDelay     = 2;     // us for the phosphor to come up / decay
constexpr int kCircleSpeed   = 200;   // angular step; bigger = faster, coarser
// A dot every 2 DAC counts, not every 1. The picture is 1.5x bigger than it was
// (see the display scale below), so holding the spacing at 1 would have cost 1.5x
// the beam travel for the same apparent density — and it put two faces over the
// frame budget. 2 counts is far finer than the beam spot on a 3600-count-wide
// tube, and tuneDwell raises the dwell to keep total beam-on time, so brightness
// is unchanged.
constexpr int kLineStride    = 2;     // linear step; bigger = faster, coarser

// --- display scale ----------------------------------------------------------
// Device units are not DAC counts. Every face, scene and overlay in this
// firmware is authored against a working edge of 1200, but the tube's usable
// radius is about 1800 DAC counts — measured by putting concentric rings on the
// screen and looking, which is the only way to know. Drawing 1:1 therefore used
// two thirds of the glass and left a dark ring all the way round.
//
// One scale here rather than 27 edited faces: line() and ellipseArc() are the
// only two primitives, text goes through them, and pushed scenes and the
// notification overlay do too. So device coordinates keep meaning what they
// always meant and the picture simply fills the tube.
constexpr int kFieldNum = 3, kFieldDen = 2;      // 1200 -> 1800
inline int sc(int v) { return v * kFieldNum / kFieldDen; }

int32_t sintab[kSteps];
int32_t costab[kSteps];

// Where the beam was left by the previous segment, in DAC counts. The move to
// the next segment's start is delayed in proportion to this distance.
int beamX = kMidDac;
int beamY = kMidDac;

// Two independent display offsets that sum: the user's centring pots, and the
// screensaver's slow anti-burn-in wander. Kept apart so the screensaver cannot
// stomp on the trim (and vice versa) when either updates.
int trimX = 0, trimY = 0;
int saveX = 0, saveY = 0;
int alnX  = 0, alnY  = 0;   // host centring, DAC counts
bool wobbleHeld = false;

// --- beam dwell, i.e. brightness --------------------------------------------
// On a vector CRT the brightness of a stroke is how long the beam sits on each
// dot. That used to be supplied by accident: analogWrite() was slow enough
// (~113 cycles a channel) that the dwell came for free. Writing the DAC
// directly made the beam sweep 3.2x faster and the picture went dim, with the
// leftover frame time showing up as a dark gap — the tube idle two thirds of
// every refresh.
//
// So the dwell is explicit now, and adjustable, which also gives Msg::Set-
// Brightness something real to drive.
//
// It also cannot be a fixed constant. What the eye reads as brightness is
// beam-on time per refresh, and the dwell needed to achieve that depends
// entirely on how many dots the frame happens to contain: a value tuned so the
// analog face fills its 16.7ms lands the cube face at 69%, and an arbitrary
// pushed list anywhere at all. Fix the dwell and every scene is a different
// brightness.
//
// So steer on the measured frame time instead — no dot counting, and it works
// for content nobody has seen yet. tuneDwell() nudges toward a target fraction
// of the refresh period; the previous frame is an excellent predictor because
// scenes change smoothly.
constexpr uint32_t kDwellCeiling = 600;  // cycles; a sparse frame cannot fill
                                         // the period without parking the beam
constexpr uint32_t kTargetPct    = 93;   // of the refresh period, at full
uint32_t dotDwell   = 120;
uint8_t  brightness = 255;

inline void dwell() {
  if (!dotDwell) return;
  const uint32_t t0 = ARM_DWT_CYCCNT;
  while ((ARM_DWT_CYCCNT - t0) < dotDwell) { /* hold the beam on this dot */ }
}

inline int toDacX(int x) { return x + trimX + saveX + alnX + kMidDac; }
inline int toDacY(int y) { return y + trimY + saveY + alnY + kMidDac; }

// How far the beam has to fly to reach (x,y), as the larger of the two axes.
inline int travelTo(int x, int y) {
  const int dx = abs(beamX - x), dy = abs(beamY - y);
  return dx > dy ? dx : dy;
}

// Steps 1-3 of the dance above: blanked move, settle, unblank, glow.
inline void beginStroke(int x, int y) {
  const int motion = travelTo(x, y);
  hal::dac::write(x, y);
  delayMicroseconds(motion / kMotionDelay + kSettlingDelay);
  hal::dac::blank(false);            // start making photons
  delayMicroseconds(kGlowDelay);
  beamX = x; beamY = y;
}

// Step 5: let the final dot glow as long as the others, then hide it.
inline void endStroke() {
  delayMicroseconds(kGlowDelay);
  hal::dac::blank(true);
}

} // namespace

void setBrightness(uint8_t b) { brightness = b; }

void tuneDwell(uint32_t frameUs, uint32_t budgetUs) {
  if (!budgetUs) return;
  const uint32_t targetUs = (uint64_t)budgetUs * kTargetPct * brightness / (100u * 255u);

  // Proportional, and deliberately gentle: a step per 200us of error settles a
  // face change in well under a second without hunting around the target.
  int32_t step = ((int32_t)targetUs - (int32_t)frameUs) / 200;
  if (step >  8) step =  8;
  if (step < -8) step = -8;
  if (step == 0) step = (targetUs > frameUs) ? 1 : (targetUs < frameUs ? -1 : 0);

  const int32_t next = (int32_t)dotDwell + step;
  dotDwell = next < 0 ? 0
           : ((uint32_t)next > kDwellCeiling ? kDwellCeiling : (uint32_t)next);
}

void init() {
  // The cycle counter is the dwell timebase; the Teensy 3 core leaves it off.
  ARM_DEMCR    |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;

  for (int i = 0; i < kSteps; ++i) {
    const double a = (double)i * 2.0 * PI / (double)kSteps;
    costab[i] = (int32_t)(65536.0 * cos(a));
    sintab[i] = (int32_t)(65536.0 * sin(a));
  }
  hal::dac::blank(true);
  hal::dac::write(kMidDac, kMidDac);
  beamX = beamY = kMidDac;
}

int32_t sinT(int idx) { return sintab[((idx % kSteps) + kSteps) % kSteps]; }
int32_t cosT(int idx) { return costab[((idx % kSteps) + kSteps) % kSteps]; }

void setTrim(int x, int y) { trimX = x; trimY = y; }

// Clamped well inside the DAC: past this the image is off the glass whatever
// else is right, so a bad value cannot lose the picture entirely.
void setAlign(int x, int y) {
  alnX = x < -600 ? -600 : (x > 600 ? 600 : x);
  alnY = y < -600 ? -600 : (y > 600 ? 600 : y);
}
int alignX() { return alnX; }
int alignY() { return alnY; }

void holdWobble(bool held) {
  wobbleHeld = held;
  if (held) { saveX = 0; saveY = 0; }
}

void line(int x0, int y0, int x1, int y1) {
  x0 = sc(x0); y0 = sc(y0); x1 = sc(x1); y1 = sc(y1);
  const int xs = toDacX(x0), ys = toDacY(y0);
  const int xlen = x1 - x0, ylen = y1 - y0;

  // Length, dodging the square root on the axis-aligned cases (most of them).
  int len;
  if (xlen == 0)      len = abs(ylen);
  else if (ylen == 0) len = abs(xlen);
  else                len = (int)sqrtf((float)(xlen * xlen + ylen * ylen));
  if (len <= 0) len = kLineStride;   // degenerate: still put one dot down

  // 24.8 fixed point per unit of length. Multiply rather than shift: a left
  // shift of a negative value is undefined, and half of every stroke runs in
  // the negative direction.
  const int xinc = (xlen * 256) / len;
  const int yinc = (ylen * 256) / len;

  beginStroke(xs, ys);
  for (int i = 0; i < len; i += kLineStride) {
    beamX = ((i * xinc) >> 8) + xs;
    beamY = ((i * yinc) >> 8) + ys;
    hal::dac::write(beamX, beamY);
    dwell();
  }
  endStroke();
}

void ellipseArc(int cx, int cy, int xrad, int yrad, int firstO, int lastO) {
  cx = sc(cx); cy = sc(cy); xrad = sc(xrad); yrad = sc(yrad);
  const int xcen = toDacX(cx), ycen = toDacY(cy);
  const int firstAngle =  firstO      * (kSteps >> 3);
  const int lastAngle  = (lastO + 1)  * (kSteps >> 3);
  if (lastAngle <= firstAngle) return;

  int bigness = (xrad > yrad ? xrad : yrad);
  if (bigness < 1) bigness = 1;
  int stride = (kCircleSpeed << 8) / bigness;   // 24.8, so small arcs step finely
  if (stride < 1) stride = 1;

  // kSteps is a power of two, so the wrap is a mask — this is the hot loop.
  constexpr int kMask = kSteps - 1;
  const int xs = ((costab[firstAngle & kMask] * xrad) >> 16) + xcen;
  const int ys = ((sintab[firstAngle & kMask] * yrad) >> 16) + ycen;

  beginStroke(xs, ys);
  for (int32_t i = (int32_t)firstAngle << 8; i < ((int32_t)lastAngle << 8); i += stride) {
    const int a = (int)(i >> 8) & kMask;
    beamX = (int)((costab[a] * xrad) >> 16) + xcen;
    beamY = (int)((sintab[a] * yrad) >> 16) + ycen;
    hal::dac::write(beamX, beamY);
    dwell();
  }
  endStroke();
}

void arc(int cx, int cy, int r) { ellipseArc(cx, cy, r, r, 6, 13); }

// Once an hour, shuffle the whole display around a small triangle raster. It is
// imperceptible frame to frame but spreads phosphor burn over a 4x15 unit area.
// Continuous anti-burn-in drift.
//
// The hourly raster below jumps the image to one of 31 places once an hour,
// which spreads burn but leaves the picture in one spot for a full hour at a
// time. A slow circular wander covers the same ground continuously and is
// imperceptible frame to frame: 45 DAC counts is 2.5% of the tube's radius, and
// a lap takes four minutes.
//
// saveX/saveY are in DAC counts, not device units — they are added after the
// display scale, which is what makes 45 mean 45 here.
bool wobbleOn = true;

void setWobble(bool on) {
  wobbleOn = on;
  if (!on) { saveX = 0; saveY = 0; }
}
bool wobble() { return wobbleOn; }

void tickWobble() {
  if (!wobbleOn || wobbleHeld) return;
  constexpr int32_t kRadius = 45;
  constexpr uint32_t kMsPerStep = 234;      // 1024 steps -> ~4 minutes a lap
  const int a = (int)((millis() / kMsPerStep) & (kSteps - 1));
  saveX = (int)((kRadius * costab[a]) >> 16);
  saveY = (int)((kRadius * sintab[a]) >> 16);
}

void updateScreenSaver(int hour) {
  // The wobble owns the offset when it is on; two things writing it would fight.
  if (wobbleOn) return;
  static const int kSavers = 31;
  static int scrX = 8;      // first tick lands on 9, the step nearest (0,0)
  static int lastHour = -1;
  // Both axes are abs(), so the raw walk runs 0..+60 — a DC offset, not a
  // wander. The original got away with that because the centring pots trim any
  // constant offset back out, but leaning on the trim to cancel a bias the
  // screensaver had no business introducing just burns half its range.
  // Subtracting half of each axis's swing straddles the walk across the
  // origin: same burn coverage, zero mean.
  static const int kXBias = 30, kYBias = 30;
  if (hour != lastHour) {
    if (++scrX >= kSavers) scrX = 0;
    saveX = 4  * abs(scrX     - kSavers / 2)       - kXBias;
    saveY = 15 * abs(scrX % 8 - (kSavers % 8) / 2) - kYBias;
    lastHour = hour;
  }
}

void renderFrame(const DrawList& list) {
  for (uint8_t i = 0; i < list.count; ++i) {
    const Item& it = list.items[i];
    switch (it.type) {
      case ItemType::Text:   txt::drawString(it.x, it.y, it.scale, it.str, it.font); break;
      case ItemType::Line:   line(it.x, it.y, it.x2, it.y2);                break;
      case ItemType::Circle: arc(it.x, it.y, it.x2);                        break;
      default: break;
    }
  }
}

} // namespace vec
