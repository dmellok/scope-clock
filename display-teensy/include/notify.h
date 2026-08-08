// notify.h — title + body overlay, placed and temporary. See notify.cpp.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
struct DeviceState;
struct DrawList;

// Draw the active notification, if any, and expire it when its time is up.
// MUST be called after txt::centerLines: the items it adds carry real
// positions, and any positioned text opts the whole list out of centring.
void overlayNotify(DeviceState& dev, DrawList& list);
