// wasm.cpp — the same frames, in the browser's own tab.
//
// The other transport is websim.cpp, which serves them over a socket; this one
// hands JavaScript a pointer into the wasm heap. The page (index.html) picks
// whichever it finds, so there is one viewer and two ways to run it: a native
// binary on your LAN, or a static page anybody can open with nothing installed.
//
// No main loop here. The page drives, calling ws_frame() once per animation
// frame, which keeps the browser's scheduler in charge of the pace.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core.h"
#include <emscripten/emscripten.h>

extern "C" {

EMSCRIPTEN_KEEPALIVE void ws_init() { core::init(); }

EMSCRIPTEN_KEEPALIVE const char* ws_faces_json() {
  return core::facesJson().c_str();
}

// Returns a pointer into the heap, valid until the next call. The page copies
// it straight into a typed array, which is why the frame format is bytes
// rather than anything that would need marshalling.
static const std::string* g_last = nullptr;

EMSCRIPTEN_KEEPALIVE const uint8_t* ws_frame(int face, int scale, int stepMs) {
  g_last = &core::frame(face, scale, (uint32_t)stepMs);
  return (const uint8_t*)g_last->data();
}

EMSCRIPTEN_KEEPALIVE int ws_frame_len() {
  return g_last ? (int)g_last->size() : 0;
}

}  // extern "C"
