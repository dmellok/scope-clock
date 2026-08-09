// hostdata.h — the small things the host tells the display about.
//
// Weather and the ticker share a home because they share a shape: a short
// payload the host sends when it changes, held until it changes again. Neither
// belongs in DeviceState — a face renderer is handed only (ClockState, DrawList),
// which is why hal::midi and np:: own their state the same way.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace host {

// Condition codes, chosen to be drawable rather than exhaustive: a vector tube
// can tell sun from cloud from rain, and cannot usefully tell drizzle from light
// rain. The host maps whatever vocabulary it has onto these.
enum Sky : uint8_t { Clear = 0, PartCloud, Cloud, Rain, Snow, Storm, Fog };

struct Weather {
  int16_t tempC10 = 0;      // tenths, so 21.5 survives the wire
  uint8_t sky = Cloud;
  char    place[24] = {0};
  char    detail[28] = {0}; // "18/24" or "feels 19", host's choice
  bool    valid = false;
};

struct Ticker {
  char     text[160] = {0};
  uint32_t stampMs = 0;
  bool     valid = false;
};

void setWeather(const uint8_t* payload, uint8_t len);
void setTicker(const uint8_t* payload, uint8_t len);
const Weather& weather();
const Ticker&  ticker();

}  // namespace host
