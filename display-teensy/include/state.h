// state.h — owned state structs. Replaces the ~100 loose globals of the original.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include "drawlist.h"

// The RTC holds LOCAL time (the host applies timezone + DST). The device never
// reasons about zones.
struct ClockState {
  int16_t year = 26, month = 1, day = 1, wday = 0;
  int16_t hour = 0, minute = 0, second = 0;
  bool    rtcPresent = false;
  bool    hr12 = true;
  // When the host last disciplined us. Reported in Status so the host can tell
  // "RTC present" from "RTC actually being kept honest".
  uint32_t setAtMs  = 0;
  bool     everSet  = false;
};

// Audio is the odd one out: the draw list is not in its path at all, the DACs
// belong to the audio DMA, and the beam is lit continuously rather than per
// stroke. See hal/audio.h.
enum class Mode : uint8_t { Face, Pushed, Audio };

struct DeviceState {
  Mode      mode = Mode::Face;
  uint8_t   faceId = 0;
  uint8_t   brightness = 255;
  uint16_t  hz = 60;            // 50 or 60
  uint32_t  frameUs = 0;        // last render time; the budget is 1e6/hz, and
                                // exceeding it means the refresh is free-running
                                // slower than hz. Reported via Msg::Status.
  bool      hostPresent = false;

  // Per-face render scale, percent of nominal. Faces are authored against one
  // size but a tube's usable area is its own; this is the adjustment, and it is
  // per face because a dense face and a sparse one want different answers.
  // The host owns the persistence and re-sends the table on Hello.
  static constexpr uint8_t kMaxFaces = 32;
  // 70% of the render's nominal size. The render scales device units by 3/2 to
  // reach the tube's 1800-count rim, which puts a face's own 1200-unit edge
  // exactly on the glass — right for a full-bleed animation, too tight for a
  // dial whose numerals then touch the rim. 70% insets it to a clock face.
  static constexpr uint8_t kDefaultScale = 70;
  uint8_t   faceScale[kMaxFaces];
  bool      scaleMode = false;      // knob is adjusting scale, not choosing faces
  uint32_t  scaleModeUntilMs = 0;

  DeviceState() { for (uint8_t i = 0; i < kMaxFaces; ++i) faceScale[i] = kDefaultScale; }

  DrawList  pushed;             // host-authored list when mode == Pushed
  // Backing store for pushed text. Item holds a const char*, and the link's
  // receive buffer is overwritten by the next frame, so the strings have to be
  // copied somewhere with the same lifetime as the list itself. A payload
  // cannot exceed MAX_PAYLOAD, so its strings cannot either.
  static constexpr uint16_t kArenaSize = 240;
  char      arena[kArenaSize] = {0};
  // Notification overlay: title + body, placed, auto-expiring locally so a host
  // stall can't strand one on screen. A plain Banner is just this with no title
  // and the bottom placement, so there is one overlay renderer, not two.
  bool      noteActive = false;
  uint32_t  noteUntilMs = 0;
  uint8_t   notePlace = 0;          // 0 bottom strip, 1 top strip, 2 centred card
  bool      noteSolo = false;       // blank the face behind it
  char      noteTitle[32] = {0};
  char      noteBody[64] = {0};
};
