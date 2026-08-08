// hal/midi.h — USB-MIDI in on the micro-USB device jack, for scope music.
//
// The jack was already reserved for oscilloscope-music duty and the USB type is
// already USB_MIDI_AUDIO_SERIAL, so the MK66's device controller enumerates as a
// MIDI port with no descriptor change: plug a DAW or a `.mid` player into the
// front of the clock and it appears as "Teensy MIDI".
//
// This is the second USB controller. The bridge lives on the *host* controller
// at the back, so nothing here goes near the link — and nothing here touches the
// radio, which stays on the ESP32 (hard rule 5).
//
// Like everything else called from the loop, poll() is non-blocking and, more
// importantly, BOUNDED: a stuck sustain pedal under a dense sequence can queue
// MIDI faster than 60Hz drains it, and an unbounded drain would eat the refresh.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace hal { namespace midi {

// One sounding note. Released voices linger with a decaying level so a figure
// rings out instead of vanishing between chords — on a phosphor tube an abrupt
// change reads as a glitch, and the decay is also what makes legato look legato.
struct Voice {
  uint8_t  note = 0;
  uint8_t  vel = 0;
  uint16_t level = 0;      // 0..1024, the envelope
  bool     held = false;   // key still down (or sustained)
};

struct MidiState {
  static constexpr uint8_t kVoices = 8;
  Voice    v[kVoices];
  uint8_t  sounding = 0;       // voices with level > 0
  uint8_t  lowest = 255;       // MIDI note number of the lowest held voice
  bool     sustain = false;
  uint32_t lastEventMs = 0;    // for "has anything been played lately"
};

void init();
void poll();                       // drain the queue, advance envelopes
const MidiState& state();          // read-only; the faces render from this

}}  // namespace hal::midi
