// settime.h — the knob-driven clock setter. See settime.cpp.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
struct DeviceState;
struct DrawList;

// Replaces the frame with the setter while dev.timeMode is set; a no-op
// otherwise. Called from the same slot as overlayNotify, i.e. after the
// per-face scale, so the editor is always full size whatever the face is.
void overlaySetTime(DeviceState& dev, DrawList& list);
