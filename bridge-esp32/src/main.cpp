// bridge main.cpp — Wi-Fi + NTP now; MQTT/scenes later. Talks the shared protocol
// to the Teensy over UART (or over USB-host CDC on the zero-mod route).
// SPDX-License-Identifier: GPL-2.0-or-later
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
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
#ifndef OTA_HOST
#define OTA_HOST "scope-clock-bridge"
#endif
#ifndef OTA_PASS
#define OTA_PASS ""            // empty = unauthenticated; .env should set one
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

// ---- send: draw lists and banners -------------------------------------------
// The bridge's half of the generic path. Layouts are specified in protocol.h;
// these builders are what P3 will drive from MQTT.

[[maybe_unused]] static void sendBanner(const char* text, uint16_t ms, uint8_t priority = 0) {
  uint8_t p[proto::MAX_PAYLOAD];
  p[0] = (uint8_t)(ms & 0xFF);
  p[1] = (uint8_t)(ms >> 8);
  p[2] = priority;
  uint8_t n = 3;
  for (const char* q = text; *q && n < proto::MAX_PAYLOAD; ++q) p[n++] = (uint8_t)*q;
  sendFrame(proto::Msg::Banner, p, n);
}

[[maybe_unused]] static void sendSetMode(uint8_t mode, uint8_t faceId = 0) {
  const uint8_t p[2] = { mode, faceId };
  sendFrame(proto::Msg::SetMode, p, sizeof(p));
}

// Accumulates items into a payload. Every add() checks the remaining space, so
// a list that grows too big loses its tail rather than overrunning the buffer.
struct [[maybe_unused]] ListBuilder {
  uint8_t buf[proto::MAX_PAYLOAD];
  uint8_t len = 1;      // buf[0] is the item count
  uint8_t count = 0;

  ListBuilder() { buf[0] = 0; }

  void put16(int16_t v) {
    buf[len++] = (uint8_t)(v & 0xFF);
    buf[len++] = (uint8_t)((v >> 8) & 0xFF);
  }
  bool room(uint8_t need) const { return (uint16_t)len + need <= proto::MAX_PAYLOAD; }

  void text(int16_t x, int16_t y, int16_t scale, const char* s) {
    uint8_t sl = 0; while (s[sl]) ++sl;
    if (!room((uint8_t)(8 + sl))) return;
    buf[len++] = 0x01; put16(x); put16(y); put16(scale); buf[len++] = sl;
    for (uint8_t i = 0; i < sl; ++i) buf[len++] = (uint8_t)s[i];
    buf[0] = ++count;
  }
  void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    if (!room(9)) return;
    buf[len++] = 0x02; put16(x0); put16(y0); put16(x1); put16(y1);
    buf[0] = ++count;
  }
  void circle(int16_t cx, int16_t cy, int16_t r) {
    if (!room(7)) return;
    buf[len++] = 0x03; put16(cx); put16(cy); put16(r);
    buf[0] = ++count;
  }
  void send() const { sendFrame(proto::Msg::PushList, buf, len); }
};

// ---- receive: the device talks back ----------------------------------------
// Mirror of the device's assembler. Same contiguous CRC, same refusal to
// believe a length byte that cannot fit.
namespace rx {
enum class St { Start, Id, Len, Payload, Crc };
St st = St::Start;
uint8_t id, len, idx, crc, buf[proto::MAX_PAYLOAD];
bool helloSeen = false;     // set when the device (re)introduces itself

void poll() {
  while (TO_DISPLAY.available()) {
    const uint8_t b = (uint8_t)TO_DISPLAY.read();
    switch (st) {
      case St::Start:   if (b == proto::START) st = St::Id; break;
      case St::Id:      id = b; crc = proto::crc8_update(0x00, b); st = St::Len; break;
      case St::Len:
        if (b > proto::MAX_PAYLOAD) { st = St::Start; break; }
        len = b; crc = proto::crc8_update(crc, b); idx = 0;
        st = len ? St::Payload : St::Crc;
        break;
      case St::Payload:
        buf[idx++] = b; crc = proto::crc8_update(crc, b);
        if (idx >= len) st = St::Crc;
        break;
      case St::Crc:
        if (b == crc && static_cast<proto::Msg>(id) == proto::Msg::Hello)
          helloSeen = true;
        st = St::Start;
        break;
    }
  }
}
} // namespace rx

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

  rx::poll();


  if (WiFi.status() != WL_CONNECTED) return;

  // OTA needs the network up, so arm it on first association rather than in
  // setup(). Deliberately no progress callbacks: they would print to Serial,
  // which IS the protocol link, and land in the middle of a frame.
  static bool otaReady = false;
  if (!otaReady) {
    ArduinoOTA.setHostname(OTA_HOST);
    if (OTA_PASS[0]) ArduinoOTA.setPassword(OTA_PASS);
    ArduinoOTA.begin();
    otaReady = true;
  }
  // Blocks only while an update is actually in flight, and the device rides
  // that out on its own RTC — which is the entire point of it keeping time.
  ArduinoOTA.handle();

  // Sync when the device asks, not merely when our own timer says so.
  //
  // Firing once at our boot and then hourly looks fine until you notice the
  // device is not necessarily listening at that moment — it may not have
  // enumerated us yet, or it may reboot later. Then it carries a stale RTC for
  // up to an hour. The device already announces itself with Hello on every
  // connection, so treat that as the request it is.
  const uint32_t now = millis();
  const bool due = rx::helloSeen
                 || (!everSynced ? (now - lastSync > 2000UL)
                                 : (now - lastSync > 3600000UL));
  if (!due) return;
  lastSync = now;

  // getLocalTime() hands back the epoch until the first NTP reply lands, so
  // year < 2001 means "not yet" — hold the request rather than ship 1970.
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year < 101) return;   // tm_year is since 1900
  pushLocalTime();
  rx::helloSeen = false;
  everSynced = true;

  // TODO(P2/P3): subscribe MQTT -> sendFrame(Banner/PushList ...);
  //              read frames back from the Teensy (EventEncoder/Button/Status).
}
