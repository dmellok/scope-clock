// midi_usbdev.cpp — USB-MIDI in, on the micro-USB device jack.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal/midi.h"
#include <Arduino.h>

namespace hal { namespace midi {
namespace {

MidiState st;

// Envelope, in level units per millisecond. Held notes rise fast enough to look
// instant; released ones fall over about half a second, which is roughly how
// long the eye keeps seeing a figure that has stopped being redrawn anyway.
constexpr int kAttackPerMs  = 12;
constexpr int kReleasePerMs = 2;
constexpr uint16_t kLevelMax = 1024;

// A dense sequence with the sustain pedal down can queue messages faster than a
// 60Hz loop drains them. Bounded drain: the backlog shrinks over a few frames
// instead of one frame growing without limit. Nothing in the loop may block,
// and "drain until empty" is a loop whose length the host controls.
constexpr int kMaxPerPoll = 24;

Voice* find(uint8_t note) {
  for (uint8_t i = 0; i < MidiState::kVoices; ++i)
    if (st.v[i].level && st.v[i].note == note) return &st.v[i];
  return nullptr;
}

// Prefer a slot that is silent, then the quietest released one, and only then
// steal a held voice — stealing something the player is still holding is the
// most audible-looking mistake, so it is the last resort.
Voice* claim() {
  Voice* best = nullptr;
  for (uint8_t i = 0; i < MidiState::kVoices; ++i) {
    Voice& c = st.v[i];
    if (!c.level) return &c;
    if (c.held) continue;
    if (!best || c.level < best->level) best = &c;
  }
  if (best) return best;
  for (uint8_t i = 0; i < MidiState::kVoices; ++i)
    if (!best || st.v[i].level < best->level) best = &st.v[i];
  return best;
}

void noteOn(uint8_t note, uint8_t vel) {
  // Note-on at velocity 0 is note-off; most sequencers send it that way.
  if (vel == 0) {
    if (Voice* v = find(note)) v->held = false;
    return;
  }
  Voice* v = find(note);
  if (!v) v = claim();
  v->note = note;
  v->vel = vel;
  v->held = true;
  if (!v->level) v->level = 1;         // start the attack, do not jump to full
  st.lastEventMs = millis();
}

void noteOff(uint8_t note) {
  if (Voice* v = find(note)) {
    // Under the pedal the key coming up does not release the note.
    if (!st.sustain) v->held = false;
    else             v->vel |= 0x80;   // flagged: release when the pedal lifts
  }
  st.lastEventMs = millis();
}

void allOff() {
  for (uint8_t i = 0; i < MidiState::kVoices; ++i) {
    st.v[i].held = false;
    st.v[i].vel &= 0x7F;
  }
  st.sustain = false;
}

void controlChange(uint8_t cc, uint8_t val) {
  switch (cc) {
    case 64: {                         // sustain pedal
      const bool down = val >= 64;
      if (st.sustain && !down)         // pedal up: release everything waiting
        for (uint8_t i = 0; i < MidiState::kVoices; ++i)
          if (st.v[i].vel & 0x80) { st.v[i].vel &= 0x7F; st.v[i].held = false; }
      st.sustain = down;
      break;
    }
    // 120 All Sound Off, 123 All Notes Off. Every sequencer and DAW sends one of
    // these when you press stop, and honouring them is what stops a figure from
    // freezing on notes nobody is playing — which looks exactly like a crash.
    case 120:
    case 123:
      allOff();
      break;
    default: break;
  }
}

} // namespace

void init() {
  // Deliberately nothing. usbMIDI needs no begin() — the USB device stack is
  // already running from the descriptor set, and this is the module where
  // somebody would be tempted to add a blocking wait for a host that may never
  // arrive. (See link_usbhost.cpp for what that costs.)
}

void poll() {
  for (int n = 0; n < kMaxPerPoll && usbMIDI.read(); ++n) {
    switch (usbMIDI.getType()) {
      case usbMIDI.NoteOn:        noteOn(usbMIDI.getData1(), usbMIDI.getData2()); break;
      case usbMIDI.NoteOff:       noteOff(usbMIDI.getData1()); break;
      case usbMIDI.ControlChange: controlChange(usbMIDI.getData1(), usbMIDI.getData2()); break;
      // A sequencer that stops mid-chord leaves notes hanging; the figure would
      // freeze with them and look like the clock had crashed.
      case usbMIDI.SystemReset:   allOff(); break;
      default: break;
    }
  }

  // Advance envelopes on real elapsed time, not on frames: the refresh rate is
  // 50 or 60Hz depending on the mains the clock was built for, and a decay tied
  // to frames would be a fifth faster on one of them.
  static uint32_t last = 0;
  const uint32_t now = millis();
  uint32_t dt = now - last;
  last = now;
  if (dt > 100) dt = 100;              // after a stall, do not jump the envelope

  uint8_t sounding = 0, lowest = 255;
  for (uint8_t i = 0; i < MidiState::kVoices; ++i) {
    Voice& v = st.v[i];
    if (v.held) {
      const uint32_t target = (uint32_t)(v.vel & 0x7F) * kLevelMax / 127;
      const uint32_t step = dt * kAttackPerMs;
      v.level = (uint16_t)(v.level + step < target ? v.level + step : target);
    } else if (v.level) {
      const uint32_t step = dt * kReleasePerMs;
      v.level = (uint16_t)(v.level > step ? v.level - step : 0);
    }
    if (v.level) {
      ++sounding;
      if (v.note < lowest) lowest = v.note;
    }
  }
  st.sounding = sounding;
  st.lowest = lowest;
}

const MidiState& state() { return st; }

}}  // namespace hal::midi
