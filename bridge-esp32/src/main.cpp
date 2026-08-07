// bridge main.cpp — Wi-Fi + NTP now; MQTT/scenes later. Talks the shared protocol
// to the Teensy over UART (or over USB-host CDC on the zero-mod route).
// SPDX-License-Identifier: GPL-2.0-or-later
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "protocol.h"     // shared/

// ---- config (TODO: move to NVS / captive portal) ----
static const char* WIFI_SSID = "YOUR_SSID";
static const char* WIFI_PASS = "YOUR_PASS";
static const char* TZ_POSIX  = "PST8PDT,M3.2.0,M11.1.0";  // host owns TZ + DST
static const char* NTP1 = "pool.ntp.org";

// UART to the Teensy. On the AtomS3U-in-USB route this is instead the native USB
// CDC (Serial); on an internal module use a hardware UART (Serial1 + pins).
#define TO_DISPLAY Serial1

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
  Serial.begin(115200);                 // USB CDC (debug / flashing)
  TO_DISPLAY.begin(115200, SERIAL_8N1, /*RX=*/44, /*TX=*/43);  // TODO: real pins

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
  static bool     everSynced = false;

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
