// protocol.h — wire protocol shared by the Teensy client and the ESP32 bridge.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Framing: [START=0x7E][id:u8][len:u8][payload:len][crc8]
// crc8 is over id+len+payload. Keep this file the single source of truth;
// both platformio.ini files add `-I ../shared`.
#pragma once
#include <stdint.h>

namespace proto {

static constexpr uint8_t START = 0x7E;
static constexpr uint8_t MAX_PAYLOAD = 240;

// Per-face size, in percent. Shared so the page, the bridge, the knob and the
// device cannot disagree about the range — they did, and the slider's floor
// was the only one anybody could see.
static constexpr uint8_t kMinScale = 20;
static constexpr uint8_t kMaxScale = 250;

enum class Msg : uint8_t {
  // host -> device
  SetTime        = 0x01,   // local (TZ/DST-applied) time -> RTC
  SetMode        = 0x02,   // face id | pushed
  PushList       = 0x03,   // arbitrary draw list, retained
  Banner         = 0x04,   // text + duration_ms + priority
  SetBrightness  = 0x05,
  SetHz          = 0x06,   // 50 | 60
  Ping           = 0x07,
  PushBegin      = 0x08,   // start staging a draw list larger than one frame
  PushChunk      = 0x09,   // append raw PushList bytes to the staging buffer
  PushCommit     = 0x0A,   // decode what was staged and show it
  Notify         = 0x0B,   // title + body overlay, placed, auto-expiring
  SetScales      = 0x0C,   // per-face render scale, percent, one byte each
  SetNowPlaying  = 0x0D,   // current track, for the nowplaying face
  SetGauges      = 0x0E,   // labelled percentages, for the gauges face
  SetWobble      = 0x0F,   // anti-burn-in drift on/off
  SetElement     = 0x10,   // atom face: atomic number, 0 = cycle
  SetWeather     = 0x11,   // temperature, sky code, place, detail
  SetTicker      = 0x12,   // marquee text
  SetZones       = 0x13,   // world clock: offsets from LOCAL time
  SetFont        = 0x14,   // default typeface for text that does not name one
  SetConstell    = 0x15,   // constellation chart: pin one, or cycle
  // device -> host
  Hello          = 0x81,   // fw version, caps, panel size
  Pong           = 0x82,
  EventEncoder   = 0x83,   // signed delta
  EventButton    = 0x84,   // press | long
  Status         = 0x85,   // uptime, rtc ok + last-set age, mode, frame us
  EventScale     = 0x86,   // face id + new scale, when set at the knob
};

// CRC-8, polynomial 0x07. Incremental so a receiver can fold each byte in as
// it streams past, without buffering the frame to CRC it afterwards.
inline uint8_t crc8_update(uint8_t c, uint8_t b) {
  c ^= b;
  for (uint8_t i = 0; i < 8; ++i)
    c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  return c;
}

inline uint8_t crc8(const uint8_t* p, uint16_t n) {
  uint8_t c = 0x00;
  while (n--) c = crc8_update(c, *p++);
  return c;
}

// The frame check: ONE contiguous run over id, then len, then payload.
//
// Both ends must call this rather than rolling their own. Combining separate
// CRCs — crc8(hdr) ^ crc8(payload), as the skeleton did — is not the same
// function and is measurably weaker. CRC is linear over GF(2), so an error
// contributes a syndrome that depends only on its bit pattern and its distance
// from the end of the run. Give a byte the same offset in two runs of the same
// length and it produces the same syndrome, so XORing the two CRCs cancels it:
// with len == 2, flipping bit k of `id` and bit k of `payload[0]` is entirely
// invisible. Brute force over two-byte corruptions puts that at 40 misses in
// 1344; one contiguous run misses none, because a byte's offset in the stream
// is then unique.
inline uint8_t frameCrc(uint8_t id, uint8_t len, const uint8_t* payload) {
  uint8_t c = crc8_update(crc8_update(0x00, id), len);
  for (uint8_t i = 0; i < len; ++i) c = crc8_update(c, payload[i]);
  return c;
}

// ---------------------------------------------------------------------------
// Payload layouts. All multi-byte fields are little-endian.
//
// SetMode      [mode:u8][faceId:u8]   mode 0 local face, 1 pushed list,
//                                    2 audio (stereo drives X/Y; see hal/audio.h)
//              mode 0 = render a local face (faceId selects it)
//              mode 1 = render the retained pushed list
//
// PushList     [count:u8] followed by `count` self-describing items:
//                Text    0x01, x:i16, y:i16, scale:i16, len:u8, chars  (8+len)
//                Line    0x02, x:i16, y:i16, x2:i16, y2:i16            (9)
//                Circle  0x03, cx:i16, cy:i16, r:i16                   (7)
//                Clock   0x04, x:i16, y:i16, scale:i16, len:u8, fmt    (8+len)
//                Hand    0x05, cx:i16, cy:i16, r0:i16, r1:i16, src:u8  (10)
//
//              The last two are what make a pushed list a FACE TEMPLATE rather
//              than a still image: they are re-evaluated against the RTC every
//              refresh, so the host can define a whole working clock face
//              without a firmware rebuild, and it keeps running with the host
//              gone. No separate mode — a list is a template exactly when it
//              contains one of them.
//
//                Clock fmt tokens: %H 24h  %I 12h  %M min  %S sec
//                                  %d day  %m mon  %y year  %% literal
//                Hand src: 0 = seconds, 1 = minutes, 2 = hours. Drawn from
//                radius r0 to r1 about (cx,cy), 0 = north, clockwise.
//              The tags match ItemType on the device. Text is NOT terminated
//              on the wire — the device copies each string into its own arena
//              and terminates it there, because Item holds a pointer and the
//              receive buffer is reused by the very next frame.
//              A text item at x=0,y=0 opts into the device's own centring.
//
// Banner       [ms:u16][priority:u8][chars...]   text runs to end of payload
// SetScales    [count:u8][pct:u8 x count]   100 = nominal, clamped to
//                kMinScale..kMaxScale. The floor is 20 rather than something
//                smaller because a face's text stops shrinking with it: the
//                font's scale is an integer and bottoms out at 1.
//                Sent on Hello and whenever the host changes one, so the
//                device can apply it without a round trip when the knob
//                selects a face.
// EventScale   [faceId:u8][pct:u8]   the knob changed a scale; save it.
// SetNowPlaying [flags:u8][durS:u16][progS:u16][titleLen:u8][artistLen:u8]
//                [title][artist][album]   flags bit0 = playing.
//                Sent when the track CHANGES, not per second: the device
//                advances the progress ring itself between messages.
// SetWeather   [tempC10:i16][sky:u8][placeLen:u8][place][detail]
//                sky 0 clear, 1 part cloud, 2 cloud, 3 rain, 4 snow,
//                5 storm, 6 fog. Few on purpose: a vector tube can draw
//                those distinctly and cannot draw twenty.
// SetFont      [id:u8]   0 regular, 1 seven segment, 2 condensed, 3 wide,
//                4 italic, 5 bold. A DEFAULT: a face that names a typeface
//                explicitly (the digital clock asking for segments) keeps it.
// SetConstell  [id:u8]   0 cycles every nine seconds, 1..88 pins one. The
//                order is the device's sky::kCons, brightest figure first;
//                bridge-esp32/src/constellations.h carries the same names.
// SetZones     [n:u8] then n x [deltaMin:i16][labelLen:u8][label]
//                deltaMin is minutes to ADD TO LOCAL TIME, not to UTC. The
//                host owns every question about summer time and re-sends
//                when an offset changes; the device never learns that
//                timezones exist. See hard rule 4.
// SetTicker    [chars...]   marquee text, to the end of the payload.
// SetElement   [z:u8]    1..118 pins the atom face to one element; 0 cycles.
// SetWobble    [on:u8]   continuous slow drift of the whole image, which
//                supersedes the hourly screensaver nudge while it is on.
// SetGauges    [n:u8] then n x [pct:u8][labelLen:u8][label], then a footer
//                string to the end. Nothing in it says where the numbers
//                came from, so any source can drive it.
// Notify       [ms:u16][place:u8][titleLen:u8][title][body]
//                place: low bits 0 bottom strip, 1 top strip, 2 centred card;
//                bit 0x80 = solo, i.e. blank the face behind it.
//                titleLen may be 0; body runs to the end of the payload.
//                ms == 0 clears any notification early. Like Banner, the
//                expiry is the DEVICE's — a host that dies mid-notice
//                cannot leave one burnt on the screen.
//              Overlaid on whatever is showing and expires locally after `ms`,
//              so a host that dies mid-banner cannot strand it on the screen.
//              ms = 0 clears any banner immediately.
//
// SetBrightness [level:u8]   255 = full beam dwell
// SetHz         [hz:u8]      50 or 60
//
// PushBegin / PushChunk / PushCommit
//              A PushList payload is limited to MAX_PAYLOAD, which is about 34
//              items — fine for a banner or a face template, nowhere near
//              enough for traced artwork. These stage the identical byte
//              stream ([count][items...]) across as many frames as it takes:
//              Begin resets the staging buffer, each Chunk appends its whole
//              payload, Commit decodes the result exactly as PushList would.
//
//              Chunks carry no sequence number by design. The link is an
//              ordered, CRC-checked byte stream over USB, so a chunk cannot
//              arrive out of order — it can only fail to arrive, and Commit
//              catches that because the staged bytes then fail to decode.
// ---------------------------------------------------------------------------

// Status payload (device -> host, sent periodically).
//
// frameUs against 1e6/hz is the number that matters: exceeding it means the
// refresh is free-running slower than hz, which reads as a dim, flickering
// tube. setAgeS lets the host see that the RTC is being disciplined rather
// than merely present.
struct __attribute__((packed)) StatusPayload {
  uint32_t uptimeS;
  uint32_t frameUs;      // last render duration
  uint16_t hz;           // refresh target
  uint16_t setAgeS;      // seconds since last SetTime, 0xFFFF = never
  // Seconds since the device last RECEIVED anything. The link can fail in one
  // direction only — the device's uplink keeps working while the downlink is
  // dead — so this is how the far end learns it has gone deaf. Nothing else
  // can tell it: from the host's side its own writes still look successful.
  uint16_t linkSilentS;
  uint8_t  mode;         // 0 = local face, 1 = pushed list
  uint8_t  faceId;
  uint8_t  brightness;
  uint8_t  rtcOk;
};

// SetTime payload (local time; host already applied timezone + DST).
struct __attribute__((packed)) SetTimePayload {
  uint8_t year;   // 0-99 (20xx)
  uint8_t month;  // 1-12
  uint8_t day;    // 1-31
  uint8_t hour;   // 0-23
  uint8_t minute; // 0-59
  uint8_t second; // 0-59
};

} // namespace proto
