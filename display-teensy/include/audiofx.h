// audiofx.h — what the bridge's microphone heard, and what the visualisers
// draw from.
//
// The mic is on the ESP32 and the beam is on the Teensy, so hard rule 5 decides
// the shape of this: the DSP happens over there and only derived numbers cross
// the link. Nothing here has ever seen audio.
//
// The device does the SMOOTHING, not the bridge. Messages arrive at ~30Hz and
// the tube refreshes at 60, so a face reading the raw values would visibly
// step; decaying between messages is what turns a stream of samples into
// motion. It is the same bargain nowplaying makes with its progress ring, and
// it means a dropped frame costs a little decay rather than a stutter.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace afx {

constexpr uint8_t kBands   = 16;
constexpr uint8_t kWaveMax = 96;
// Rows of spectrum history for the waterfall. Pushed on a timer rather than per
// message, so the depth is a duration (about 2.4s) and not a message rate.
constexpr uint8_t kHist    = 24;
constexpr uint16_t kHistMs = 100;

struct State {
  uint8_t  band[kBands] = {0};    // live, decayed
  uint8_t  peak[kBands] = {0};    // peak-hold, falls slower
  uint8_t  level = 0, peakLevel = 0;
  int8_t   wave[kWaveMax] = {0};
  uint8_t  waveN = 0;
  uint8_t  hist[kHist][kBands] = {{0}};
  uint8_t  histHead = 0;          // newest row
  uint32_t lastMsgMs = 0;
  bool     valid = false;
};

void setBands(const uint8_t* payload, uint8_t len);   // SetAudio
void setWave(const uint8_t* payload, uint8_t len);    // SetWave

// Advance the decay. Time-based rather than per-frame so the fall looks the
// same whatever the frame rate is doing.
void tick();

const State& get();

// True when something has been heard recently. A visualiser with nothing to
// show should say so rather than sit at zero, which is indistinguishable from
// a broken face.
bool live();

}  // namespace afx
