// seed.h — the fixtures a face needs when there is no clock, no host and no
// microphone attached.
//
// Shared by the harnesses rather than copied into each: thumbs.cpp and
// websim.cpp both have to present a face with something plausible to draw, and
// two copies of "what a gauge looks like" drift the moment one is edited.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Fill the host-fed stores (gauges, now playing, weather, ticker, world clock,
// radar) with fixed content, once, before any face renders.
void seedHostFaces();

// Feed the visualisers a synthetic spectrum and waveform for step k. Re-fed
// every frame on purpose: afx::live() times out after 2s of silence and the
// decay would otherwise flatten the bars to NO SIGNAL.
void feedAudio(int k);

namespace hal { namespace midi {
// A couple of sounding voices, so the MIDI faces draw the interval they exist
// to draw rather than an empty staff. A fifth: what midiscope shows as 3:2.
void seed();
}}
