// wdt_teensy36.cpp — Kinetis MK66 watchdog.
//
// Teensyduino's startup hook leaves the watchdog disabled but writes
// WDOG_STCTRLH_ALLOWUPDATE, which is what lets user code enable it later. That
// matters: the unlock window is normally only open for a few hundred bus
// cycles after reset, so without ALLOWUPDATE this could only be done from a
// startup hook — at boot, before anything has proven it works.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal/watchdog.h"
#include <Arduino.h>

namespace {
// Generous against a ~17ms frame. The point is to catch a stopped loop, not to
// police jitter, and a tight timeout would only add risk.
constexpr uint16_t kTimeoutMs = 2000;
bool isArmed = false;
}

namespace hal { namespace wdt {

void feed() {
  if (!isArmed) return;
  // The two writes must be consecutive; an interrupt between them is refused
  // by the hardware and the refresh is lost.
  noInterrupts();
  WDOG_REFRESH = 0xA602;
  WDOG_REFRESH = 0xB480;
  interrupts();
}

void armAfter(uint32_t healthyMs) {
  if (isArmed || millis() < healthyMs) return;

  noInterrupts();
  WDOG_UNLOCK = WDOG_UNLOCK_SEQ1;
  WDOG_UNLOCK = WDOG_UNLOCK_SEQ2;
  __asm__ volatile ("nop\n nop\n");   // the unlock needs a moment to take
  WDOG_TOVALH  = 0;
  WDOG_TOVALL  = kTimeoutMs;          // LPO ticks at 1kHz, so this is ms
  WDOG_PRESC   = 0;
  // CLKSRC left clear = the 1kHz LPO, which keeps running whatever the core
  // clock is doing. WAITEN/STOPEN so a hang in any mode still counts.
  WDOG_STCTRLH = WDOG_STCTRLH_WDOGEN | WDOG_STCTRLH_ALLOWUPDATE
               | WDOG_STCTRLH_WAITEN | WDOG_STCTRLH_STOPEN;
  interrupts();

  isArmed = true;
  feed();
}

bool armed() { return isArmed; }

}}
