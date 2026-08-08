// hal/watchdog.h — last line of defence for the phosphor.
//
// A hung render loop is not a frozen picture: it leaves the beam parked and
// UNBLANKED on a single point, which burns a permanent spot into the tube.
// Everything else in this project is recoverable; that is not. So if the loop
// stops, the part must be reset — dac::init() blanks the beam before anything
// else runs, so a reset is also a rescue.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace hal { namespace wdt {

// Call once per pass. Cheap, and safe before the watchdog is armed.
void feed();

// Arm once the loop has been running for `healthyMs`, and not before.
//
// Arming at boot would risk a reset loop, and a reset loop on a board whose
// program button is inside a sealed clock is the one failure that costs a
// disassembly. Waiting guarantees a window on every boot — longer than USB
// takes to enumerate — in which new firmware can always be flashed. Even a
// firmware that hangs reliably still comes up, stays up for that window, and
// can be replaced.
void armAfter(uint32_t healthyMs);

bool armed();

}}
