// nowplaying.h — what the host says is currently playing.
//
// Its own module rather than a field of DeviceState, because a face renderer is
// handed only (ClockState, DrawList) — the same reason hal::midi owns its voice
// table. The host sends a track when it changes; the DEVICE advances the
// progress between those messages, so the ring sweeps smoothly without the link
// carrying an update every second.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace np {

struct Track {
  char     title[48]  = {0};
  char     artist[40] = {0};
  char     album[40]  = {0};
  uint16_t durationS  = 0;
  uint16_t progressS  = 0;   // as of stampMs
  uint32_t stampMs    = 0;
  bool     playing    = false;
  bool     valid      = false;
};

void set(const uint8_t* payload, uint8_t len);   // decode SetNowPlaying
const Track& track();

// Seconds elapsed now: the reported progress plus real time since it arrived,
// but only while playing, and never past the end of the song.
uint16_t elapsed();

}  // namespace np
