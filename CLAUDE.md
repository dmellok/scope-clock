# scope-clock — agent brief

You are picking up a firmware project mid-scaffold. Read this fully before editing.

## What this is

A ground-up rewrite of the **SCTV Scope Clock** firmware (an X-Y oscilloscope-tube
clock) into a **thin vector-rendering client**, paired with an **ESP32-S3 Wi-Fi
bridge** (M5Stack AtomS3U). The display MCU does only two hard-real-time jobs —
steer the CRT beam and keep time — and everything networked/smart lives off-device.

- Derivative of https://github.com/nixiebunny/SCTVcode (David Forbes / Cathode
  Corner), **GPL-2.0-or-later**. Keep that license; paste the full GPL text into
  `LICENSE` before any distribution.
- Original hardware: **Teensy 3.6** (internal dual 12-bit DAC → X/Y, digital blank
  → Z), **DS3232** RTC (I2C), rotary encoder + button, centering pots.

## Architecture (already reflected in the tree)

    bridge-esp32/   ESP32-S3: Wi-Fi, NTP, (later) MQTT. Speaks shared/protocol.h.
    display-teensy/ Teensy: renders draw lists + keeps time. THE THIN CLIENT.
    shared/         protocol.h — one source of truth, both projects `-I ../shared`.

Data flow: host/bridge pushes **draw lists** + `SET_TIME` + banners down; the
device sends input events + status up. A small set of **local faces** render from
the RTC so the clock is autonomous when the network is down — that is the whole
reason the device keeps an RTC.

Physical (zero-hardware-mod route the owner chose):
- Rear **USB-A host** jack ← AtomS3U → Wi-Fi/time/notifications.
- **Micro-USB device** jack ← a computer → USB audio (oscilloscope-music source).
  The MK66 has two independent USB controllers, so both run at once.

## Current state

Skeleton only. Interfaces (headers), the module split, the pipeline `loop()`, the
protocol framing, and the ESP32 NTP→SET_TIME path are real. Render bodies are
stubs. Every stub carries a `TODO(port)` naming the original `.ino` function to
bring over. The original source to port FROM is the upstream repo above
(`SCTVcode/*.ino`) — clone it alongside for reference.

## Hard rules (do not violate)

1. **Nothing in the loop may block.** The main loop *is* the CRT refresh; a stall
   is visible flicker. All link/RTC/input calls stay non-blocking or time-bounded.
   (The original's `userial.begin()` that "hangs if the splash screen is too big"
   is the exact anti-pattern to avoid.)
2. **State lives in structs** (`ClockState`, `DeviceState`) — do not reintroduce
   loose globals. That was the main thing the rewrite exists to fix.
3. **Faces via the registry** (`faces.cpp`), never `if (mode == 0/1/2)`.
4. **The RTC holds LOCAL time.** The bridge/host applies timezone + DST. Never put
   timezone tables on the MCU.
5. **Keep the radio off the beam.** Networking stays on the ESP32, never folded
   into the Teensy render path.
6. Preserve the tuned beam timing when porting (`motionDelay`, `settlingDelay`,
   `glowDelay`) — it is what keeps circles clean.

## Roadmap (do these in order; commit at each)

- **P0 — make it draw.** Port the render engine: `vector.cpp` (sin/cos tables,
  line + circle beam stepping) and `text.cpp` (segment font, `DispStr`, `Center`)
  from `d_drawing.ino` / `b_font.ino`. Port `rtc_ds3232.cpp` BCD read/write from
  `readRTCtime`/`writeRTCtime`. Goal: the digital + hands faces render live time.
- **P1 — link + real sync.** Finish the framed protocol (fix the placeholder CRC
  to one contiguous run over id+len+payload on BOTH sides), wire `EventEncoder`/
  `EventButton` up, and confirm the ESP32 `SET_TIME` disciplines the RTC.
- **P2 — generic path.** Implement `PushList` + `Banner` (banner auto-expires
  locally). Device becomes a true thin client.
- **P3 — smarts on the bridge.** MQTT/Home-Assistant → banners/scenes as draw lists.
- **P4 — polish.** OTA (both MCUs), host-uploadable face templates, web config.

## Your task right now

Start **P0**: clone upstream SCTVcode for reference, then port `vector.cpp` and
`text.cpp` so `display-teensy` builds and the two built-in faces draw real time
from the RTC. Fill the `TODO(port)` stubs; keep the module boundaries and the hard
rules above. Build with `pio run` in `display-teensy/`.

## Environment note

You can build (`pio run`) but **flashing is the human's job** — they plug the
Teensy / AtomS3U into their machine and run `pio run -t upload`. Ask them to flash
and report results; don't assume upload happened.
