// micfx.h — the AtomS3U's microphone, reduced to numbers the tube can draw.
//
// The mic is an SPM1423 PDM part on GPIO38 (clock) / GPIO39 (data). It lives on
// the ESP32, and hard rule 5 keeps the beam on the Teensy — so all of the DSP
// happens here and only band energies and one triggered trace ever cross the
// link. The audio itself never leaves this chip and is never stored.
//
// Capture only runs while a visualiser face is actually showing. That is a
// privacy property worth stating plainly, and it is also a link one: sendFrame
// blocks up to 400ms waiting for ring space, so a stream left running would
// stall the loop that serves the web page.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace mic {

enum Want : uint8_t { None = 0, Bands = 1, Wave = 2 };

constexpr uint8_t kBands  = 16;
constexpr uint8_t kWaveN  = 64;

// Start or stop capture to match what is being drawn. Idempotent; call it every
// loop with whatever the showing face needs.
void want(Want w);

// Drain whatever the I2S DMA has ready and update the analysis. Returns without
// touching the hardware if capture is off, and never blocks: it reads only when
// a whole block is already available.
void poll();

// Latest analysis. Both return false until a block has been processed.
bool bands(uint8_t* out, uint8_t& n, uint8_t& level, uint8_t& peak);
bool wave(int8_t* out, uint8_t& n);

bool running();

// Diagnostics: the raw block statistics before any scaling, so a silent display
// can be told apart from a silent room. rms is in raw int16 units.
void stats(int32_t& dc, int32_t& rms, int32_t& pkpk, uint32_t& blocks);

// Which PDM phase the mic sits on. 0 left, 1 right.
void setSlot(int8_t m);
int8_t slot();

}  // namespace mic
