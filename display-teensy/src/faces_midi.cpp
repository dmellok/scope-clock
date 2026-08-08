// faces_midi.cpp — draw what is being played, from USB-MIDI on the front jack.
//
// The premise of oscilloscope music is that an X-Y display shows the RATIO
// between two signals, not the signals. Feed a scope a perfect fifth and you get
// a three-lobed figure because 3:2 is what a fifth *is*. So these faces do not
// visualise MIDI as bars or notes — they build the figure the interval would
// have drawn, from just-intonation ratios, and let the chord be the shape.
//
// The beam redraws a static list 60 times a second, so it cannot trace a
// waveform at audio rate the way a real scope-music track does. What it can do
// is draw the same geometry, which is the part you actually see.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "faces_impl.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "hal/midi.h"

namespace faces { namespace impl {
namespace {

// Just intonation, one octave. These are the small whole-number ratios the
// intervals are named after, and small whole numbers are exactly what makes a
// Lissajous figure closed and legible — the equal-tempered fifth is 1.4983,
// which draws a figure that never quite closes and reads as a smear.
const uint8_t kNum[12] = { 1,16, 9, 6, 5, 4, 7, 3, 8, 5,16,15 };
const uint8_t kDen[12] = { 1,15, 8, 5, 4, 3, 5, 2, 5, 3, 9, 8 };

constexpr int kMaxRate  = 16;    // beyond this the figure is hash, not a figure
constexpr int kAmp      = 1000;
constexpr int kMaxSegs  = 150;   // leaves headroom under DrawList::CAP for a banner

constexpr uint8_t kMaxV = hal::midi::MidiState::kVoices;

// Sounding voices, lowest note first. Sorted because every ratio below is taken
// against the lowest note, and the voice table is in allocation order.
uint8_t sortedVoices(const hal::midi::Voice** out) {
  const hal::midi::MidiState& m = hal::midi::state();
  uint8_t n = 0;
  for (uint8_t i = 0; i < kMaxV; ++i)
    if (m.v[i].level) out[n++] = &m.v[i];
  for (uint8_t i = 1; i < n; ++i) {              // insertion sort; n <= 8
    const hal::midi::Voice* key = out[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && out[j]->note > key->note) { out[j + 1] = out[j]; --j; }
    out[j + 1] = key;
  }
  return n;
}

// The just ratio of an interval in semitones, as p/q. Octaves multiply the
// numerator, which is why a note two octaves up is 4/1 and not 1/1 again.
void ratioOf(int semitones, int& p, int& q) {
  const int oct = semitones / 12, semi = semitones % 12;
  p = kNum[semi] << oct;
  q = kDen[semi];
}

} // namespace

// The scope-music face.
//
// A scope in X-Y mode plots one channel against another, so an interval draws
// its own frequency ratio: the two lowest notes go on the two axes, and a fifth
// comes out as the three-by-two figure a fifth actually is. Summing both notes
// into both axes — which is the obvious thing to write — destroys exactly that,
// because then neither axis is any one note and the ratio never appears.
//
// Notes above the lowest two are added as quieter partials on alternating axes,
// at their own frequency in the same base. Their frequency is rounded to a whole
// number of cycles; where it does not land exactly, the figure precesses slowly
// instead of closing, which is what a scope does with a detuned interval and is
// worth having rather than hiding.
void midiscope(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;

  const hal::midi::Voice* v[kMaxV];
  const uint8_t n = sortedVoices(v);

  if (!n) {
    // Nothing playing. A dead black tube looks like a dead clock, so idle on a
    // slow breathing circle — it also proves the face is alive before the first
    // note arrives, which is what you want when debugging a MIDI cable.
    const int r = 300 + (int)((160 * vec::sinT(t * 2)) >> 16);
    d.circle(0, 0, r);
    return;
  }

  // Per-axis partial lists: frequency in cycles per figure, plus amplitude.
  struct Comp { int freq; int32_t amp; };
  Comp cx[kMaxV], cy[kMaxV];
  uint8_t nx = 0, ny = 0;
  int32_t sumx = 0, sumy = 0;

  int fx = 1, fy = 1;                      // the two axis frequencies
  if (n == 1) {
    // One note has no interval to draw, so the second axis is the same note in
    // quadrature — which is a circle, and is what a scope shows for a pure tone
    // fed to both channels through a 90-degree shift.
    fx = fy = 1;
  } else {
    int p, q; ratioOf(v[1]->note - v[0]->note, p, q);
    // A very wide interval folds down by octaves rather than being clamped:
    // halving p drops the upper note an octave, which keeps the interval's
    // character, where clamping would turn a three-octave tenth into a random
    // ratio that happens to fit.
    while (p > kMaxRate && p > q) p >>= 1;
    fx = q; fy = p;                         // lower note on X, upper on Y
  }
  cx[nx++] = Comp{fx, v[0]->level}; sumx += v[0]->level;
  cy[ny++] = Comp{fy, (n == 1 ? v[0]->level : v[1]->level)};
  sumy += cy[0].amp;

  // Everything above the second note decorates whichever axis is its turn.
  for (uint8_t i = 2; i < n; ++i) {
    int p, q; ratioOf(v[i]->note - v[0]->note, p, q);
    // Its frequency in the base where the fundamental is fx cycles.
    const int f = (fx * p + q / 2) / q;
    if (f < 1 || f > kMaxRate) continue;
    const int32_t a = v[i]->level / 2;     // decoration, not the subject
    if (i & 1) { cy[ny++] = Comp{f, a}; sumy += a; }
    else       { cx[nx++] = Comp{f, a}; sumx += a; }
  }
  if (!sumx || !sumy) return;

  // Segment count follows the fastest component: too few and a 7:5 figure comes
  // out as a polygon, too many and the beam spends the frame on one shape.
  int fastest = 1;
  for (uint8_t i = 0; i < nx; ++i) if (cx[i].freq > fastest) fastest = cx[i].freq;
  for (uint8_t i = 0; i < ny; ++i) if (cy[i].freq > fastest) fastest = cy[i].freq;
  int segs = 14 * fastest;
  if (segs < 48) segs = 48;
  if (segs > kMaxSegs) segs = kMaxSegs;

  // A figure with more cycles in it has a longer path, and beam-on time per
  // frame is the one budget that cannot be negotiated: a semitone cluster is a
  // 16:15 figure, and at full size it wants 37ms of a 16.7ms frame. So the
  // amplitude falls off past a few cycles — dense chords draw smaller. That is
  // a real property of the display rather than a fudge, and it also happens to
  // read well: the figure tightens as the harmony does.
  constexpr int kFreeRate = 6;      // up to this many cycles, full size
  const int amp = fastest > kFreeRate ? kAmp * kFreeRate / fastest : kAmp;

  // Normalising by the amplitude SUM rather than an observed peak means the
  // figure can never leave the tube whatever is held down: the worst case is
  // every partial peaking together, and that lands exactly on `amp`. Per axis,
  // so a channel carrying one note still fills the screen.
  const int32_t nrmx = (int32_t)amp * 65536 / sumx;
  const int32_t nrmy = (int32_t)amp * 65536 / sumy;
  // A slow drift on one axis only. With both channels locked a held chord is a
  // frozen figure; a few cents of drift is what makes it breathe, and it is also
  // what a real pair of oscillators would do.
  const int drift = (int)(t * 2);

  int px = 0, py = 0;
  for (int s = 0; s <= segs; ++s) {
    const int u = (s * vec::kSteps) / segs;
    int32_t x = 0, y = 0;
    for (uint8_t i = 0; i < nx; ++i)
      x += (((cx[i].amp * nrmx) >> 16) * vec::sinT(cx[i].freq * u)) >> 16;
    for (uint8_t i = 0; i < ny; ++i)
      y += (((cy[i].amp * nrmy) >> 16) * vec::cosT(cy[i].freq * u + drift)) >> 16;
    if (s) d.line(px, py, (int)x, (int)y);
    px = (int)x; py = (int)y;
  }
}

// The chord as a shape: the twelve pitch classes on a circle, sounding ones
// joined in a ring. Major and minor triads draw mirror-image triangles, a
// diminished seventh draws a square, and a walking bass line rotates the whole
// figure — the geometry of harmony, which is a real thing and not a metaphor.
void midichord(const ClockState&, DrawList& d) {
  static uint16_t t = 0;
  ++t;
  const hal::midi::MidiState& m = hal::midi::state();

  constexpr int R = 980;
  // Level per pitch class, so two octaves of the same note reinforce one point
  // rather than drawing twice on top of each other.
  uint16_t pc[12] = {0};
  for (uint8_t i = 0; i < hal::midi::MidiState::kVoices; ++i)
    if (m.v[i].level) {
      const uint8_t c = m.v[i].note % 12;
      if (m.v[i].level > pc[c]) pc[c] = m.v[i].level;
    }

  // The twelve marks, so the wheel is readable when nothing is playing.
  for (int i = 0; i < 12; ++i) {
    const int a = (i * vec::kSteps) / 12;
    const int r0 = R - (pc[i] ? 150 : 60);
    d.line((int)((r0 * vec::sinT(a)) >> 16), (int)((r0 * vec::cosT(a)) >> 16),
           (int)(( R * vec::sinT(a)) >> 16), (int)(( R * vec::cosT(a)) >> 16));
  }

  // Join the sounding classes in pitch order. Closing the ring is what turns
  // three points into a recognisable triangle rather than two line segments.
  int fx = 0, fy = 0, lx = 0, ly = 0, count = 0;
  for (int i = 0; i < 12; ++i) {
    if (!pc[i]) continue;
    const int a = (i * vec::kSteps) / 12;
    const int r = (R * 3) / 4 * pc[i] / 1024 + R / 5;    // louder sits further out
    const int x = (int)((r * vec::sinT(a)) >> 16);
    const int y = (int)((r * vec::cosT(a)) >> 16);
    if (count++) d.line(lx, ly, x, y);
    else         { fx = x; fy = y; }
    lx = x; ly = y;
  }
  if (count > 2) d.line(lx, ly, fx, fy);
  if (count == 1) d.circle(fx, fy, 70);                  // a single note is a dot

  d.circle(0, 0, 55);
}

}}  // namespace faces::impl
