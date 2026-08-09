// settime.h — the knob-driven clock and calendar editor. See settime.cpp.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
struct DeviceState;
struct DrawList;

// Replaces the frame with the editor while dev.edit is set; a no-op otherwise.
// Called from the same slot as overlayNotify, i.e. after the per-face scale, so
// the editor is always full size whatever the face is scaled to.
void overlaySetTime(DeviceState& dev, DrawList& list);

// The inclusive range the given field may hold, for whichever kind is being
// edited. The day's upper bound depends on the month and year, which is why
// hal::input asks rather than assuming 31.
void editRange(const DeviceState& dev, uint8_t field, int& lo, int& hi);

// Pull the day back into the month it now lands in. Moving off the 31st into
// February has to leave a real date behind.
void clampDay(DeviceState& dev);

uint8_t daysInMonth(uint8_t month, uint8_t year2);
