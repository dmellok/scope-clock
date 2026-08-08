// notify.cpp — the notification overlay: a title and a body, placed, temporary.
//
// Kept out of main.cpp so the layout can be rendered and measured in the host
// sim before it ever reaches the tube, which is how every other bit of geometry
// in this firmware has been checked.
//
// Two things constrain it. The overlay is added AFTER txt::centerLines, because
// any text item carrying a real position opts the whole list out of centring —
// running it earlier would break the digital face. And the field is +/-1250,
// with the numeral ring of the analog faces reaching about 1100, so a strip has
// roughly 150 units of clear air at each edge and a centred card has to earn its
// place by being readable over whatever it covers.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "notify.h"
#include "state.h"
#include "drawlist.h"
#include "text.h"
#include <Arduino.h>
#include <stdio.h>

namespace {

constexpr int kTitleScale = 9;
constexpr int kBodyScale  = 7;
constexpr int kGap        = 40;    // between title and body
constexpr int kEdge       = 1215;  // outer edge of a strip's ink
constexpr int kPad        = 90;    // card padding around the text block

constexpr int kLineMax = 2350;   // widest a strip may get before it clips
constexpr int kMinScale = 5;     // floor for the auto-fit; below this it is a smudge

// The frame is what makes a centred notice readable over a busy face: without
// it the text just tangles with whatever is already being drawn.
void frame(DrawList& d, int x0, int y0, int x1, int y1) {
  d.line(x0, y0, x1, y0);
  d.line(x1, y0, x1, y1);
  d.line(x1, y1, x0, y1);
  d.line(x0, y1, x0, y0);
}

// One centred line hard against the top or bottom of the field, shrunk to fit.
//
// Below scale 40 the font's kern is a flat multiple of the scale, so ink width
// is exactly linear in it and the scale that just fits is a division rather
// than a search. Shrinking beats truncating: the whole point of a notification
// is the words in it.
int fitScale(const char* text) {
  const int unit = txt::inkWidth(1, text);
  if (unit <= 0) return kBodyScale;
  int s = kLineMax / unit;
  if (s > kBodyScale) s = kBodyScale;
  if (s < kMinScale)  s = kMinScale;   // any smaller is unreadable across a room
  return s;
}

void drawLine(DrawList& d, const char* text, uint8_t place) {
  if (!text[0]) return;
  const int s = fitScale(text);
  const int w = txt::inkWidth(s, text);
  const int y = place == 1 ? kEdge - txt::height(s) : -kEdge;
  d.text(-w / 2, y, s, text);
}

} // namespace

void overlayNotify(DeviceState& dev, DrawList& list) {
  if (!dev.noteActive) return;
  // Wrap-safe comparison; the expiry is the device's own.
  if ((int32_t)(millis() - dev.noteUntilMs) >= 0) { dev.noteActive = false; return; }

  const bool hasTitle = dev.noteTitle[0] != '\0';
  const bool hasBody  = dev.noteBody[0]  != '\0';
  if (!hasTitle && !hasBody) return;

  // A strip is ONE line, whatever it was given.
  //
  // The clear air between the numeral ring (ink to about 1100) and the edge of
  // the field is roughly 150 units, and a single line at kBodyScale is 140. Two
  // lines need 370 and simply cannot fit: stacked, the title lands squarely on
  // the numerals. So a strip joins title and body into one line, which is what
  // a ticker does anyway, and only the centred card gets the two-line treatment
  // it has the room for.
  static char joined[sizeof(dev.noteTitle) + sizeof(dev.noteBody) + 4];
  if (dev.notePlace != 2 && hasTitle) {
    if (hasBody) snprintf(joined, sizeof joined, "%s - %s", dev.noteTitle, dev.noteBody);
    else         snprintf(joined, sizeof joined, "%s", dev.noteTitle);
    // No need to sacrifice the title for width — drawLine shrinks to fit, and
    // only gives up at kMinScale, which a joined pair reaches only if it is
    // longer than both buffers allow.
    return drawLine(list, joined, dev.notePlace);
  }
  if (dev.notePlace != 2) return drawLine(list, dev.noteBody, dev.notePlace);

  const int tw = hasTitle ? txt::inkWidth(kTitleScale, dev.noteTitle) : 0;
  const int bw = hasBody  ? txt::inkWidth(kBodyScale,  dev.noteBody)  : 0;
  const int th = hasTitle ? txt::height(kTitleScale) : 0;
  const int bh = hasBody  ? txt::height(kBodyScale)  : 0;
  const int gap = (hasTitle && hasBody) ? kGap : 0;
  const int blockH = th + gap + bh;

  // Height of whichever line comes first — the title if there is one, otherwise
  // the body is the only line and carries the whole block.
  const int firstH = hasTitle ? th : bh;

  // Only the centred card reaches here; the strips returned above.
  const int topBase = blockH / 2 - firstH;
  // Baseline of the bottom line, which is the body only when there is a title
  // above it. Computing it rather than assuming two lines is what keeps the
  // card's frame correct for a title-less notice.
  const int lastBase = (hasTitle && hasBody) ? topBase - gap - bh : topBase;

  const int cw = (tw > bw ? tw : bw) / 2 + kPad;
  frame(list, -cw, lastBase - kPad / 2, cw, topBase + firstH + kPad / 2);

  if (hasTitle) list.text(-tw / 2, topBase, kTitleScale, dev.noteTitle);
  if (hasBody)  list.text(-bw / 2, lastBase, kBodyScale, dev.noteBody);
}
