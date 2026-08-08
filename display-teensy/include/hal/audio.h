// hal/audio.h — oscilloscope music: stereo audio drives the beam directly.
//
// This is the one mode where the draw list is not in the path at all. The left
// channel is X and the right is Y, DMA-fed to both DACs at 44.1kHz from the USB
// audio device on the front jack — which the clock has been enumerating as all
// along, since the USB type is USB_MIDI_AUDIO_SERIAL.
//
// Consequences worth knowing before touching this:
//
//   * Brightness is not `dev.brightness` here and `vec::tuneDwell` does nothing.
//     The beam lingers where the waveform moves slowly, so the trace brightens
//     on slow curves by itself. That is most of why scope music looks the way
//     it does, and it is not a bug to correct.
//   * Digital silence is sample zero, which parks a lit beam dead centre — the
//     classic way to burn a hole in a phosphor. service() watches the level and
//     blanks when nothing is playing. Call it every loop while active.
//   * 44.1kHz is fixed by the Teensy core: usb_desc.c declares tSamFreq 44100
//     with bSamFreqType 1 (exactly one rate) and a 180-byte isochronous
//     endpoint. 192kHz needs 768 bytes/frame and is an SD-card job, not a USB
//     one.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace hal { namespace audio {

// Hand the DACs to the audio DMA. The first call costs ~257ms inside the Audio
// library's DC ramp, so it is deliberately not on the boot path.
void start();
void setVariant(uint8_t v);   // diagnostic, see audio_usb.cpp

// Hand them back to the render path, blanked and reconfigured.
void stop();

bool active();
uint16_t level();    // last measured peak, 0..32767 (diagnostic)
bool beamLit();      // is the beam currently unblanked

// Level watch + beam blanking. Cheap; call it every loop while active.
void service();

}}  // namespace hal::audio
