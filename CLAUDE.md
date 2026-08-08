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
- **Micro-USB device** jack ← a computer → USB **MIDI** (drives the `midiscope` /
  `midichord` faces) and USB audio. The MK66 has two independent USB controllers,
  so the bridge on the back and a DAW on the front run at once.

## Current state

**P0–P4 are done and running on hardware.** The render engine, the NTP-
disciplined RTC over a USB-host link, and the generic draw-list path all work.
**26 faces** — dials, digital, the five Platonic solids, a tesseract, generative
curves, and two driven live by USB-MIDI — plus `PushList` / `Banner` / `SetMode` /
`SetBrightness` / `SetHz` from the host, MQTT + Home Assistant discovery on the
bridge, host-uploadable face templates, and a web config page.

Faces live in `faces_time/_3d/_gen/_midi.cpp` behind `faces_impl.h`; `faces.cpp`
is only the registry and the knob/button navigation. **Face order is the wire
id** — append, never insert, and the bridge derives its MQTT and web lists from
`kFaceNames[]` so there is one list to update, not three.
The bridge flashes over Wi-Fi (`pio run -e atoms3u_ota -t upload`); the Teensy
flashes in one command (`pio run -t upload`).

Upstream `SCTVcode/*.ino` is still the reference for anything not yet ported —
clone it alongside.

## Hard-won details (each of these cost hours; do not rediscover them)

- **Brightness is beam dwell per dot, and it cannot be a constant.** What the eye
  reads is beam-on time per refresh, which depends on how many dots a frame has.
  `vec::tuneDwell` steers it on measured frame time. Speeding up rendering
  *dims* the tube unless the dwell grows to compensate — that is not a bug.
- **Size every face in the host sim before flashing.** `scratchpad/hostsim/faces6`
  sweeps 1100 frames per face and reports the worst extent and worst-case dot
  cost. A single sampled frame is not enough: the tesseract and the tunnel both
  ran off the tube only partway through a rotation, and `sector` only reaches
  95% of the frame budget at 23:59:59.
- **A face switch overruns one frame.** The dwell is tuned for the previous
  face's dot count, so jumping from a sparse face to a dense one reports a wild
  `frameUs` (49ms was observed) until `tuneDwell` reconverges a few frames later.
  Self-correcting; not a bug to chase.
- **Scope music is the RATIO between two channels, not their sum.** Put the
  lower note on X and the upper on Y and a fifth draws the 3:2 figure a fifth
  actually is. Summing both notes into both axes — the obvious implementation —
  destroys exactly the thing worth seeing, because then neither axis is any one
  note. Also: path length scales with cycle count, so dense figures must be
  drawn *smaller* or they blow the frame (a semitone cluster is 16:15 and wants
  37ms of a 16.7ms frame at full size).
- **Never call `USBSerialBase::begin()`** — it spins up to 5s and `yield()` does
  not run the render loop, so the CRT freezes on a static image. Enumeration
  already sets line coding and DTR/RTS. `USBSerialBase::write()` also spins
  unbounded when its buffer is full; gate every send on `availableForWrite()`.
- **The bridge must be spoken to first.** ESP32 `HWCDC::write` discards
  everything until the peripheral *receives* something, and a soft reset clears
  that. A soft reset does not drop the USB pull-up, so the Teensy sees no
  disconnect — the device re-announces on a timer to break the tie.
- **`USBSerial` will not claim the AtomS3U on descriptors alone**: Espressif
  sets subclass 2 on the CDC Data interface where the driver demands 0. The
  VID/PID constructor with a forced `CDCACM` sertype is the way in.
- The blanking pin is **active low** despite its name: HIGH makes photons.
- The RTC holds local time; the bridge owns TZ/DST. A wrong `TZ_POSIX` makes the
  clock silently, confidently wrong — check any candidate against tzdata.
- Credentials live in `bridge-esp32/.env` (gitignored, this repo is public).
  `-D` defines are silently DROPPED if appended from a *post* script; verify
  they reached the ELF rather than trusting a green build.

## Verifying without eyes on the tube

Two habits that have caught real bugs repeatedly, both worth continuing:
`scratchpad/hostsim` compiles the real `vector.cpp`/`text.cpp`/`faces.cpp`
against a fake DAC and renders a frame to SVG, so geometry can be checked before
flashing; and the PushList decoder is fuzzed under ASan/UBSan with guard bytes,
since it is the only place untrusted bytes become a structure.

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

Nothing is outstanding on the roadmap. Open threads, none blocking:

- `EventEncoder`/`EventButton` reach Home Assistant as device triggers, but the
  button still has no local behaviour beyond cycling the variant within a family.
- The MIDI faces only read note on/off and the sustain pedal. Pitch bend, velocity
  curves and per-channel colouring are all unclaimed, and `hal::midi::MidiState`
  already carries enough to do them.
- Nothing auto-switches to `midiscope` when MIDI starts arriving. `MidiState`
  has `lastEventMs`, so "hand the tube to whatever is playing, then give it back"
  is a few lines — but it fights whatever the host last pushed, hence not done.
- Scenes and templates are documented only in `shared/protocol.h`; if the repo
  gets users, that wants a page of its own with worked examples.

Teensy OTA was considered and **rejected**: FlasherX would work, but a failed
update leaves no valid application, and the only way back in is the physical
program button — which on this build means disassembling the clock. HalfKay
flashing is retryable and unbrickable; the micro-USB jack is externally
accessible, so any always-on machine on that port gives remote updates safely.

## Environment note

You can build (`pio run`) but **flashing is the human's job** — they plug the
Teensy / AtomS3U into their machine and run `pio run -t upload`. Ask them to flash
and report results; don't assume upload happened.
