// hostdata.cpp — decode the weather and ticker messages.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hostdata.h"
#include <Arduino.h>

namespace host {
namespace {
Weather w;
Ticker  tk;

void field(char* dst, uint8_t cap, const uint8_t* src, uint8_t n) {
  const uint8_t k = n < (uint8_t)(cap - 1) ? n : (uint8_t)(cap - 1);
  for (uint8_t i = 0; i < k; ++i) dst[i] = (char)src[i];
  dst[k] = '\0';
}
} // namespace

// [tempC10:i16][sky:u8][placeLen:u8][place][detail]
void setWeather(const uint8_t* p, uint8_t len) {
  if (len < 4) return;
  const uint8_t pl = p[3];
  if ((uint16_t)pl + 4 > len) return;      // the one host-supplied length
  w.tempC10 = (int16_t)(p[0] | (p[1] << 8));
  w.sky = p[2] > Fog ? Cloud : p[2];
  field(w.place,  sizeof w.place,  p + 4, pl);
  field(w.detail, sizeof w.detail, p + 4 + pl, (uint8_t)(len - 4 - pl));
  w.valid = true;
}

void setTicker(const uint8_t* p, uint8_t len) {
  field(tk.text, sizeof tk.text, p, len);
  tk.stampMs = millis();                   // restart the scroll on new text
  tk.valid = tk.text[0] != '\0';
}

const Weather& weather() { return w; }
const Ticker&  ticker()  { return tk; }

}  // namespace host
