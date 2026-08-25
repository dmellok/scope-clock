// audiofx.cpp — decode SetAudio / SetWave and smooth them for the tube.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "audiofx.h"
#include <Arduino.h>

namespace afx {
namespace {

State cur;
uint32_t lastTickMs = 0;
uint32_t lastHistMs = 0;

// Attack is instant and release is slow. A meter that eased into a transient
// would miss every one of them — the whole point is the moment the sound
// arrives — while an instant fall makes the display flicker at the beat rate.
inline uint8_t fall(uint8_t v, uint16_t by) {
  return (uint16_t)v > by ? (uint8_t)(v - by) : 0;
}

} // namespace

void setBands(const uint8_t* p, uint8_t len) {
  if (len < 3) return;
  cur.level     = p[0];
  if (p[1] > cur.peakLevel) cur.peakLevel = p[1];
  const uint8_t n = p[2] < kBands ? p[2] : kBands;
  if (3 + n > len) return;                    // truncated; leave the last set
  for (uint8_t i = 0; i < n; ++i) {
    const uint8_t v = p[3 + i];
    if (v > cur.band[i]) cur.band[i] = v;     // attack: straight to it
    if (v > cur.peak[i]) cur.peak[i] = v;
  }
  cur.lastMsgMs = millis();
  cur.valid = true;

  // History on its own clock, so the waterfall's depth is a duration.
  if (millis() - lastHistMs >= kHistMs) {
    lastHistMs = millis();
    cur.histHead = (uint8_t)((cur.histHead + 1) % kHist);
    for (uint8_t i = 0; i < kBands; ++i) cur.hist[cur.histHead][i] = cur.band[i];
  }
}

void setWave(const uint8_t* p, uint8_t len) {
  if (len < 1) return;
  const uint8_t n = p[0] < kWaveMax ? p[0] : kWaveMax;
  if (1 + n > len) return;
  for (uint8_t i = 0; i < n; ++i) cur.wave[i] = (int8_t)p[1 + i];
  cur.waveN = n;
  cur.lastMsgMs = millis();
  cur.valid = true;
}

void tick() {
  const uint32_t now = millis();
  uint32_t dt = now - lastTickMs;
  lastTickMs = now;
  if (dt > 200) dt = 200;                     // after a pause, do not slam to 0

  // Per second: bands fall the full range in ~0.7s, peaks in ~3s.
  const uint16_t bandDrop = (uint16_t)((dt * 360) / 1000);
  const uint16_t peakDrop = (uint16_t)((dt * 85) / 1000);
  for (uint8_t i = 0; i < kBands; ++i) {
    cur.band[i] = fall(cur.band[i], bandDrop);
    cur.peak[i] = fall(cur.peak[i], peakDrop);
    if (cur.peak[i] < cur.band[i]) cur.peak[i] = cur.band[i];
  }
  cur.level     = fall(cur.level, bandDrop);
  cur.peakLevel = fall(cur.peakLevel, peakDrop);
  if (cur.peakLevel < cur.level) cur.peakLevel = cur.level;
}

const State& get() { return cur; }

// Two seconds: long enough to ride out the gap between messages and a quiet
// passage, short enough that a face says so when the bridge stops sending.
bool live() { return cur.valid && (millis() - cur.lastMsgMs) < 2000; }

}  // namespace afx
