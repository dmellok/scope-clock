// bridge main.cpp — Wi-Fi + NTP now; MQTT/scenes later. Talks the shared protocol
// to the Teensy over UART (or over USB-host CDC on the zero-mod route).
// SPDX-License-Identifier: GPL-2.0-or-later
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include "protocol.h"     // shared/
#include "webui.h"
#include "soc/usb_serial_jtag_struct.h"
#include "soc/system_reg.h"

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

// ---- runtime configuration --------------------------------------------------
// The .env values above are compiled in and remain the bootstrap. NVS holds any
// overrides set through the web page; an absent or empty override falls back to
// the build-time value, so a fresh device behaves exactly as it did before this
// existed. That fallback is the whole safety story — see cfgPanic().
static Preferences prefs;
static WebServer   web(80);

static struct Cfg {
  String wifiSsid, wifiPass, tz, ntp;
  String mqttHost, mqttPort, mqttUser, mqttPass, mqttPrefix;
  String npTopic, gaugeTopic;      // arbitrary topics feeding the data faces
} cfg;

static bool cfgOverridden = false;   // any value came from NVS rather than .env

static String cfgGet(const char* key, const char* fallback) {
  const String v = prefs.getString(key, "");
  if (v.length()) { cfgOverridden = true; return v; }
  return String(fallback);
}

static void cfgLoad() {
  prefs.begin("scopeclock", true);          // read-only
  cfg.wifiSsid   = cfgGet("wifi_ssid",  WIFI_SSID);
  cfg.wifiPass   = cfgGet("wifi_pass",  WIFI_PASS);
  cfg.tz         = cfgGet("tz",         TZ_POSIX);
  cfg.ntp        = cfgGet("ntp",        NTP1);
  cfg.mqttHost   = cfgGet("mq_host",    MQTT_HOST);
  cfg.mqttPort   = cfgGet("mq_port",    MQTT_PORT);
  cfg.mqttUser   = cfgGet("mq_user",    MQTT_USER);
  cfg.mqttPass   = cfgGet("mq_pass",    MQTT_PASS);
  cfg.mqttPrefix = cfgGet("mq_prefix",  MQTT_PREFIX);
  cfg.npTopic    = cfgGet("np_topic",   "straybot/playing");
  cfg.gaugeTopic = cfgGet("gg_topic",   "claude/usage");
  prefs.end();
}

// Wipe the overrides and reboot onto the compiled-in values.
//
// This exists because the web page can lock the bridge out of its own network,
// and Wi-Fi is how firmware gets here — a bad SSID would otherwise cost a
// physical flash, which for a board living inside the clock means dismantling
// it. So a prolonged failure to associate is treated as "the config is wrong"
// rather than "the network is down": .env is known to have worked once.
static void cfgPanic() {
  prefs.begin("scopeclock", false);
  prefs.clear();
  prefs.end();
  delay(100);
  ESP.restart();
}

// Link to the Teensy: the AtomS3U's native USB CDC, plugged into the clock's
// rear USB-A host jack. The Teensy is the USB host and enumerates us as
// CDC-ACM, so no wiring is involved.
//
// NOTE: this is the same `Serial` the ESP32 uses for its console, so nothing
// here may print debug text — it would land in the middle of a frame and the
// device would drop it on the CRC. Use Serial0/UART or a network log instead.
#define TO_DISPLAY Serial

static void sendFrame(proto::Msg id, const uint8_t* p, uint8_t len) {
  // Wait for room rather than overrun the ring.
  //
  // The CDC TX ring is 256 bytes — setTxBufferSize cannot enlarge it, because
  // ARDUINO_USB_CDC_ON_BOOT means the core has already created it before
  // setup() runs — and it drains about one 64-byte packet per refresh, since
  // the device re-queues its IN transfer once per frame. Write faster than
  // that and the tail is silently discarded: the API reports success, the
  // frame never arrives, and neither end sees an error. Small frames always
  // fitted, which is why only artwork ever broke.
  const int need = (int)len + 4;
  for (uint32_t t0 = millis(); TO_DISPLAY.availableForWrite() < need; ) {
    if (millis() - t0 > 400) break;   // give up rather than block the bridge
    delay(2);
  }
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

// Notify [ms:u16][place:u8][titleLen:u8][title][body]
static void sendNotify(const String& title, const String& body,
                       uint8_t place, bool solo, uint16_t ms) {
  uint8_t p[proto::MAX_PAYLOAD];
  p[0] = (uint8_t)(ms & 0xFF);
  p[1] = (uint8_t)(ms >> 8);
  p[2] = (uint8_t)(place | (solo ? 0x80 : 0));   // high bit: blank the face
  // Title is length-prefixed by a single byte, so it cannot be longer than one,
  // and both together cannot exceed the payload. Truncate here rather than let
  // the device decide — it has the smaller buffers.
  uint8_t tl = (uint8_t)(title.length() > 31 ? 31 : title.length());
  uint16_t n = 4;
  for (uint8_t i = 0; i < tl && n < proto::MAX_PAYLOAD; ++i) p[n++] = (uint8_t)title[i];
  tl = (uint8_t)(n - 4);
  p[3] = tl;
  for (uint16_t i = 0; i < body.length() && n < proto::MAX_PAYLOAD; ++i)
    p[n++] = (uint8_t)body[i];
  sendFrame(proto::Msg::Notify, p, (uint8_t)n);
}

// Minimal field lookup for the flat objects Home Assistant's notify service
// sends. A JSON library for four known keys would be a dependency and a heap
// allocator in the MQTT callback; this is neither.
static String jsonField(const String& src, const char* key) {
  const String pat = String("\"") + key + "\"";
  const int k = src.indexOf(pat);
  if (k < 0) return String();
  int i = src.indexOf(':', k + pat.length());
  if (i < 0) return String();
  ++i;
  while (i < (int)src.length() && isSpace(src[i])) ++i;
  if (i >= (int)src.length()) return String();
  if (src[i] == '"') {
    String out;
    for (++i; i < (int)src.length() && src[i] != '"'; ++i) {
      if (src[i] == '\\' && i + 1 < (int)src.length()) ++i;
      out += src[i];
    }
    return out;
  }
  int e = i;
  // '.' included: extra_usage.utilization arrives as 0.314…, and stopping at
  // the point would read it as 0 and throw the fraction away.
  while (e < (int)src.length() && (isDigit(src[e]) || src[e] == '-' || src[e] == '.')) ++e;
  return src.substring(i, e);
}

// The value of a nested object, by brace matching, so "utilization" can be
// looked up inside "five_hour" without colliding with the other two.
static String jsonObject(const String& src, const char* key) {
  const String pat = String("\"") + key + "\"";
  const int k = src.indexOf(pat);
  if (k < 0) return String();
  const int open = src.indexOf('{', k);
  if (open < 0) return String();
  int depth = 0;
  for (int i = open; i < (int)src.length(); ++i) {
    if (src[i] == '{') ++depth;
    else if (src[i] == '}' && --depth == 0) return src.substring(open, i + 1);
  }
  return String();
}

// Seconds from now until an ISO-8601 instant, or -1 if it cannot be read.
// Written out rather than handed to mktime because the timestamp is UTC and the
// bridge's TZ is deliberately local — mktime would silently apply the offset.
static long secondsUntilIso(const String& iso) {
  int Y, Mo, D, H, Mi, S;
  if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &H, &Mi, &S) != 6) return -1;
  // Days from civil (Howard Hinnant's algorithm), which is exact and has no
  // lookup tables or leap-year special cases to get wrong.
  int y = Y; const int m = Mo;
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + D - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = (long)era * 146097 + (long)doe - 719468;
  const long epoch = days * 86400L + H * 3600L + Mi * 60L + S;
  const time_t now = time(nullptr);
  if (now < 1000000000L) return -1;            // clock not set yet
  return epoch - (long)now;
}

static void sendNowPlaying(bool playing, uint16_t durS, uint16_t progS,
                           const String& title, const String& artist,
                           const String& album) {
  uint8_t p[proto::MAX_PAYLOAD];
  p[0] = playing ? 1 : 0;
  p[1] = (uint8_t)(durS & 0xFF);  p[2] = (uint8_t)(durS >> 8);
  p[3] = (uint8_t)(progS & 0xFF); p[4] = (uint8_t)(progS >> 8);
  const uint8_t tl = (uint8_t)(title.length()  > 47 ? 47 : title.length());
  const uint8_t al = (uint8_t)(artist.length() > 39 ? 39 : artist.length());
  p[5] = tl; p[6] = al;
  uint16_t n = 7;
  for (uint8_t i = 0; i < tl; ++i) p[n++] = (uint8_t)title[i];
  for (uint8_t i = 0; i < al; ++i) p[n++] = (uint8_t)artist[i];
  for (uint16_t i = 0; i < album.length() && n < proto::MAX_PAYLOAD; ++i)
    p[n++] = (uint8_t)album[i];
  sendFrame(proto::Msg::SetNowPlaying, p, (uint8_t)n);
}

static void sendGauges(const uint8_t* pct, const char* const* labels, uint8_t n,
                       const String& footer) {
  uint8_t p[proto::MAX_PAYLOAD];
  p[0] = n;
  uint16_t at = 1;
  for (uint8_t i = 0; i < n; ++i) {
    const uint8_t ll = (uint8_t)strlen(labels[i]);
    p[at++] = pct[i]; p[at++] = ll;
    for (uint8_t j = 0; j < ll; ++j) p[at++] = (uint8_t)labels[i][j];
  }
  for (uint16_t i = 0; i < footer.length() && at < proto::MAX_PAYLOAD; ++i)
    p[at++] = (uint8_t)footer[i];
  sendFrame(proto::Msg::SetGauges, p, (uint8_t)at);
}

// Accepts either the JSON that HA sends or a bare line of text, so the topic is
// useful from a shell as well as from a notify service.
static void applyNotify(const String& msg, uint16_t defaultMs) {
  String title, body, where, soloStr;
  long ms = defaultMs;
  if (msg.startsWith("{")) {
    title = jsonField(msg, "title");
    body  = jsonField(msg, "message");
    if (!body.length()) body = jsonField(msg, "body");
    where = jsonField(msg, "place");
    soloStr = jsonField(msg, "solo");
    const String m = jsonField(msg, "ms");
    if (m.length()) ms = m.toInt();
  } else {
    body = msg;
  }
  // jsonField returns "" for a bare true/false, so look for the literal too.
  const bool solo = soloStr.startsWith("t") || soloStr == "1"
                 || msg.indexOf("\"solo\":true") >= 0;
  uint8_t place = 0;
  if (where.equalsIgnoreCase("top")) place = 1;
  else if (where.startsWith("cent") || where.startsWith("Cent")) place = 2;
  if (ms < 0) ms = 0;
  if (ms > 60000) ms = 60000;
  sendNotify(title, body, place, solo, (uint16_t)ms);
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
  // Template primitives: resolved against the RTC on the device every refresh,
  // so a face defined here keeps running with the bridge unplugged.
  void clock(int16_t x, int16_t y, int16_t scale, const char* fmt) {
    uint8_t sl = 0; while (fmt[sl]) ++sl;
    if (!room((uint8_t)(8 + sl))) return;
    buf[len++] = 0x04; put16(x); put16(y); put16(scale); buf[len++] = sl;
    for (uint8_t i = 0; i < sl; ++i) buf[len++] = (uint8_t)fmt[i];
    buf[0] = ++count;
  }
  void hand(int16_t cx, int16_t cy, int16_t r0, int16_t r1, uint8_t src) {
    if (!room(10)) return;
    buf[len++] = 0x05; put16(cx); put16(cy); put16(r0); put16(r1);
    buf[len++] = src;
    buf[0] = ++count;
  }

  void send() const { sendFrame(proto::Msg::PushList, buf, len); }
};

// A scene bigger than one frame, staged on the device.
//
// Same byte stream a PushList carries, just split up: Begin, as many Chunks as
// it takes, Commit. The device decodes the result with the same parser, so
// there is no second format and no second set of bounds checks.
//
// Paced deliberately. The device drains its link once per refresh, and its USB
// TX buffer is small — firing chunks flat out overruns it and they are dropped
// rather than queued, which shows up as a scene that will not commit.
// Chunks are well under MAX_PAYLOAD on purpose. HWCDC's TX ring defaults to
// 256 bytes, so a 244-byte frame very nearly fills it in one call; when the
// device has not drained it yet the tail is dropped rather than queued, and the
// frame simply never arrives. Every frame that had ever worked until now was
// tiny, which is why this only showed up with artwork.
constexpr uint8_t kChunk = 48;

static void sendStaged(const uint8_t* p, uint16_t len) {
  sendFrame(proto::Msg::PushBegin, nullptr, 0);
  uint16_t at = 0;
  while (at < len) {
    const uint8_t n = (uint16_t)(len - at) > kChunk
                    ? kChunk : (uint8_t)(len - at);
    sendFrame(proto::Msg::PushChunk, p + at, n);
    at += n;
    // Below the drain rate: ~52 bytes a frame against the ~64 the device takes.
    delay(30);
  }
  sendFrame(proto::Msg::PushCommit, nullptr, 0);
}

// Grows past MAX_PAYLOAD, so it is staged rather than sent in one frame.
struct BigList {
  uint8_t buf[2048];
  uint16_t len = 1;
  uint8_t  count = 0;
  BigList() { buf[0] = 0; }
  bool room(uint16_t n) const { return len + n <= sizeof(buf) && count < 192; }
  void put16(int16_t v) { buf[len++] = (uint8_t)(v & 0xFF); buf[len++] = (uint8_t)((v >> 8) & 0xFF); }
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
  void text(int16_t x, int16_t y, int16_t scale, const char* s) {
    uint8_t sl = 0; while (s[sl]) ++sl;
    if (!room((uint16_t)(8 + sl))) return;
    buf[len++] = 0x01; put16(x); put16(y); put16(scale); buf[len++] = sl;
    for (uint8_t i = 0; i < sl; ++i) buf[len++] = (uint8_t)s[i];
    buf[0] = ++count;
  }
  void clock(int16_t x, int16_t y, int16_t scale, const char* fmt) {
    uint8_t sl = 0; while (fmt[sl]) ++sl;
    if (!room((uint16_t)(8 + sl))) return;
    buf[len++] = 0x04; put16(x); put16(y); put16(scale); buf[len++] = sl;
    for (uint8_t i = 0; i < sl; ++i) buf[len++] = (uint8_t)fmt[i];
    buf[0] = ++count;
  }
  void hand(int16_t cx, int16_t cy, int16_t r0, int16_t r1, uint8_t src) {
    if (!room(10)) return;
    buf[len++] = 0x05; put16(cx); put16(cy); put16(r0); put16(r1);
    buf[len++] = src;
    buf[0] = ++count;
  }
  void send() const { sendStaged(buf, len); }
};

// ---- MQTT: where the smarts live -------------------------------------------
// Everything above this point is mechanism; this is the policy layer. It is on
// the bridge and not the display MCU on purpose — rule 5, keep the radio off
// the beam.

static bool rxHello = false;      // device (re)introduced itself
static bool statusFresh = true;   // force a full republish after (re)connect
static proto::StatusPayload lastStatus{};   // newest telemetry, for the web UI
static bool haveStatus = false;
static void onFrame(uint8_t id, const uint8_t* p, uint8_t len);

static WiFiClient   net;
static PubSubClient mqtt(net);

// Must match faces.cpp on the device, in order — Status reports an index and
// this is what turns it back into a name.
//
// The group is a column of the same table rather than a second table, because
// the families it names are real: the knob on the clock walks between them and
// the button walks within one. Keeping them side by side means adding a face is
// still one line in one place, and a group cannot drift out of step with the
// name it belongs to. Order IS the wire id — append, never insert.
struct FaceEntry { const char* name; const char* group; };
static const FaceEntry kFaces[] = {
  {"hands","Analog"}, {"numbers","Analog"}, {"tickdial","Analog"},
  {"orbit","Analog"}, {"sector","Analog"},
  {"digital","Digital"}, {"datetime","Digital"}, {"wordclock","Digital"},
  {"binary","Digital"},
  {"tetra","Solids"}, {"cube","Solids"}, {"octa","Solids"}, {"icosa","Solids"},
  {"dodeca","Solids"}, {"tesseract","Solids"}, {"torus","Solids"},
  {"lissajous","Curves"}, {"harmonograph","Curves"}, {"spirograph","Curves"},
  {"rose","Curves"}, {"lorenz","Curves"}, {"starpoly","Curves"},
  {"starfield","Motion"}, {"tunnel","Motion"},
  {"midiscope","MIDI"}, {"midichord","MIDI"},
  {"matrix","Effects"},
  {"nowplaying","Data"}, {"gauges","Data"},
  {"teapot","Wireframes"}, {"sphere","Wireframes"}, {"knot","Wireframes"},
  {"mobius","Wireframes"}, {"helix","Wireframes"},
  {"atom","Science"},
  {"solar","Sky"}, {"moon","Sky"}, {"weather","Sky"},
  {"pong","Games"}, {"life","Games"},
  {"trailclock","Extra"}, {"ticker","Extra"}, {"worldclock","Extra"},
  {"asteroids","Arcade"},
};
// Everything downstream still wants a plain name by index.
static const char* faceName(uint8_t i) { return kFaces[i].name; }
constexpr uint8_t kFaceCount = sizeof(kFaces) / sizeof(kFaces[0]);
static uint8_t  curFace       = 0;

// Per-face render scale, percent. The device holds the live copy and applies it;
// the bridge owns the persistence, because the Teensy has nowhere to keep it and
// the bridge already has NVS for config. Saved lazily: the knob emits one of
// these per detent and NVS is flash.
static uint8_t  faceScale[32];
static bool     scaleDirty = false;
static uint32_t scaleSaveAt = 0;

// Must match DeviceState::kDefaultScale, which is what the device uses until
// this table reaches it.
constexpr uint8_t kDefaultScale = 70;

static void scaleLoad() {
  for (uint8_t i = 0; i < 32; ++i) faceScale[i] = kDefaultScale;
  prefs.begin("scopeclock", true);
  // Pre-filled above, so an absent key simply leaves the defaults in place.
  prefs.getBytes("fscale", faceScale, sizeof faceScale);
  prefs.end();
  for (uint8_t i = 0; i < 32; ++i)
    if (faceScale[i] < 40 || faceScale[i] > 250) faceScale[i] = kDefaultScale;
}
static void scaleSaveSoon() { scaleDirty = true; scaleSaveAt = millis() + 2000; }
static void scaleSaveTick() {
  if (!scaleDirty || (int32_t)(millis() - scaleSaveAt) < 0) return;
  scaleDirty = false;
  prefs.begin("scopeclock", false);
  prefs.putBytes("fscale", faceScale, sizeof faceScale);
  prefs.end();
}

// The whole table in one frame: 27 faces plus a count is well inside a payload,
// and sending it whole means there is no partial state to reason about.
// Located by name, so appending or reordering faces cannot silently point this
// at the wrong one.
static uint8_t npFace() {
  for (uint8_t i = 0; i < kFaceCount; ++i)
    if (!strcmp(kFaces[i].name, "nowplaying")) return i;
  return kFaceCount;
}
// Auto-show policy for the now-playing face.
//
// The first version switched whenever a track message arrived and the device
// was on a local face — which meant every update yanked the screen back, and
// navigating away while music played was impossible. Two things fix that: only
// act on a TRANSITION (playback starting, or the song changing), and treat any
// deliberate face choice as an override that holds until the music stops.
static bool autoNowPlaying = true;    // master switch, persisted
static bool wobbleOn = true;          // anti-burn-in drift, persisted
static uint8_t atomZ = 0;             // atom face: 0 cycles, 1..118 pins

// Words the weather services actually use, mapped onto the seven shapes the
// display can draw distinctly. Order matters: "partly cloudy" must be tested
// before "cloudy", or it matches the wrong one.
static uint8_t skyCode(const String& in) {
  String t = in; t.toLowerCase();
  if (t.indexOf("thunder") >= 0 || t.indexOf("storm") >= 0)   return 5;
  if (t.indexOf("snow") >= 0 || t.indexOf("sleet") >= 0)      return 4;
  if (t.indexOf("rain") >= 0 || t.indexOf("shower") >= 0 ||
      t.indexOf("drizzl") >= 0)                               return 3;
  if (t.indexOf("fog") >= 0 || t.indexOf("mist") >= 0 ||
      t.indexOf("haze") >= 0)                                 return 6;
  if (t.indexOf("part") >= 0 || t.indexOf("few") >= 0)        return 1;
  if (t.indexOf("cloud") >= 0 || t.indexOf("overcast") >= 0)  return 2;
  if (t.indexOf("clear") >= 0 || t.indexOf("sun") >= 0)       return 0;
  return 2;
}

static void sendWeather(int16_t t10, uint8_t sky, const String& place, const String& detail) {
  uint8_t p[proto::MAX_PAYLOAD];
  p[0] = (uint8_t)(t10 & 0xFF); p[1] = (uint8_t)(t10 >> 8); p[2] = sky;
  const uint8_t pl = (uint8_t)(place.length() > 23 ? 23 : place.length());
  p[3] = pl;
  uint16_t n = 4;
  for (uint8_t i = 0; i < pl; ++i) p[n++] = (uint8_t)place[i];
  for (uint16_t i = 0; i < detail.length() && n < proto::MAX_PAYLOAD; ++i)
    p[n++] = (uint8_t)detail[i];
  sendFrame(proto::Msg::SetWeather, p, (uint8_t)n);
}

// This bridge's own offset from UTC, in minutes, right now — which is where DST
// enters and then leaves the story. The device is told deltas relative to LOCAL
// time, so it never learns that timezones exist (hard rule 4).
static int localUtcOffsetMin() {
  const time_t now = time(nullptr);
  if (now < 1000000000L) return 0;
  struct tm lt, gt;
  localtime_r(&now, &lt);
  gmtime_r(&now, &gt);
  int d = (lt.tm_hour - gt.tm_hour) * 60 + (lt.tm_min - gt.tm_min);
  const int dd = lt.tm_yday - gt.tm_yday;
  // Across a date boundary the day numbers differ by one, or by a year's worth
  // on 31 December, which is the case that catches people out.
  if (dd == 1 || dd < -1) d += 1440;
  else if (dd == -1 || dd > 1) d -= 1440;
  return d;
}

// The zone list as the host gave it, kept so it can be re-sent: an offset that
// was right in June is wrong in December, and the device has no way to know.
static String zonesJson;

static void sendZones() {
  if (!zonesJson.length()) return;
  // Refuse to send before the clock is set. Sending anyway means every delta is
  // measured against an offset of zero, which is exactly the raw UTC offset —
  // Auckland showed 22:40 instead of 12:41, ten hours out, which is Melbourne's
  // own offset staring back. Better to send nothing until it can be right.
  if (time(nullptr) < 1000000000L) return;
  const int local = localUtcOffsetMin();
  uint8_t p[proto::MAX_PAYLOAD];
  uint16_t at = 1;
  uint8_t n = 0;
  int from = 0;
  while (n < 5) {
    const int lb = zonesJson.indexOf("\"label\"", from);
    if (lb < 0) break;
    const int ob = zonesJson.indexOf("\"offset\"", lb);
    if (ob < 0) break;
    const String label = jsonField(zonesJson.substring(lb), "label");
    const int off = jsonField(zonesJson.substring(ob), "offset").toInt();
    const int16_t delta = (int16_t)(off - local);
    const uint8_t ll = (uint8_t)(label.length() > 13 ? 13 : label.length());
    if (at + 3 + ll > proto::MAX_PAYLOAD) break;
    p[at++] = (uint8_t)(delta & 0xFF);
    p[at++] = (uint8_t)((delta >> 8) & 0xFF);
    p[at++] = ll;
    for (uint8_t i = 0; i < ll; ++i) p[at++] = (uint8_t)label[i];
    ++n;
    from = ob + 8;
  }
  p[0] = n;
  if (n) sendFrame(proto::Msg::SetZones, p, (uint8_t)at);
}

static void sendTicker(const String& text) {
  uint8_t p[proto::MAX_PAYLOAD];
  uint16_t n = 0;
  for (uint16_t i = 0; i < text.length() && n < proto::MAX_PAYLOAD; ++i)
    p[n++] = (uint8_t)text[i];
  sendFrame(proto::Msg::SetTicker, p, (uint8_t)n);
}

static void sendElement() {
  const uint8_t p[1] = { atomZ };
  sendFrame(proto::Msg::SetElement, p, 1);
}

static void sendWobble() {
  const uint8_t p[1] = { (uint8_t)(wobbleOn ? 1 : 0) };
  sendFrame(proto::Msg::SetWobble, p, 1);
}
static bool npWasPlaying   = false;
static String npLastSong;
static bool npOverridden   = false;   // user picked something else; leave them be
static int  npExpectFace   = -1;      // a switch we made, echoing back in Status

// Called wherever a face is chosen on purpose — web, MQTT, or the knob.
static void faceChosenByUser(uint8_t f) {
  if (npExpectFace >= 0 && f == (uint8_t)npExpectFace) { npExpectFace = -1; return; }
  // Only an override if there is something to override. Choosing a face while
  // nothing is playing is just choosing a face, and must not stop the next
  // track from showing itself — which is what it did.
  if (npWasPlaying) npOverridden = true;
}

static void sendScales() {
  uint8_t p[proto::MAX_PAYLOAD];
  const uint8_t n = kFaceCount < 32 ? kFaceCount : 32;
  p[0] = n;
  for (uint8_t i = 0; i < n; ++i) p[1 + i] = faceScale[i];
  sendFrame(proto::Msg::SetScales, p, (uint8_t)(n + 1));
}
static uint8_t  curBrightness = 255;
static uint16_t bannerMs      = 8000;

static void pushScene(const String& msg);

static String topic(const char* leaf) { return cfg.mqttPrefix + "/" + leaf; }

// Parse one scene line into the builder. The text form exists so a scene can be
// written by hand or by an HA template without anyone encoding binary:
//   T <x> <y> <scale> <text to end of line>
//   L <x0> <y0> <x1> <y1>
//   C <cx> <cy> <r>
//   D <x> <y> <scale> <strftime-ish format>     live from the RTC
//   H <cx> <cy> <r0> <r1> <0|1|2>               hand: sec / min / hour
//
// D and H make the scene a face template: the device re-resolves them every
// refresh, so it keeps telling the time even with the bridge gone.
template <typename B>
static void sceneLine(B& b, const String& ln) {
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
    if ((kind == 'T' || kind == 'D') && n == 3) break;  // rest is the string
  }
  if ((kind == 'T' || kind == 'D') && n >= 3) {
    while (i < (int)ln.length() && ln[i] == ' ') ++i;
    const String body = ln.substring(i);
    if (kind == 'T') b.text ((int16_t)v[0], (int16_t)v[1], (int16_t)v[2], body.c_str());
    else             b.clock((int16_t)v[0], (int16_t)v[1], (int16_t)v[2], body.c_str());
  } else if (kind == 'H' && n >= 4) {
    // the 5th field is the source; re-scan it since the loop caps at 4
    int src = 0;
    const int sp = ln.lastIndexOf(' ');
    if (sp > 0) src = ln.substring(sp + 1).toInt();
    b.hand((int16_t)v[0], (int16_t)v[1], (int16_t)v[2], (int16_t)v[3], (uint8_t)(src & 3));
  } else if (kind == 'L' && n >= 4) {
    b.line((int16_t)v[0], (int16_t)v[1], (int16_t)v[2], (int16_t)v[3]);
  } else if (kind == 'C' && n >= 3) {
    b.circle((int16_t)v[0], (int16_t)v[1], (int16_t)v[2]);
  }
}

static void publishState() {
  mqtt.publish(topic("face/state").c_str(), faceName(curFace), true);
  char b[8]; snprintf(b, sizeof b, "%u", curBrightness);
  mqtt.publish(topic("brightness/state").c_str(), b, true);
  mqtt.publish(topic("wobble/state").c_str(), wobbleOn ? "ON" : "OFF", true);
}

static void onMqtt(char* t, uint8_t* payload, unsigned int len) {
  String msg;
  msg.reserve(len);
  for (unsigned i = 0; i < len; ++i) msg += (char)payload[i];
  const String tp(t);

  if (tp == cfg.npTopic) {
    // Spotify-shaped: name / album / artist / is_playing / duration_ms /
    // progress_ms. Sent on change only — the device runs the progress ring
    // itself, so a track update is a few hundred bytes a song, not per second.
    const bool playing = msg.indexOf("\"is_playing\":true") >= 0
                      || msg.indexOf("\"is_playing\": true") >= 0;
    const long dur  = jsonField(msg, "duration_ms").toInt();
    const long prog = jsonField(msg, "progress_ms").toInt();
    sendNowPlaying(playing, (uint16_t)(dur / 1000), (uint16_t)(prog / 1000),
                   jsonField(msg, "name"), jsonField(msg, "artist"),
                   jsonField(msg, "album"));
    const String song = jsonField(msg, "song_id");
    const bool started = playing && !npWasPlaying;
    const bool changed = playing && song.length() && song != npLastSong;
    if (!playing) npOverridden = false;   // a fresh start earns the screen again
    npWasPlaying = playing;
    if (song.length()) npLastSong = song;

    if (autoNowPlaying && (started || changed) && !npOverridden
        && npFace() < kFaceCount
        && (!haveStatus || lastStatus.mode == 0)) {
      // A pushed scene is a more recent decision than any default, so it stays.
      curFace = npFace();
      npExpectFace = (int)curFace;
      sendSetMode(0, curFace);
    }
  } else if (tp == cfg.gaugeTopic) {
    // Three utilisation figures, each in its own nested object, plus a countdown
    // to whichever window resets first.
    const String h5 = jsonObject(msg, "five_hour");
    const String d7 = jsonObject(msg, "seven_day");
    const String ex = jsonObject(msg, "extra_usage");
    auto pctOf = [](const String& o) -> uint8_t {
      const float v = jsonField(o, "utilization").toFloat();
      const long r = lroundf(v < 0 ? 0 : (v > 100 ? 100 : v));
      return (uint8_t)r;
    };
    const uint8_t pct[3] = { pctOf(h5), pctOf(d7), pctOf(ex) };
    static const char* const labels[3] = { "5H", "7D", "EXTRA" };

    String footer;
    const long secs = secondsUntilIso(jsonField(h5, "resets_at"));
    if (secs > 0) {
      const long m = secs / 60;
      footer = m >= 60 ? String("5H RESETS IN ") + (m / 60) + "h" + (m % 60) + "m"
                       : String("5H RESETS IN ") + m + "m";
    }
    const long used = jsonField(ex, "used_credits").toInt();
    if (used > 0) {
      if (footer.length()) footer += "  ";
      footer += String(used) + " CR";
    }
    sendGauges(pct, labels, 3, footer);
  } else if (tp == topic("notify/set")) {
    applyNotify(msg, bannerMs);
  } else if (tp == topic("weather/set")) {
    // JSON from an HA automation, or "21.5 rain MELBOURNE" from a shell.
    if (msg.startsWith("{")) {
      const float t = jsonField(msg, "temp").toFloat();
      sendWeather((int16_t)lroundf(t * 10), skyCode(jsonField(msg, "condition")),
                  jsonField(msg, "place"), jsonField(msg, "detail"));
    } else {
      const int a = msg.indexOf(' '), b = msg.indexOf(' ', a + 1);
      if (a > 0) sendWeather((int16_t)lroundf(msg.substring(0, a).toFloat() * 10),
                             skyCode(b > 0 ? msg.substring(a + 1, b) : msg.substring(a + 1)),
                             b > 0 ? msg.substring(b + 1) : String(), String());
    }
  } else if (tp == topic("zones/set")) {
    // {"zones":[{"label":"LONDON","offset":60},...]}  offset is minutes from UTC.
    zonesJson = msg;
    prefs.begin("scopeclock", false); prefs.putString("zones", zonesJson); prefs.end();
    sendZones();
  } else if (tp == topic("ticker/set")) {
    sendTicker(msg);
  } else if (tp == topic("element/set")) {
    // 0, "cycle" or anything unparseable means walk the table.
    const long v = msg.equalsIgnoreCase("cycle") ? 0 : msg.toInt();
    atomZ = (uint8_t)(v >= 1 && v <= 118 ? v : 0);
    prefs.begin("scopeclock", false); prefs.putUChar("atomz", atomZ); prefs.end();
    sendElement();
    mqtt.publish(topic("element/state").c_str(),
                 atomZ ? String(atomZ).c_str() : "0", true);
  } else if (tp == topic("wobble/set")) {
    wobbleOn = !(msg.equalsIgnoreCase("off") || msg == "0" || msg.equalsIgnoreCase("false"));
    prefs.begin("scopeclock", false); prefs.putUChar("wobble", wobbleOn ? 1 : 0); prefs.end();
    sendWobble();
    mqtt.publish(topic("wobble/state").c_str(), wobbleOn ? "ON" : "OFF", true);
  } else if (tp == topic("banner/set")) {
    sendBanner(msg.c_str(), bannerMs);
  } else if (tp == topic("banner/duration")) {
    const long v = msg.toInt();
    if (v > 0 && v <= 60000) bannerMs = (uint16_t)v;
  } else if (tp == topic("face/set")) {
    for (uint8_t i = 0; i < kFaceCount; ++i) {
      if (msg.equalsIgnoreCase(faceName(i))) { curFace = i; faceChosenByUser(i); break; }
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
    pushScene(msg);
  }
}

// Shared by MQTT and the web API.
static void pushScene(const String& msg) {
  {
    // Empty payload means "give the clock back to its own face".
    if (msg.length() == 0) { sendSetMode(0, curFace); return; }
    // Staged, so a traced drawing is not capped at the ~34 items a single
    // frame holds. Small scenes cost one extra round trip; big ones become
    // possible at all.
    BigList b;
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
  const String dev = String("\"dev\":{\"ids\":[\"") + cfg.mqttPrefix + "\"],\"name\":\"Scope Clock\","
                   + "\"mf\":\"Cathode Corner\",\"mdl\":\"SCTV\"}";
  String j;

  // Built from the face table rather than typed out again: a second copy of the
  // list is a second thing to forget when a face is added, and the symptom
  // (a face the knob can select but the dropdown cannot) is a confusing one.
  String opts;
  for (uint8_t i = 0; i < kFaceCount; ++i) {
    if (i) opts += ',';
    opts += '"'; opts += faceName(i); opts += '"';
  }
  j = String("{\"name\":\"Face\",\"uniq_id\":\"" MQTT_PREFIX "_face\",")
      + "\"cmd_t\":\"" + topic("face/set") + "\",\"stat_t\":\"" + topic("face/state") + "\","
      + "\"options\":[" + opts + "],"
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish((String("homeassistant/select/") + cfg.mqttPrefix + "/face/config").c_str(), j.c_str(), true);

  j = String("{\"name\":\"World clock zones\",\"uniq_id\":\"" MQTT_PREFIX "_zones\",")
      + "\"cmd_t\":\"" + topic("zones/set") + "\",\"ic\":\"mdi:earth\","
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish((String("homeassistant/notify/") + cfg.mqttPrefix + "/zones/config").c_str(), j.c_str(), true);

  j = String("{\"name\":\"Ticker\",\"uniq_id\":\"" MQTT_PREFIX "_ticker\",")
      + "\"cmd_t\":\"" + topic("ticker/set") + "\",\"ic\":\"mdi:text-long\","
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish((String("homeassistant/notify/") + cfg.mqttPrefix + "/ticker/config").c_str(), j.c_str(), true);

  j = String("{\"name\":\"Element\",\"uniq_id\":\"" MQTT_PREFIX "_elem\",")
      + "\"cmd_t\":\"" + topic("element/set") + "\",\"stat_t\":\"" + topic("element/state") + "\","
      + "\"min\":0,\"max\":118,\"step\":1,\"mode\":\"box\",\"ic\":\"mdi:atom\","
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish((String("homeassistant/number/") + cfg.mqttPrefix + "/element/config").c_str(), j.c_str(), true);

  j = String("{\"name\":\"Anti burn-in drift\",\"uniq_id\":\"" MQTT_PREFIX "_wobble\",")
      + "\"cmd_t\":\"" + topic("wobble/set") + "\",\"stat_t\":\"" + topic("wobble/state") + "\","
      + "\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"ic\":\"mdi:television-shimmer\","
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish((String("homeassistant/switch/") + cfg.mqttPrefix + "/wobble/config").c_str(), j.c_str(), true);

  j = String("{\"name\":\"Brightness\",\"uniq_id\":\"" MQTT_PREFIX "_bri\",")
      + "\"cmd_t\":\"" + topic("brightness/set") + "\",\"stat_t\":\"" + topic("brightness/state") + "\","
      + "\"min\":0,\"max\":255,\"mode\":\"slider\","
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish((String("homeassistant/number/") + cfg.mqttPrefix + "/brightness/config").c_str(), j.c_str(), true);

  j = String("{\"name\":\"Banner\",\"uniq_id\":\"" MQTT_PREFIX "_banner\",")
      + "\"cmd_t\":\"" + topic("banner/set") + "\","
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish((String("homeassistant/notify/") + cfg.mqttPrefix + "/banner/config").c_str(), j.c_str(), true);

  // The command template is what makes the title survive: HA's notify service
  // takes title and message separately, and without this only the message is
  // sent. Placement and duration come from the service call's data block.
  j = String("{\"name\":\"Notification\",\"uniq_id\":\"" MQTT_PREFIX "_notify\",")
      + "\"cmd_t\":\"" + topic("notify/set") + "\","
      + "\"cmd_tpl\":\"{\\\"title\\\":\\\"{{ title|default('') }}\\\","
      + "\\\"message\\\":\\\"{{ message }}\\\","
      + "\\\"place\\\":\\\"{{ data.place|default('bottom') }}\\\","
      + "\\\"solo\\\":{{ data.solo|default(false)|lower }},"
      + "\\\"ms\\\":{{ data.ms|default(8000) }}}\","
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish((String("homeassistant/notify/") + cfg.mqttPrefix + "/notify/config").c_str(), j.c_str(), true);

  // Diagnostics, from the device's own Status frames.
  struct Sen { const char* id; const char* name; const char* leaf;
               const char* unit; const char* ic; };
  static const Sen sens[] = {
    {"uptime",  "Uptime",     "uptime/state",  "s",  "mdi:timer-outline"},
    {"frame",   "Frame time", "frame/state",   "us", "mdi:speedometer"},
    {"timeset", "Time synced","timeset/state", "s",  "mdi:clock-check-outline"},
  };
  for (const Sen& sn : sens) {
    j = String("{\"name\":\"") + sn.name + "\",\"uniq_id\":\"" MQTT_PREFIX "_" + sn.id + "\","
        + "\"stat_t\":\"" + topic(sn.leaf) + "\",\"unit_of_meas\":\"" + sn.unit + "\","
        + "\"ic\":\"" + sn.ic + "\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\","
        + "\"avty_t\":\"" + avail + "\"," + dev + "}";
    mqtt.publish((String("homeassistant/sensor/") + cfg.mqttPrefix + "/" + sn.id + "/config").c_str(),
                 j.c_str(), true);
  }

  j = String("{\"name\":\"RTC\",\"uniq_id\":\"" MQTT_PREFIX "_rtc\",")
      + "\"stat_t\":\"" + topic("rtc/state") + "\",\"dev_cla\":\"problem\","
      + "\"pl_on\":\"OFF\",\"pl_off\":\"ON\",\"ent_cat\":\"diagnostic\","
      + "\"avty_t\":\"" + avail + "\"," + dev + "}";
  mqtt.publish((String("homeassistant/binary_sensor/") + cfg.mqttPrefix + "/rtc/config").c_str(), j.c_str(), true);

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
    j = String("{\"automation_type\":\"trigger\",\"type\":\"") + tg.type + "\","
        + "\"subtype\":\"" + tg.sub + "\",\"topic\":\"" + topic(tg.leaf) + "\","
        + "\"payload\":\"" + tg.payload + "\"," + dev + "}";
    mqtt.publish((String("homeassistant/device_automation/") + cfg.mqttPrefix + "/" + tg.id + "/config").c_str(),
                 j.c_str(), true);
  }
}

static void mqttConnect() {
  if (!cfg.mqttHost.length()) return;             // no broker configured
  static uint32_t lastTry = 0;
  if (mqtt.connected() || millis() - lastTry < 5000) return;
  lastTry = millis();

  const String avail = topic("availability");
  // Last will, so HA marks the clock unavailable if the bridge drops off
  // rather than showing stale controls that silently do nothing.
  const bool ok = cfg.mqttUser.length()
    ? mqtt.connect(cfg.mqttPrefix.c_str(), cfg.mqttUser.c_str(), cfg.mqttPass.c_str(),
                   avail.c_str(), 0, true, "offline")
    : mqtt.connect(cfg.mqttPrefix.c_str(), avail.c_str(), 0, true, "offline");
  if (!ok) return;

  mqtt.publish(avail.c_str(), "online", true);
  publishDiscovery();
  publishState();
  statusFresh = true;                 // republish everything on this connection
  if (cfg.npTopic.length())    mqtt.subscribe(cfg.npTopic.c_str());
  if (cfg.gaugeTopic.length()) mqtt.subscribe(cfg.gaugeTopic.c_str());
  mqtt.subscribe(topic("zones/set").c_str());
  mqtt.subscribe(topic("weather/set").c_str());
  mqtt.subscribe(topic("ticker/set").c_str());
  mqtt.subscribe(topic("element/set").c_str());
  mqtt.subscribe(topic("wobble/set").c_str());
  mqtt.subscribe(topic("notify/set").c_str());
  mqtt.subscribe(topic("banner/set").c_str());
  mqtt.subscribe(topic("banner/duration").c_str());
  mqtt.subscribe(topic("face/set").c_str());
  mqtt.subscribe(topic("brightness/set").c_str());
  mqtt.subscribe(topic("scene/set").c_str());
}

// Reset the USB Serial/JTAG peripheral itself.
//
// This is the third thing tried against the one-way wedge, and the only one
// that touches the part the evidence actually implicates. The others reset
// everything around it and failed: USBHost::begin() on the device already
// asserts USBHS_USBCMD_RST and re-inits the PHY, so a Teensy reboot is a full
// host-controller reset and did not clear it; an ESP32 soft reset leaves the
// peripheral's state untouched; and dropping the D+ pull-up only changes what
// is on the wire, which detached the device but never re-enumerated and took
// the working uplink down with it.
//
// Asserting SYSTEM_USB_DEVICE_RST returns the block's registers and state
// machine to reset — which is what removing power does to it, and power is the
// one thing known to work. The driver is torn down first and rebuilt after,
// because begin() is what reconfigures the PHY and re-raises the pull-up.
static uint32_t lastResetMs = 0;

static void usbPeripheralReset() {
  lastResetMs = millis();
  TO_DISPLAY.end();
  delay(20);
  static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&mux);                 // shared register, so atomically
  REG_SET_BIT(SYSTEM_PERIP_RST_EN1_REG, SYSTEM_USB_DEVICE_RST);
  REG_CLR_BIT(SYSTEM_PERIP_RST_EN1_REG, SYSTEM_USB_DEVICE_RST);
  portEXIT_CRITICAL(&mux);
  delay(80);
  TO_DISPLAY.begin(115200);
}

// The device reports how long it has been deaf; we are still hearing it, so the
// report arrives even when nothing we send does. That is the only way to see
// this fault: from here, our own writes still report success.
static uint16_t lastSilent = 0;
static bool     autoRecover = true;    // proven: peripheral reset + device restart

static void checkLink(uint16_t silentS, uint32_t deviceUpS) {
  lastSilent = silentS;
  if (!autoRecover) return;
  // 0xFFFF is "has never heard anything", which after a restart is the WORST
  // case, not an exempt one — treating it as healthy is why the pair could
  // restart once and then sit deaf forever, each side waiting on the other.
  const bool deaf = (silentS == 0xFFFF) ? (deviceUpS > 30) : (silentS >= 30);
  if (!deaf) return;
  if (millis() < 30000) return;                    // ignore our own boot churn
  if (millis() - lastResetMs < 60000) return;      // one attempt a minute
  usbPeripheralReset();
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
      faceChosenByUser(curFace);      // the knob counts as choosing
      mqtt.publish(topic("face/state").c_str(), faceName(curFace), true);
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
    mqtt.publish(topic("mode/state").c_str(),
                 s.mode == 2 ? "audio" : (s.mode == 1 ? "pushed" : "face"), true);

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
      // The device comes up with every face at 100 and the drift on; hand it
      // what was actually chosen before anyone sees otherwise.
      sendScales();
      sendWobble();
      sendElement();
      sendZones();
      break;
    case proto::Msg::Status: {
      if (len < sizeof(proto::StatusPayload)) break;
      proto::StatusPayload s;
      memcpy(&s, p, sizeof s);          // the payload need not be aligned
      lastStatus = s; haveStatus = true;
      checkLink(s.linkSilentS, s.uptimeS);
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
    case proto::Msg::EventScale:
      // The knob changed a face's size. The device is already showing it; all
      // this end has to do is remember.
      if (len >= 2 && p[0] < 32 && p[1] >= 40 && p[1] <= 250) {
        faceScale[p[0]] = p[1];
        scaleSaveSoon();
        mqtt.publish(topic("scale/state").c_str(), String(p[1]).c_str(), true);
      }
      break;
    default: break;
  }
}

// ---- web config -------------------------------------------------------------
// Enough to set a clock up somewhere you cannot build firmware. Saving writes
// NVS and reboots; clearing reverts to whatever was compiled in from .env.
//
// Guarded by the OTA password, because this page both reveals and sets network
// credentials. Existing secrets are never rendered back — a blank field means
// "leave it alone", so the page cannot be used to read them out.

static bool webAuthed() {
  if (!strlen(OTA_PASS)) return true;           // no password set, no gate
  if (web.authenticate("admin", OTA_PASS)) return true;
  web.requestAuthentication();
  return false;
}

static String field(const char* label, const char* name, const String& val, bool secret) {
  return String("<label>") + label + "<input name='" + name + "'"
       + (secret ? " type='password' placeholder='(unchanged)'"
                 : String(" value='") + val + "'")
       + "></label>";
}

// The page polls this; keep it small and allocation-light.
static void handleState() {
  const proto::StatusPayload& s = lastStatus;
  String j = "{";
  j += "\"face\":\"" + String(curFace < kFaceCount ? faceName(curFace) : "?") + "\",";
  j += "\"mode\":" + String(haveStatus ? s.mode : 0) + ",";
  j += "\"bri\":"  + String(curBrightness) + ",";
  j += "\"scale\":" + String(curFace < 32 ? faceScale[curFace] : kDefaultScale) + ",";
  j += "\"autonp\":" + String(autoNowPlaying ? 1 : 0) + ",";
  j += "\"wobble\":" + String(wobbleOn ? 1 : 0) + ",";
  j += "\"elem\":" + String(atomZ) + ",";
  // The bridge's own UTC offset, which is what every zone delta is measured
  // against. Exposed because a wrong world clock looks identical to a wrong
  // zone list from the outside.
  j += "\"tzoff\":" + String(localUtcOffsetMin()) + ",";
  j += "\"frame\":" + String(haveStatus ? s.frameUs : 0) + ",";
  j += "\"hz\":"    + String(haveStatus ? s.hz : 0) + ",";
  j += "\"rtc\":"   + String(haveStatus && s.rtcOk ? 1 : 0) + ",";
  // -1 rather than 65535: "never" is a state the UI should say out loud, not a
  // number it should print.
  j += "\"sync\":"  + String(!haveStatus || s.setAgeS == 0xFFFF ? -1 : (int)s.setAgeS) + ",";
  j += "\"up\":"    + String(haveStatus ? s.uptimeS : 0) + ",";
  j += "\"mqtt\":"  + String(mqtt.connected() ? 1 : 0) + ",";
  j += "\"rssi\":"  + String(WiFi.RSSI()) + ",";
  j += "\"ssid\":\"" + cfg.wifiSsid + "\",";
  j += "\"ip\":\""   + WiFi.localIP().toString() + "\",";
  j += "\"silent\":" + String(haveStatus ? (int)lastSilent : -1) + "}";
  web.send(200, "application/json", j);
}

// The face list, fetched once at page load. The page could carry its own copy,
// but then adding a face means editing three lists in two languages and the one
// you forget is the one nobody notices for a week.
static void handleFaces() {
  String j = "[";
  for (uint8_t i = 0; i < kFaceCount; ++i) {
    if (i) j += ',';
    j += "{\"n\":\""; j += kFaces[i].name;
    j += "\",\"g\":\""; j += kFaces[i].group; j += "\"}";
  }
  j += ']';
  web.send(200, "application/json", j);
}

// The API mirrors the MQTT topics exactly rather than inventing a second set of
// semantics — same handlers, same effects, so the two cannot drift apart.
static void handleApi() {
  const String uri = web.uri();
  // The page posts a raw body, which lands in arg("plain"). Anything sending
  // form-encoded instead (curl --data, most HTTP helpers) has its body parsed
  // into arg names, so fall back to the first of those rather than appearing
  // to accept the request and silently doing nothing.
  String body = web.arg("plain");
  if (!body.length() && web.args() > 0) body = web.argName(0);
  if (uri.endsWith("/face")) {
    for (uint8_t i = 0; i < kFaceCount; ++i)
      if (body.equalsIgnoreCase(faceName(i))) { curFace = i; faceChosenByUser(i); break; }
    sendSetMode(0, curFace);
  } else if (uri.endsWith("/brightness")) {
    const long v = body.toInt();
    curBrightness = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    const uint8_t p[1] = { curBrightness };
    sendFrame(proto::Msg::SetBrightness, p, 1);
  } else if (uri.endsWith("/ticker")) {
    sendTicker(body);
  } else if (uri.endsWith("/element")) {
    const long v = body.equalsIgnoreCase("cycle") ? 0 : body.toInt();
    atomZ = (uint8_t)(v >= 1 && v <= 118 ? v : 0);
    prefs.begin("scopeclock", false); prefs.putUChar("atomz", atomZ); prefs.end();
    sendElement();
    mqtt.publish(topic("element/state").c_str(),
                 atomZ ? String(atomZ).c_str() : "0", true);
  } else if (uri.endsWith("/wobble")) {
    wobbleOn = !(body == "0" || body.equalsIgnoreCase("off"));
    prefs.begin("scopeclock", false); prefs.putUChar("wobble", wobbleOn ? 1 : 0); prefs.end();
    sendWobble();
    mqtt.publish(topic("wobble/state").c_str(), wobbleOn ? "ON" : "OFF", true);
  } else if (uri.endsWith("/autonp")) {
    autoNowPlaying = !(body == "0" || body.equalsIgnoreCase("off"));
    prefs.begin("scopeclock", false);
    prefs.putUChar("autonp", autoNowPlaying ? 1 : 0);
    prefs.end();
  } else if (uri.endsWith("/scale")) {
    // Applies to whichever face is showing, which is the one the slider is for.
    long v = body.toInt();
    if (v < 40) v = 40;
    if (v > 250) v = 250;
    if (curFace < 32) { faceScale[curFace] = (uint8_t)v; scaleSaveSoon(); sendScales(); }
  } else if (uri.endsWith("/notify")) {
    applyNotify(body, bannerMs);
  } else if (uri.endsWith("/banner")) {
    sendBanner(body.c_str(), bannerMs);
  } else if (uri.endsWith("/scene")) {
    pushScene(body);
  } else if (uri.endsWith("/audio")) {
    // Mode 2 hands the clock's DACs to the USB audio stream on its front jack.
    // "off" is just the local face again — there is no separate teardown.
    const bool on = !(body == "0" || body.equalsIgnoreCase("off"));
    sendSetMode(on ? 2 : 0, curFace);
  } else if (uri.endsWith("/relink")) {
    usbPeripheralReset();
  }
  web.send(200, "text/plain", "ok");
}

static void handleRoot() {
  if (!webAuthed()) return;
  String h =
    // Same tokens as webui.h, so the two pages read as one product.
    "<!doctype html><html lang=en><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<meta name=color-scheme content=dark>"
    "<title>Settings \xe2\x80\x94 Scope Clock</title><style>"
    ":root{--bg:#0e100f;--card:#161a19;--raised:#1d2321;--line:#272e2c;"
    "--text:#e8ecea;--muted:#8d9994;--accent:#3ddc84}"
    "*{box-sizing:border-box}"
    "body{margin:0;padding:24px 18px 48px;background:var(--bg);color:var(--text);"
    "font:15px/1.5 system-ui,-apple-system,'Segoe UI',Roboto,sans-serif}"
    ".wrap{max-width:520px;margin:0 auto}"
    "h1{font-size:19px;font-weight:650;margin:0 0 4px;letter-spacing:-.01em}"
    "p.s{color:var(--muted);font-size:13px;margin:0 0 20px}"
    "fieldset{background:var(--card);border:1px solid var(--line);border-radius:10px;"
    "margin:0 0 14px;padding:6px 18px 18px}"
    "legend{padding:0 8px;font-size:12px;font-weight:600;text-transform:uppercase;"
    "letter-spacing:.07em;color:var(--muted)}"
    "label{display:block;margin:14px 0 0;font-size:13px;color:var(--muted)}"
    "input{width:100%;padding:9px 11px;margin-top:6px;background:var(--raised);"
    "color:var(--text);border:1px solid var(--line);border-radius:7px;font:inherit;font-size:14px}"
    "input:focus{outline:none;border-color:rgba(61,220,132,.45)}"
    "button{padding:9px 16px;border:1px solid rgba(61,220,132,.45);border-radius:7px;"
    "background:var(--raised);color:var(--accent);font:inherit;font-size:13.5px;cursor:pointer}"
    "button:hover{background:rgba(61,220,132,.10)}"
    "a{color:var(--muted);font-size:12.5px}a:hover{color:var(--accent)}"
    ".foot{margin-top:18px;display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap}"
    "</style><div class=wrap>"
    "<h1>Bridge settings</h1><p class=s>Saving reboots the bridge. "
    "Blank password fields keep their current value.</p><form method=POST action=/save>";
  h += "<fieldset><legend>Wi-Fi</legend>"
     + field("Network", "wifi_ssid", cfg.wifiSsid, false)
     + field("Password", "wifi_pass", "", true) + "</fieldset>";
  h += "<fieldset><legend>Time</legend>"
     + field("POSIX timezone", "tz", cfg.tz, false)
     + field("NTP server", "ntp", cfg.ntp, false) + "</fieldset>";
  h += "<fieldset><legend>MQTT</legend>"
     + field("Broker", "mq_host", cfg.mqttHost, false)
     + field("Port", "mq_port", cfg.mqttPort, false)
     + field("Username", "mq_user", cfg.mqttUser, false)
     + field("Password", "mq_pass", "", true)
     + field("Topic prefix", "mq_prefix", cfg.mqttPrefix, false) + "</fieldset>";
  h += "<fieldset><legend>Data faces</legend>"
     + field("Now-playing topic", "np_topic", cfg.npTopic, false)
     + field("Gauges topic", "gg_topic", cfg.gaugeTopic, false) + "</fieldset>";
  h += "<button>Save and reboot</button></form>"
       "<div class=foot><a href=/>\xe2\x86\x90 Back to the clock</a>"
       "<a href=/forget onclick=\"return confirm('Revert to the built-in settings?')\">"
       "Forget saved settings</a></div></div>";
  web.send(200, "text/html", h);
}

static void handleSave() {
  if (!webAuthed()) return;
  prefs.begin("scopeclock", false);
  static const char* keys[] = {"wifi_ssid","tz","ntp","mq_host","mq_port","mq_user","mq_prefix",
                               "np_topic","gg_topic"};
  for (const char* k : keys) if (web.hasArg(k)) prefs.putString(k, web.arg(k));
  // Secrets only when actually supplied, so a blank field is "keep".
  if (web.hasArg("wifi_pass") && web.arg("wifi_pass").length()) prefs.putString("wifi_pass", web.arg("wifi_pass"));
  if (web.hasArg("mq_pass")   && web.arg("mq_pass").length())   prefs.putString("mq_pass",   web.arg("mq_pass"));
  prefs.end();
  web.send(200, "text/html",
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<body style='font:15px system-ui;background:#111;color:#ddd;padding:2rem'>"
    "Saved. Rebooting - this page will stop responding for a few seconds.");
  delay(250);
  ESP.restart();
}

static void handleForget() {
  if (!webAuthed()) return;
  web.send(200, "text/html",
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<body style='font:15px system-ui;background:#111;color:#ddd;padding:2rem'>"
    "Reverted to built-in settings. Rebooting.");
  delay(250);
  cfgPanic();
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
  TO_DISPLAY.begin(115200);   // native USB CDC to the display MCU
  cfgLoad();
  scaleLoad();
  prefs.begin("scopeclock", true);
  autoNowPlaying = prefs.getUChar("autonp", 1) != 0;
  wobbleOn       = prefs.getUChar("wobble", 1) != 0;
  atomZ          = prefs.getUChar("atomz", 0);
  zonesJson      = prefs.getString("zones", "");
  if (atomZ > 118) atomZ = 0;
  prefs.end();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Power saving costs ~300ms of latency, which is invisible for an hourly
  // time sync and very visible for a notification that should appear now.
  WiFi.setSleep(false);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  configTzTime(cfg.tz.c_str(), cfg.ntp.c_str());   // NTP + TZ/DST handled here

  if (cfg.mqttHost.length()) {
    mqtt.setServer(cfg.mqttHost.c_str(), (uint16_t)cfg.mqttPort.toInt());
    mqtt.setCallback(onMqtt);
    mqtt.setSocketTimeout(2);           // do not sit on a dead broker
    mqtt.setBufferSize(4096);           // discovery, and whole traced scenes
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


  // Config set through the web page can lock us off the network, and the
  // network is how firmware gets here. If overrides are in play and we still
  // have not associated after 90s, assume they are the problem and fall back
  // to the compiled-in values, which are known to have worked.
  if (WiFi.status() != WL_CONNECTED) {
    if (cfgOverridden && millis() > 90000UL) cfgPanic();
    return;
  }

  // OTA needs the network up, so arm it on first association rather than in
  // setup(). Deliberately no progress callbacks: they would print to Serial,
  // which IS the protocol link, and land in the middle of a frame.
  static bool otaReady = false;
  if (!otaReady) {
    ArduinoOTA.setHostname(OTA_HOST);
    if (OTA_PASS[0]) ArduinoOTA.setPassword(OTA_PASS);
    ArduinoOTA.begin();
    web.on("/", HTTP_GET, [] { web.send_P(200, "text/html", WEB_UI); });
    web.on("/api/state", HTTP_GET, handleState);
    web.on("/api/faces", HTTP_GET, handleFaces);
    web.on("/api/face", HTTP_POST, handleApi);
    web.on("/api/brightness", HTTP_POST, handleApi);
    web.on("/api/ticker", HTTP_POST, handleApi);
    web.on("/api/element", HTTP_POST, handleApi);
    web.on("/api/wobble", HTTP_POST, handleApi);
    web.on("/api/autonp", HTTP_POST, handleApi);
    web.on("/api/scale", HTTP_POST, handleApi);
    web.on("/api/notify", HTTP_POST, handleApi);
    web.on("/api/banner", HTTP_POST, handleApi);
    web.on("/api/scene", HTTP_POST, handleApi);
    web.on("/api/audio", HTTP_POST, handleApi);
    web.on("/api/relink", HTTP_POST, handleApi);
    web.on("/config", HTTP_GET, handleRoot);
    web.on("/save", HTTP_POST, handleSave);
    web.on("/forget", HTTP_GET, handleForget);
    web.begin();
    otaReady = true;
  }
  web.handleClient();
  // Blocks only while an update is actually in flight, and the device rides
  // that out on its own RTC — which is the entire point of it keeping time.
  ArduinoOTA.handle();
  scaleSaveTick();     // lazy NVS write; the knob emits one event per detent

  // Re-send the zone deltas whenever this bridge's own UTC offset changes.
  //
  // That covers all three cases with one test: NTP landing after the first send
  // (offset moves from unknown to real), summer time starting or ending, and the
  // timezone being edited on the config page. An hourly timer covered only the
  // middle one, and left a wrong world clock on screen for up to an hour.
  // One proactive re-announce shortly after boot.
  //
  // By far the commonest cause of a wedged link is this bridge having just
  // restarted — an OTA, a config save — and in that case the deaf detector's
  // path is 30s of boot blackout, then 30s before the device counts as deaf,
  // then up to 60s of rate limiting. Two minutes of blank tube waiting to
  // rediscover something we already know happened. If nothing has been heard
  // from the display 12s in, reset the peripheral once and let it re-enumerate.
  static bool bootKick = false;
  if (!bootKick && millis() > 12000 && !rxHello && !haveStatus) {
    bootKick = true;
    usbPeripheralReset();
  }

  static int lastZoneOff = INT32_MIN;
  if (zonesJson.length() && time(nullptr) >= 1000000000L) {
    const int off = localUtcOffsetMin();
    if (off != lastZoneOff) { lastZoneOff = off; sendZones(); }
  }

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
