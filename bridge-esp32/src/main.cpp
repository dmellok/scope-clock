// bridge main.cpp — Wi-Fi + NTP now; MQTT/scenes later. Talks the shared protocol
// to the Teensy over UART (or over USB-host CDC on the zero-mod route).
// SPDX-License-Identifier: GPL-2.0-or-later
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "protocol.h"     // shared/

// ---- config ----
// Supplied at build time from .env by load_env.py (see .env.example). That file
// is gitignored: this repo is public, so no credential may live in source.
// These fallbacks only exist so a fresh clone still compiles.
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_SSID"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_PASS"
#endif
#ifndef TZ_POSIX
#define TZ_POSIX "PST8PDT,M3.2.0,M11.1.0"   // bridge owns TZ + DST, not the device
#endif
#ifndef NTP1
#define NTP1 "pool.ntp.org"
#endif

// Link to the Teensy: the AtomS3U's native USB CDC, plugged into the clock's
// rear USB-A host jack. The Teensy is the USB host and enumerates us as
// CDC-ACM, so no wiring is involved.
//
// NOTE: this is the same `Serial` the ESP32 uses for its console, so nothing
// here may print debug text — it would land in the middle of a frame and the
// device would drop it on the CRC. Use Serial0/UART or a network log instead.
#define TO_DISPLAY Serial

static void sendFrame(proto::Msg id, const uint8_t* p, uint8_t len) {
  TO_DISPLAY.write(proto::START);
  TO_DISPLAY.write((uint8_t)id);
  TO_DISPLAY.write(len);
  if (len) TO_DISPLAY.write(p, len);
  TO_DISPLAY.write(proto::frameCrc((uint8_t)id, len, p));   // must match the device
}

static void pushLocalTime() {
  struct tm t;
  if (!getLocalTime(&t)) return;
  proto::SetTimePayload p{
    (uint8_t)(t.tm_year % 100), (uint8_t)(t.tm_mon + 1), (uint8_t)t.tm_mday,
    (uint8_t)t.tm_hour, (uint8_t)t.tm_min, (uint8_t)t.tm_sec };
  sendFrame(proto::Msg::SetTime, reinterpret_cast<uint8_t*>(&p), sizeof(p));
}

void setup() {
  TO_DISPLAY.begin(115200);             // native USB CDC to the display MCU

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTzTime(TZ_POSIX, NTP1);         // NTP + timezone/DST handled here
}

// Wi-Fi comes and goes, and the clock has to survive that: the device keeps
// its own time from the RTC, so a bridge that cannot reach the network is an
// inconvenience, not an outage. Nothing here blocks waiting for a link.
void loop() {
  static uint32_t lastSync  = 0;
  static uint32_t lastPing  = 0;
  static bool     everSynced = false;

  // Heartbeat, so the device can tell "bridge attached but silent" from
  // "bridge talking". Sent regardless of Wi-Fi: the link being up is a
  // separate question from the network being up.
  if (millis() - lastPing > 5000UL) {
    lastPing = millis();
    sendFrame(proto::Msg::Ping, nullptr, 0);
  }

  if (WiFi.status() != WL_CONNECTED) return;

  // Send the first SET_TIME as soon as SNTP has actually landed a real date,
  // then re-sync hourly to ride over DST changes. getLocalTime() reports the
  // epoch until the first NTP reply arrives, so year < 2001 means "not yet".
  const uint32_t now = millis();
  const bool due = !everSynced ? (now - lastSync > 2000UL)
                               : (now - lastSync > 3600000UL);
  if (!due) return;
  lastSync = now;

  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year < 101) return;   // tm_year is since 1900
  pushLocalTime();
  everSynced = true;

  // TODO(P2/P3): subscribe MQTT -> sendFrame(Banner/PushList ...);
  //              read frames back from the Teensy (EventEncoder/Button/Status).
}
