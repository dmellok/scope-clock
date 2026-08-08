// sdprobe.cpp — read-only look at the Teensy 3.6's built-in microSD socket.
//
// Diagnostic, driven from the front-jack console ('s'), because the answer to
// "does the 192kHz path need the clock opened?" is entirely "is there a card in
// there already?" and that is not knowable from outside the case.
//
// This DOES block — SD.begin() and directory reads take as long as they take —
// so the CRT stutters while it runs. That is acceptable for a manual probe and
// is exactly why it is not on any automatic path.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <Arduino.h>
#include <SD.h>
#include "debug.h"
#include "hal/watchdog.h"

namespace dbg {

void probeSd() {
  // Refuse to run until the watchdog is armed.
  //
  // With an empty socket, SD.begin(BUILTIN_SDCARD) does not return false — it
  // spins in SDIO init waiting for a card that will never answer. Run that
  // before hal::wdt::armAfter(15000) has fired and the board hangs with nothing
  // to rescue it: the CRT freezes and the clock stops until it is reflashed.
  // Armed, the same hang is just a 2s watchdog reset.
  if (millis() < 20000) {
    say("sd: not yet — wait for the watchdog to arm (20s uptime), or an empty");
    say("sd: socket will hang the board instead of resetting it");
    return;
  }
  hal::wdt::feed();
  say("sd: mounting BUILTIN_SDCARD...");
  if (!SD.begin(BUILTIN_SDCARD)) {
    say("sd: NO CARD (or unreadable) — the slot is empty or unformatted");
    return;
  }
  hal::wdt::feed();
  say("sd: mounted");

  File root = SD.open("/");
  if (!root) { say("sd: root unreadable"); return; }

  int files = 0;
  uint64_t bytes = 0;
  for (;;) {
    File e = root.openNextFile();
    if (!e) break;
    hal::wdt::feed();
    const uint32_t sz = e.isDirectory() ? 0 : (uint32_t)e.size();
    if (files < 20) sayf("sd:   %-32s %s%lu", e.name(),
                         e.isDirectory() ? "<dir> " : "", (unsigned long)sz);
    ++files; bytes += sz;
    e.close();
  }
  root.close();
  sayf("sd: %d entries in root, %lu KB used by them",
       files, (unsigned long)(bytes / 1024));
}

}  // namespace dbg
