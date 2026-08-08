// bridge main.cpp — Wi-Fi + NTP now; MQTT/scenes later. Talks the shared protocol
// to the Teensy over UART (or over USB-host CDC on the zero-mod route).
// SPDX-License-Identifier: GPL-2.0-or-later
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
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
#ifndef MQTT_HOST
#define MQTT_HOST ""           // empty = run with no broker at all
#endif
#ifndef MQTT_PORT
#define MQTT_PORT "1883"
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASS
#define MQTT_PASS ""
#endif
#ifndef MQTT_PREFIX
#define MQTT_PREFIX "scopeclock"
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

// ---- MQTT: where the smarts live -------------------------------------------
// Everything above this point is mechanism; this is the policy layer. It is on
// the bridge and not the display MCU on purpose — rule 5, keep the radio off
// the beam.

static bool rxHello = false;      // device (re)introduced itself
static bool statusFresh = true;   // force a full republish after (re)connect
static void onFrame(uint8_t id, const uint8_t* p, uint8_t len);

static WiFiClient   net;
static PubSubClient mqtt(net);

static const char* const kFaceNames[] = { "hands", "digital", "cube" };
constexpr uint8_t kFaceCount = 3;
static uint8_t  curFace       = 0;
static uint8_t  curBrightness = 255;
static uint16_t bannerMs      = 8000;

static String topic(const char* leaf) { return String(MQTT_PREFIX) + "/" + leaf; }

// Parse one scene line into the builder. The text form exists so a scene can be
// written by hand or by an HA template without anyone encoding binary:
//   T <x> <y> <scale> <text to end of line>
//   L <x0> <y0> <x1> <y1>
//   C <cx> <cy> <r>
static void sceneLine(ListBuilder& b, const String& ln) {
  if (ln.length() < 3) return;
  const char kind = ln[0];
  int v[4] = {0,0,0,0};
  int n = 0, i = 1;
  while (n < 4 && i < (int)ln.length()) {
    while (i < (int)ln.length() && ln[i] == ' ') ++i;
    if (i >= (int)ln.length()) break;
    const int start = i;
    if (ln[i] == '-') ++i;
    while (i < (int)ln.length() && isDigit(ln[i])) ++i;
    if (i == start) break;
    v[n++] = ln.substring(start, i).toInt();
    if (kind == 'T' && n == 3) break;      // rest of the line is the string
  }
  if (kind == 'T' && n >= 3) {
    while (i < (int)ln.length() && ln[i] == ' ') ++i;
    b.text((int16_t)v[0], (int16_t)v[1], (int16_t)v[2], ln.substring(i).c_str());
  } else if (kind == 'L' && n >= 4) {
    b.line((int16_t)v[0], (int16_t)v[1], (int16_t)v[2], (int16_t)v[3]);
  } else if (kind == 'C' && n >= 3) {
    b.circle((int16_t)v[0], (int16_t)v[1], (int16_t)v[2]);
  }
}

static void publishState() {
  mqtt.publish(topic("face/state").c_str(), kFaceNames[curFace], true);
  char b[8]; snprintf(b, sizeof b, "%u", curBrightness);
  mqtt.publish(topic("brightness/state").c_str(), b, true);
}

static void onMqtt(char* t, uint8_t* payload, unsigned int len) {
  String msg;
  msg.reserve(len);
  for (unsigned i = 0; i < len; ++i) msg += (char)payload[i];
  const String tp(t);

  if (tp == topic("banner/set")) {
    sendBanner(msg.c_str(), bannerMs);
  } else if (tp == topic("banner/duration")) {
    const long v = msg.toInt();
    if (v > 0 && v <= 60000) bannerMs = (uint16_t)v;
  } else if (tp == topic("face/set")) {
    for (uint8_t i = 0; i < kFaceCount; ++i) {
      if (msg.equalsIgnoreCase(kFaceNames[i])) { curFace = i; break; }
    }
    sendSetMode(0, curFace);            // 0 = local face
    publishState();
  } else if (tp == topic("brightness/set")) {
    const long v = msg.toInt();
    curBrightness = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    const uint8_t p[1] = { curBrightness };
    sendFrame(proto::Msg::SetBrightness, p, 1);
    publishState();
  } else if (tp == topic("scene/set")) {
    // Empty payload means "give the clock back to its own face".
    if (msg.length() == 0) { sendSetMode(0, curFace); return; }
    ListBuilder b;
    int start = 0;
    while (start < (int)msg.length()) {
      int nl = msg.indexOf('\n', start);
      if (nl < 0) nl = msg.length();
      String ln = msg.substring(start, nl);
      ln.trim();
      if (ln.length()) sceneLine(b, ln);
      start = nl + 1;
    }
    if (b.count) b.send();
  }
}

// Announce ourselves so the clock turns up in Home Assistant as a device with
// controls, rather than as topics somebody has to write YAML for.
static void publishDiscovery() {
  const String avail = topic("availability");
  const String dev = String("\"dev\":{\"ids\":[\"" MQTT_PREFIX "\"],\"name\":\"Scope Clock\",")
                   + "\"mf\":\"Cathode Corner\",\"mdl\":\"SCTV\"}";
  String cfg;

  cfg = String("{\"name\":\"Face\",\"uniq_id\":\"" MQTT_PREFIX "_face\",")
      + "\"cmd_t\":\"" + topic("face/set") + "\",\"stat_t\":\"" + topic("face/state") + "\","
      + "\"options\":[\"hands\",\"digital\",\"cube\"],"
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish(("homeassistant/select/" MQTT_PREFIX "/face/config"), cfg.c_str(), true);

  cfg = String("{\"name\":\"Brightness\",\"uniq_id\":\"" MQTT_PREFIX "_bri\",")
      + "\"cmd_t\":\"" + topic("brightness/set") + "\",\"stat_t\":\"" + topic("brightness/state") + "\","
      + "\"min\":0,\"max\":255,\"mode\":\"slider\","
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish(("homeassistant/number/" MQTT_PREFIX "/brightness/config"), cfg.c_str(), true);

  cfg = String("{\"name\":\"Banner\",\"uniq_id\":\"" MQTT_PREFIX "_banner\",")
      + "\"cmd_t\":\"" + topic("banner/set") + "\","
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish(("homeassistant/notify/" MQTT_PREFIX "/banner/config"), cfg.c_str(), true);

  // Diagnostics, from the device's own Status frames.
  struct Sen { const char* id; const char* name; const char* leaf;
               const char* unit; const char* ic; };
  static const Sen sens[] = {
    {"uptime",  "Uptime",     "uptime/state",  "s",  "mdi:timer-outline"},
    {"frame",   "Frame time", "frame/state",   "us", "mdi:speedometer"},
    {"timeset", "Time synced","timeset/state", "s",  "mdi:clock-check-outline"},
  };
  for (const Sen& sn : sens) {
    cfg = String("{\"name\":\"") + sn.name + "\",\"uniq_id\":\"" MQTT_PREFIX "_" + sn.id + "\","
        + "\"stat_t\":\"" + topic(sn.leaf) + "\",\"unit_of_meas\":\"" + sn.unit + "\","
        + "\"ic\":\"" + sn.ic + "\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\","
        + "\"avty_t\":\"" + avail + "\"," + dev + "}";
    mqtt.publish((String("homeassistant/sensor/" MQTT_PREFIX "/") + sn.id + "/config").c_str(),
                 cfg.c_str(), true);
  }

  cfg = String("{\"name\":\"RTC\",\"uniq_id\":\"" MQTT_PREFIX "_rtc\",")
      + "\"stat_t\":\"" + topic("rtc/state") + "\",\"dev_cla\":\"problem\","
      + "\"pl_on\":\"OFF\",\"pl_off\":\"ON\",\"ent_cat\":\"diagnostic\","
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish(("homeassistant/binary_sensor/" MQTT_PREFIX "/rtc/config"), cfg.c_str(), true);

  // Device triggers, so the knob and button show up in the automation UI as
  // things to trigger on rather than topics to remember.
  struct Trg { const char* id; const char* type; const char* sub;
               const char* leaf; const char* payload; };
  static const Trg trgs[] = {
    {"press", "button_short_press", "button",  "event/button",  "press"},
    {"long",  "button_long_press",  "button",  "event/button",  "long"},
    {"left",  "button_short_press", "knob_ccw","event/encoder", "left"},
    {"right", "button_short_press", "knob_cw", "event/encoder", "right"},
  };
  for (const Trg& tg : trgs) {
    cfg = String("{\"automation_type\":\"trigger\",\"type\":\"") + tg.type + "\","
        + "\"subtype\":\"" + tg.sub + "\",\"topic\":\"" + topic(tg.leaf) + "\","
        + "\"payload\":\"" + tg.payload + "\"," + dev + "}";
    mqtt.publish((String("homeassistant/device_automation/" MQTT_PREFIX "/") + tg.id + "/config").c_str(),
                 cfg.c_str(), true);
  }
}

static void mqttConnect() {
  if (!strlen(MQTT_HOST)) return;                 // no broker configured
  static uint32_t lastTry = 0;
  if (mqtt.connected() || millis() - lastTry < 5000) return;
  lastTry = millis();

  const String avail = topic("availability");
  // Last will, so HA marks the clock unavailable if the bridge drops off
  // rather than showing stale controls that silently do nothing.
  const bool ok = strlen(MQTT_USER)
    ? mqtt.connect(MQTT_PREFIX, MQTT_USER, MQTT_PASS, avail.c_str(), 0, true, "offline")
    : mqtt.connect(MQTT_PREFIX, avail.c_str(), 0, true, "offline");
  if (!ok) return;

  mqtt.publish(avail.c_str(), "online", true);
  publishDiscovery();
  publishState();
  statusFresh = true;                 // republish everything on this connection
  mqtt.subscribe(topic("banner/set").c_str());
  mqtt.subscribe(topic("banner/duration").c_str());
  mqtt.subscribe(topic("face/set").c_str());
  mqtt.subscribe(topic("brightness/set").c_str());
  mqtt.subscribe(topic("scene/set").c_str());
}

// Telemetry and input, republished for Home Assistant. Only on change, so a
// 5s status heartbeat does not become 5s of MQTT traffic.
static void publishStatus(const proto::StatusPayload& s) {
  static proto::StatusPayload prev{};
  char b[16];

  // Do not consume the change-detection state while offline. Status arrives
  // every 5s and the broker connection comes up later, so without this the
  // first frame is compared against, silently dropped, and stored as `prev` —
  // after which anything that never changes again (rtcOk, mode) is never
  // published at all. That is exactly what happened: the constantly-moving
  // sensors appeared and the steady ones did not.
  if (!mqtt.connected()) return;
  const bool first = statusFresh;

  if (first || s.faceId != prev.faceId) {
    // The knob can change the face too, so this is what keeps HA honest about
    // what is actually on the tube rather than what HA last asked for.
    if (s.faceId < kFaceCount) {
      curFace = s.faceId;
      mqtt.publish(topic("face/state").c_str(), kFaceNames[curFace], true);
    }
  }
  if (first || s.brightness != prev.brightness) {
    curBrightness = s.brightness;
    snprintf(b, sizeof b, "%u", s.brightness);
    mqtt.publish(topic("brightness/state").c_str(), b, true);
  }
  if (first || s.rtcOk != prev.rtcOk)
    mqtt.publish(topic("rtc/state").c_str(), s.rtcOk ? "ON" : "OFF", true);
  if (first || s.mode != prev.mode)
    mqtt.publish(topic("mode/state").c_str(), s.mode ? "pushed" : "face", true);

  // These move constantly, so rate-limit rather than change-detect.
  static uint32_t lastSlow = 0;
  if (first || millis() - lastSlow > 30000) {
    lastSlow = millis();
    snprintf(b, sizeof b, "%lu", (unsigned long)s.uptimeS);
    mqtt.publish(topic("uptime/state").c_str(), b, true);
    snprintf(b, sizeof b, "%lu", (unsigned long)s.frameUs);
    mqtt.publish(topic("frame/state").c_str(), b, true);
    // 0xFFFF means "never synced since the device booted". Skipping the
    // publish would leave the previous retained value in place, which then
    // claims a sync that predates the reboot — an age larger than the uptime.
    // Publish an empty payload instead: that clears the retained topic and HA
    // shows the sensor as unknown, which is the truth.
    if (s.setAgeS == 0xFFFF) mqtt.publish(topic("timeset/state").c_str(), "", true);
    else { snprintf(b, sizeof b, "%u", s.setAgeS);
           mqtt.publish(topic("timeset/state").c_str(), b, true); }
  }
  prev = s; statusFresh = false;
}

static void onFrame(uint8_t id, const uint8_t* p, uint8_t len) {
  switch (static_cast<proto::Msg>(id)) {
    case proto::Msg::Hello:
      rxHello = true;
      break;
    case proto::Msg::Status: {
      if (len < sizeof(proto::StatusPayload)) break;
      proto::StatusPayload s;
      memcpy(&s, p, sizeof s);          // the payload need not be aligned
      publishStatus(s);
      break;
    }
    case proto::Msg::EventEncoder:
      if (len >= 1)
        mqtt.publish(topic("event/encoder").c_str(), (int8_t)p[0] > 0 ? "right" : "left");
      break;
    case proto::Msg::EventButton:
      if (len >= 1)
        mqtt.publish(topic("event/button").c_str(), p[0] ? "long" : "press");
      break;
    default: break;
  }
}

// ---- receive: the device talks back ----------------------------------------
// Mirror of the device's assembler. Same contiguous CRC, same refusal to
// believe a length byte that cannot fit.
namespace rx {
enum class St { Start, Id, Len, Payload, Crc };
St st = St::Start;
uint8_t id, len, idx, crc, buf[proto::MAX_PAYLOAD];
// helloSeen lives outside the namespace so onFrame can set it

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
        if (b == crc) onFrame(id, buf, len);
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
  // Power saving costs ~300ms of latency, which is invisible for an hourly
  // time sync and very visible for a notification that should appear now.
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTzTime(TZ_POSIX, NTP1);         // NTP + timezone/DST handled here

  if (strlen(MQTT_HOST)) {
    mqtt.setServer(MQTT_HOST, (uint16_t)atoi(MQTT_PORT));
    mqtt.setCallback(onMqtt);
    mqtt.setSocketTimeout(2);           // do not sit on a dead broker
    mqtt.setBufferSize(768);            // discovery payloads exceed the 256 default
  }
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

  mqttConnect();
  mqtt.loop();

  // Sync when the device asks, not merely when our own timer says so.
  //
  // Firing once at our boot and then hourly looks fine until you notice the
  // device is not necessarily listening at that moment — it may not have
  // enumerated us yet, or it may reboot later. Then it carries a stale RTC for
  // up to an hour. The device already announces itself with Hello on every
  // connection, so treat that as the request it is.
  const uint32_t now = millis();
  const bool due = rxHello
                 || (!everSynced ? (now - lastSync > 2000UL)
                                 : (now - lastSync > 3600000UL));
  if (!due) return;
  lastSync = now;

  // getLocalTime() hands back the epoch until the first NTP reply lands, so
  // year < 2001 means "not yet" — hold the request rather than ship 1970.
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year < 101) return;   // tm_year is since 1900
  pushLocalTime();
  rxHello = false;
  everSynced = true;

  // TODO(P2/P3): subscribe MQTT -> sendFrame(Banner/PushList ...);
  //              read frames back from the Teensy (EventEncoder/Button/Status).
}
