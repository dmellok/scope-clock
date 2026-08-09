// menu.h — the knob-driven settings list. See menu.cpp.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
struct DeviceState;
struct DrawList;

namespace Menu {

// Order is the display order. Nothing persists this, so it can be reordered
// freely — unlike the face registry, whose order is the wire id.
enum : uint8_t {
  SetTime = 0, SetDate, FaceSize, Typeface, Drift, Info, Exit, kCount
};

// The line shown under the selected entry, or nullptr when it has no value.
const char* valueFor(uint8_t item);

// Do whatever the selected entry does. Some close the menu (the editors, size,
// exit); the toggles stay so you can see the value change under the label.
void activate(DeviceState& dev);

}  // namespace Menu

// Replaces the frame with the list while dev.menuMode is set; a no-op
// otherwise. Drawn after the per-face scale, like the other overlays.
void overlayMenu(DeviceState& dev, DrawList& list);
