// micfx.cpp — PDM capture, an FFT, and a zero-crossing trigger.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "micfx.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <math.h>

namespace mic {
namespace {

// SWAPPED relative to M5Stack's own AtomS3U doc page, which lists MIC_CLK as 38
// and MIC_DATA as 39. M5Unified — the library that demonstrably works on this
// board — has it the other way round:
//     case board_t::board_M5AtomS3U:
//       mic_cfg.pin_data_in = GPIO_NUM_38;
//       mic_cfg.pin_ws      = GPIO_NUM_39;
// Believing the doc page cost an evening: driving the clock out of the DATA pin
// means the microphone is never clocked, and sampling the CLOCK pin as data
// reads an unconnected input — which is exactly what a DC of -30935 with zero
// peak-to-peak, identical across both PDM phases and both sample rates, is.
constexpr int kClkPin = 39;      // WS / clock to the SPM1423
constexpr int kDatPin = 38;      // PDM data back from it
// 32kHz, not 16. The PDM clock is rate x 64, so 16kHz puts it at 1.024MHz —
// right on the SPM1423's stated 1.0MHz MINIMUM, and below its minimum a PDM mic
// does not run at all. That is exactly what a stuck DC of -30935 with zero
// peak-to-peak looks like: I2S clocking happily, microphone asleep. 32kHz gives
// 2.048MHz, comfortably inside the 1.0-3.25MHz the part specifies.
constexpr uint32_t kRate = 32000;
constexpr int kN = 256;          // FFT size; 16ms of audio at 16kHz

// The Mic_Class alone, never M5.begin(): that would also bring up power, IMU
// and — the dangerous one — Serial, which on this board is the CDC link to the
// Teensy. Only the pins and the rate are overridden; magnification (16) and
// input_channel are M5Unified's own values for this board and are the whole
// reason to be using it.
m5::Mic_Class m5mic;
bool  on = false;
bool  queued = false;
Want  curWant = None;
int8_t slotMask = 1;      // 0 = left, 1 = right; see want()

int16_t raw[kN];
float   re[kN], im[kN];
uint8_t bandOut[kBands];
uint8_t levelOut = 0, peakOut = 0;
int8_t  waveOut[kWaveN];
uint8_t waveLen = 0;
bool    haveBands = false, haveWave = false;
int32_t dDc=0, dRms=0, dPk=0; uint32_t dBlocks=0;

// Radix-2 in place. 256 points at 60 blocks a second is nothing on an S3, and
// it saves pulling in a DSP library for one function.
void fft() {
  for (int i = 1, j = 0; i < kN; ++i) {
    int bit = kN >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t;
                 t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (int len = 2; len <= kN; len <<= 1) {
    const float ang = -2.0f * (float)M_PI / (float)len;
    const float wr = cosf(ang), wi = sinf(ang);
    for (int i = 0; i < kN; i += len) {
      float cr = 1.0f, ci = 0.0f;
      for (int k = 0; k < len/2; ++k) {
        const int a = i + k, b = a + len/2;
        const float xr = re[b]*cr - im[b]*ci;
        const float xi = re[b]*ci + im[b]*cr;
        re[b] = re[a] - xr; im[b] = im[a] - xi;
        re[a] += xr;        im[a] += xi;
        const float nr = cr*wr - ci*wi;
        ci = cr*wi + ci*wr; cr = nr;
      }
    }
  }
}

// Log-spaced edges over the mic's useful range. Linear bins would put twelve of
// the sixteen bands above 4kHz, where a room has almost nothing, and leave the
// bass — the part you can see moving — squeezed into one.
// Bin width is rate/kN = 125Hz, so these span roughly 125Hz to 7kHz — the part
// of the range a room actually occupies and the mic actually hears.
const uint8_t kEdge[kBands + 1] = {
  1, 2, 3, 4, 5, 6, 7, 9, 11, 14, 18, 23, 29, 36, 44, 54, 66
};

void analyse() {
  // DC removal first: a PDM mic sits on a large offset and the whole spectrum
  // would otherwise be dominated by bin 0 leaking into its neighbours.
  int32_t sum = 0;
  for (int i = 0; i < kN; ++i) sum += raw[i];
  const int32_t dc = sum / kN;

  int64_t sq = 0;
  for (int i = 0; i < kN; ++i) {
    const float v = (float)(raw[i] - dc);
    sq += (int64_t)((raw[i]-dc)) * (raw[i]-dc);
    // Hann, so a tone between bins does not smear across half the spectrum.
    const float w = 0.5f - 0.5f * cosf(2.0f*(float)M_PI*i/(kN-1));
    re[i] = v * w; im[i] = 0.0f;
  }

  // Level as RMS on a log scale: linear loudness spends its whole range in the
  // top of a shout and shows nothing for a room.
  const float rms = sqrtf((float)sq / kN);
  dDc = dc; dRms = (int32_t)rms;
  float db = 20.0f * log10f(rms + 1.0f);       // ~0..90 for int16
  db = (db - 24.0f) * (255.0f / 46.0f);        // floor out the noise, fit 0..255
  int lv = (int)db; lv = lv < 0 ? 0 : (lv > 255 ? 255 : lv);
  levelOut = (uint8_t)lv;
  if (levelOut > peakOut) peakOut = levelOut;

  fft();
  for (int b = 0; b < kBands; ++b) {
    float acc = 0.0f;
    for (int k = kEdge[b]; k < kEdge[b+1]; ++k)
      acc += sqrtf(re[k]*re[k] + im[k]*im[k]);
    const int n = kEdge[b+1] - kEdge[b];
    float mag = acc / (n ? n : 1);
    float d = 20.0f * log10f(mag + 1.0f);
    d = (d - 28.0f) * (255.0f / 44.0f);
    int v = (int)d; v = v < 0 ? 0 : (v > 255 ? 255 : v);
    bandOut[b] = (uint8_t)v;
  }
  haveBands = true;
}

// TRIGGERED, and that is the whole difference between a trace and a mess: start
// at a rising zero crossing so the same part of the waveform lands in the same
// place every time. Without it a 4ms window refreshed 20 times a second skates
// sideways at whatever the beat frequency happens to be.
void triggerWave() {
  int32_t sum = 0;
  for (int i = 0; i < kN; ++i) sum += raw[i];
  const int32_t dc = sum / kN;

  int start = 0;
  const int room = kN - kWaveN;
  for (int i = 1; i < room; ++i) {
    if (raw[i-1] - dc <= 0 && raw[i] - dc > 0) { start = i; break; }
  }

  // Scaled so an ordinary room fills a useful part of the field. Clipped rather
  // than auto-ranged: a gain that chases the signal makes silence look loud.
  for (int i = 0; i < kWaveN; ++i) {
    int v = (raw[start + i] - dc) / 96;
    v = v < -127 ? -127 : (v > 127 ? 127 : v);
    waveOut[i] = (int8_t)v;
  }
  waveLen = kWaveN;
  haveWave = true;
}

} // namespace

void want(Want w) {
  curWant = w;
  if (w != None && !on) {
    auto c = m5mic.config();
    c.pin_data_in = kDatPin;
    c.pin_ws      = kClkPin;
    c.sample_rate = (int)kRate;
    c.magnification = 16;
    m5mic.config(c);
    on = m5mic.begin();
    queued = false;
    peakOut = 0;
  } else if (w == None && on) {
    m5mic.end();
    on = false; queued = false;
    haveBands = haveWave = false;
  }
}

#if 0
    // A PDM mic drives one clock phase only — the SPM1423's SELECT pin decides
    // which — so the mono slot has to be the one it is actually on. Left (the
    // default) came back as a stuck constant: blocks arriving, DC pinned at
    // -30935, peak-to-peak zero, which is an unconnected slot rather than a
    // quiet room. Selectable so the other phase can be tried without a reflash.
#endif

void stats(int32_t& dc, int32_t& rms, int32_t& pkpk, uint32_t& blocks) {
  dc = dDc; rms = dRms; pkpk = dPk; blocks = dBlocks;
}

void poll() {
  if (!on) return;
  // record() is asynchronous: it hands the buffer to a background task and
  // returns. So queue one, and only look at it once isRecording() has dropped —
  // which also keeps this out of the loop that serves the web page.
  // Rate-gated as well as queue-gated. isRecording() alone let this run at 283
  // blocks a second against the 62.5 that 16kHz/256 can physically produce —
  // i.e. re-reading a buffer that had never been refilled, which is exactly
  // what a constant with zero peak-to-peak looks like. A block cannot be ready
  // sooner than it takes to record one.
  constexpr uint32_t kBlockMs = (kN * 1000u) / kRate;
  static uint32_t lastBlock = 0;
  if (!queued) { queued = m5mic.record(raw, kN, kRate); lastBlock = millis(); return; }
  if (m5mic.isRecording()) return;
  if (millis() - lastBlock < kBlockMs) return;
  lastBlock = millis();
  queued = false;
  ++dBlocks;
  // Peak-to-peak on the raw block, computed whichever path runs: it is the one
  // number that separates "the room is quiet" from "the microphone is dead".
  int16_t lo = 32767, hi = -32768;
  for (int i = 0; i < kN; ++i) { if (raw[i] < lo) lo = raw[i]; if (raw[i] > hi) hi = raw[i]; }
  dPk = (int32_t)hi - lo;
  if (curWant == Wave) triggerWave();
  else                 analyse();
  queued = m5mic.record(raw, kN, kRate);   // straight back in the queue
}

bool bands(uint8_t* out, uint8_t& n, uint8_t& level, uint8_t& peak) {
  if (!haveBands) return false;
  for (uint8_t i = 0; i < kBands; ++i) out[i] = bandOut[i];
  n = kBands; level = levelOut; peak = peakOut;
  peakOut = levelOut;                 // report the peak since the last send
  return true;
}

bool wave(int8_t* out, uint8_t& n) {
  if (!haveWave) return false;
  for (uint8_t i = 0; i < waveLen; ++i) out[i] = waveOut[i];
  n = waveLen;
  return true;
}

bool running() { return on; }

// Restart on the other PDM phase. Cheap to try and the only way to tell a wrong
// slot from a dead microphone without a scope on the pins.
void setSlot(int8_t m) {
  // Kept as the diagnostic hook, but the channel is M5Unified's business now.
  slotMask = m;
}
int8_t slot() { return slotMask; }

}  // namespace mic
