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
  uint8_t hdr[2] = { (uint8_t)id, len };
  TO_DISPLAY.write(proto::START);
  TO_DISPLAY.write(hdr, 2);
  if (len) TO_DISPLAY.write(p, len);
  uint8_t c = proto::crc8(hdr, 2);
  if (len) c ^= proto::crc8(p, len);   // skeleton crc
  TO_DISPLAY.write(c);
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
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  configTzTime(TZ_POSIX, NTP1);         // NTP + timezone/DST handled here
  delay(1500);
  pushLocalTime();                       // first SET_TIME
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last > 3600000UL) {     // re-sync hourly (covers DST edges)
    last = millis();
    pushLocalTime();
  }
  // TODO(P2/P3): subscribe MQTT -> sendFrame(Banner/PushList ...);
  //              read frames back from the Teensy (EventEncoder/Button/Status).
}
