// audio_usb.cpp — USB audio in, straight out to the X/Y DACs.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal/audio.h"
#include "hal/dac.h"
#include <Arduino.h>
#include <Audio.h>
#include <new>
#include "debug.h"

namespace hal { namespace audio {
namespace {

// Our own peak tap, in place of the Audio library's AudioAnalyzePeak.
//
// The library one hangs the chip within about two seconds when read from the
// loop — bisected to exactly that, with the same graph left unread running
// indefinitely. Its accessors mask interrupts and its read() does
// double-precision arithmetic, which on an MK66 is a soft-float call, and this
// runs at 1kHz. None of that is needed to answer "is anything playing":
// integers and one volatile are enough, and this way the whole path is ours.
class PeakTap : public AudioStream {
 public:
  PeakTap() : AudioStream(1, inputQueue_) {}

  void update(void) override {
    audio_block_t* b = receiveReadOnly();
    if (!b) return;
    int32_t hi = 0;
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
      // Via int32: negating INT16_MIN overflows a signed 16-bit type.
      int32_t v = b->data[i];
      if (v < 0) v = -v;
      if (v > hi) hi = v;
    }
    if (hi > (int32_t)peak_) peak_ = (uint16_t)hi;
    release(b);
  }

  // Read-and-clear. No interrupt masking: the worst a race can do is drop one
  // block's contribution from a silence detector, and a torn read of an aligned
  // 16-bit value is not a thing this core does.
  uint16_t take() { const uint16_t v = peak_; peak_ = 0; return v; }

 private:
  audio_block_t*    inputQueue_[1];
  volatile uint16_t peak_ = 0;
};

// Everything is placement-new'd on first use rather than declared as globals.
// AudioOutputAnalogStereo's *constructor* calls begin(), which seizes both DACs
// and spends ~257ms in a delay(1) DC ramp — so a plain global would take the
// display away from the render path at static-init time and stall boot for a
// quarter of a second. Neither is acceptable when the loop is the refresh.
alignas(AudioInputUSB)           uint8_t inMem[sizeof(AudioInputUSB)];
alignas(AudioOutputAnalogStereo) uint8_t outMem[sizeof(AudioOutputAnalogStereo)];
alignas(PeakTap)                 uint8_t pkMem[2][sizeof(PeakTap)];
alignas(AudioConnection)         uint8_t cnMem[4][sizeof(AudioConnection)];

AudioInputUSB*           usbIn = nullptr;
AudioOutputAnalogStereo* dacs  = nullptr;
PeakTap*                 peak[2] = { nullptr, nullptr };

bool     active_ = false;
bool     lit = false;
uint32_t lastSignalMs = 0;
uint32_t savedPdbSc = 0;
uint16_t lastLevel = 0;

// Silence has to persist a little before the beam is parked, or a rest in the
// music strobes the display. 400ms is long enough to ride out a rest and short
// enough that a stopped track does not sit burning the phosphor.
constexpr uint32_t kSilenceHoldMs = 400;
constexpr uint16_t kFloor = 130;      // ~-48dBFS of 32767; below this is silence


} // namespace

// Diagnostic: which pieces of the graph get built. Selected by the faceId byte
// of SetMode so all three can be tried from one flash — each link recovery after
// a Teensy reflash costs minutes, so guess-per-flash is not affordable.
//   0 = DAC output only (no USB source at all)
//   1 = USB -> DACs
//   2 = USB -> DACs + peak analyzers (the real graph)
//   3 = as 2, but service() never reads the analyzers
static uint8_t variant = 2;
void setVariant(uint8_t v) { variant = v; }

void start() {
  if (active_) return;
  hal::dac::blank(true);          // dark while the DACs change hands

  if (!dacs) {
    dbg::sayf("audio: build variant %u", variant);
    AudioMemory(40);
    if (variant >= 1) { dbg::say("audio: usbin"); usbIn = new (inMem) AudioInputUSB(); }
    dbg::say("audio: dacs");
    dacs = new (outMem) AudioOutputAnalogStereo();
    if (variant >= 1) {
      dbg::say("audio: conns");
      new (cnMem[0]) AudioConnection(*usbIn, 0, *dacs, 0);  // left  -> X
      new (cnMem[1]) AudioConnection(*usbIn, 1, *dacs, 1);  // right -> Y
    }
    if (variant >= 2) {
      dbg::say("audio: peaks");
      peak[0] = new (pkMem[0]) PeakTap();
      peak[1] = new (pkMem[1]) PeakTap();
      new (cnMem[2]) AudioConnection(*usbIn, 0, *peak[0], 0);
      new (cnMem[3]) AudioConnection(*usbIn, 1, *peak[1], 0);
    }
    dbg::say("audio: ref");

    // begin() leaves DACRFS clear, selecting the 1.2V reference. The render
    // path runs the DACs at 3.3V and every tuned coordinate in the firmware
    // assumes that full scale — without this the picture comes back at roughly
    // a third of its size. INTERNAL means 1.2V here, so anything else is 3.3V.
    dacs->analogReference(EXTERNAL);
  } else {
    // Restarting by calling begin() again would re-run the DC ramp and
    // re-allocate the DMA channel. Putting the PDB back is smaller and has no
    // side effects — it is the only thing stop() turned off.
    PDB0_SC = savedPdbSc | PDB_SC_LDOK;
    PDB0_SC = savedPdbSc | PDB_SC_SWTRIG;
  }

  dbg::say("audio: running");
  active_ = true;
  lit = false;                    // service() lights it when audio arrives
  lastSignalMs = 0;
}

void stop() {
  if (!active_) return;
  // The PDB is what clocks the DMA into the DACs; with it stopped the audio
  // path cannot touch them, and direct writes work again.
  savedPdbSc = PDB0_SC;
  PDB0_SC = 0;
  active_ = false;
  lit = false;
  hal::dac::blank(true);
  hal::dac::init();               // 3.3V reference, blanked, back to direct writes
}

bool active() { return active_; }
uint16_t level() { return lastLevel; }
bool beamLit() { return lit; }

void service() {
  if (!active_) return;

  // read() consumes the accumulated peak, so both have to be drained every
  // pass or the second channel reports a stale maximum.
  if (!peak[0] || !peak[1]) return;      // diagnostic variants without analyzers
  if (variant == 3) return;              // diagnostic: taps present but untouched
  uint16_t level = peak[0]->take();
  const uint16_t r = peak[1]->take();
  if (r > level) level = r;
  if (level > kFloor) lastSignalMs = millis();
  lastLevel = level;   // diagnostic only

  // Sample zero is mid-scale on the DAC, so silence is not a dark screen — it
  // is a stationary lit dot in the centre of the tube, which is precisely how
  // phosphor gets burned. Blank until there is something being drawn.
  const bool live = lastSignalMs && (millis() - lastSignalMs) < kSilenceHoldMs;
  if (live != lit) {
    lit = live;
    hal::dac::blank(!live);
  }
}

}}  // namespace hal::audio
