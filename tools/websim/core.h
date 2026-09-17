// core.h — frame production, independent of how the frames get to a browser.
//
// The native build (websim.cpp) serves these bytes over a socket; the
// WebAssembly build (wasm.cpp) hands the same bytes to JavaScript in the same
// tab. Everything about the rendering lives here so the two transports cannot
// drift apart, and so a third one would be a day's work rather than a fork.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <string>

namespace core {

// Tables, fixtures, and the beam parked. Call once.
void init();

int faceCount();

// The registry as the picker wants it: [{"n":name,"f":family},...]
const std::string& facesJson();

// One frame, as the beam drew it. Returns a reference to a buffer reused every
// call — the caller copies or sends it before asking for the next.
//
//   u16 strokes, u32 dots, u32 moves, i32 maxR, u16 face, u16 pad
//   per stroke: u16 points, then points x (i16 x, i16 y), DAC counts about 0
//
// Full resolution, every dot: the thumbnail generator simplifies its polylines
// to fit an ESP32's flash, but here the dot spacing IS the interesting part,
// because it is what brightness is made of.
const std::string& frame(int faceId, int scalePct, uint32_t stepMs);

}  // namespace core
