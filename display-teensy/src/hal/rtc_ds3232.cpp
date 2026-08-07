// rtc_ds3232.cpp — ported from SCTVcode.ino readRTCtime/writeRTCtime/GetWDay.
// Holds LOCAL time: the bridge applies timezone + DST before sending SET_TIME,
// so there are no zone tables on this MCU and never should be.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hal/rtc.h"
#include "state.h"
#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint8_t kAddr = 0x68;

// The loop is the CRT refresh, so keep I2C off it as much as possible: at
// 400 kHz a 7-byte read is ~0.2 ms, and there is no point doing even that 60
// times a second to watch a seconds digit. 100 ms of staleness is invisible.
constexpr uint32_t kReadIntervalMs = 100;
uint32_t lastReadMs = 0;
bool     everRead   = false;

// The DS3232 stores packed BCD; `mask` selects the tens digit's valid bits,
// which differ per register (hours have a 12/24 flag, months a century bit).
inline int fromBcd(uint8_t v, uint8_t mask) {
  return (v & 0x0f) + ((v & mask) >> 4) * 10;
}
inline uint8_t toBcd(int v) { return (uint8_t)((v / 10) << 4 | (v % 10)); }

// Sakamoto-style day of week, 0 = Sunday. The DS3232's own weekday register is
// just a counter nothing keeps honest, so derive it from the date instead.
int weekday(int d, int m, int yr, int cent) {
  int y = cent * 100 + yr;
  return (d += m < 3 ? y-- : y - 2, 23 * m / 9 + d + 4 + y / 4 - cent + cent / 4) % 7;
}
} // namespace

namespace hal { namespace rtc {

void init() {
  Wire.begin();
  Wire.setClock(400000);   // DS3232 is a fast-mode part; keeps the read short
}

bool present() {
  Wire.beginTransmission(kAddr);
  return Wire.endTransmission() == 0;
}

void read(ClockState& s) {
  const uint32_t now = millis();
  if (everRead && (now - lastReadMs) < kReadIntervalMs) return;
  lastReadMs = now;

  Wire.beginTransmission(kAddr);
  Wire.write(0x00);                       // registers 0..6 are seconds..year
  if (Wire.endTransmission() != 0) { s.rtcPresent = false; return; }
  if (Wire.requestFrom(kAddr, (uint8_t)7) != 7) { s.rtcPresent = false; return; }

  const uint8_t sc = Wire.read();
  const uint8_t mi = Wire.read();
  const uint8_t hr = Wire.read();
  Wire.read();                            // weekday register — recomputed below
  const uint8_t dy = Wire.read();
  const uint8_t mo = Wire.read();
  const uint8_t yr = Wire.read();

  s.second = fromBcd(sc, 0x70);
  s.minute = fromBcd(mi, 0x70);
  s.hour   = fromBcd(hr, 0x30);           // bit 6 selects 12h mode; we run 24h
  s.day    = fromBcd(dy, 0x30);
  s.month  = fromBcd(mo, 0x10);           // bit 7 is the century flag
  s.year   = fromBcd(yr, 0xf0);
  s.wday   = weekday(s.day, s.month, s.year, 20);
  s.rtcPresent = true;
  everRead = true;
}

void write(const ClockState& s) {
  Wire.beginTransmission(kAddr);
  Wire.write(0x00);
  Wire.write(toBcd(s.second));            // bit 7 clear = oscillator running
  Wire.write(toBcd(s.minute));
  Wire.write(toBcd(s.hour));              // bit 6 clear = 24 hour mode
  Wire.write((uint8_t)(weekday(s.day, s.month, s.year, 20) + 1));  // DS3232 is 1-7
  Wire.write(toBcd(s.day));
  Wire.write(toBcd(s.month));
  Wire.write(toBcd(s.year));
  Wire.endTransmission();

  everRead = false;   // re-read on the next pass so state reflects the chip
}

}}
