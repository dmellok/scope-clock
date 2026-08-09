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
**47 faces** — dials, digital, the five Platonic solids, a tesseract, generative
curves, digital rain, a real star chart and celestial globe, a centring target,
and two driven live by USB-MIDI — plus `PushList` / `Banner` / `SetMode` /
`SetBrightness` / `SetHz` from the host, MQTT + Home Assistant discovery on the
bridge, host-uploadable face templates, and a web config page.

Faces live in `faces_time/_3d/_gen/_midi.cpp` behind `faces_impl.h`; `faces.cpp`
is only the registry and the knob/button navigation. **Face order is the wire
id** — append, never insert. The bridge's `kFaces[]` carries each name with its
family group, and MQTT discovery, the web picker and the API all derive from
that one table. `tools/check_faces.py` proves it still matches the device's
registry and `kFamilies` runs, and that the six typeface names agree across the
device, the bridge and the page — from source alone, so run it after adding a
face or a font.
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
- **USB audio in is HALF DONE and off by default.** `hal/audio.h` +
  `audio_usb.cpp` hand both DACs to the Audio library's DMA so a stereo pair
  drives X/Y directly. It genuinely draws — a circle and a 3:2 figure rendered
  from the Mac at 44.1kHz — but the Teensy still watchdog-resets when the host
  *tears down* the audio stream, so there is no button for it in the web UI.
  Enter it with `2` on the front-jack serial console, leave with `f`. What is
  already established, so it need not be re-bisected:
    * `AudioMemory(12)` starves the moment audio actually streams — both USB
      directions allocate. 40 blocks is what made the first stream work at all.
    * `AudioAnalyzePeak` hangs the chip within ~2s when read from the loop; the
      identical graph left unread runs forever. Replaced with `PeakTap`, ~25
      lines, integers only. Do not put the library analyzer back.
    * `AudioOutputAnalogStereo`'s *constructor* calls `begin()`, which seizes
      both DACs and blocks ~257ms in a DC ramp — hence placement-new on first
      use, never a global. `begin()` also leaves the DAC reference at 1.2V;
      `analogReference(EXTERNAL)` restores the 3.3V the geometry assumes.
    * Stop by clearing `PDB0_SC` (the DMA's trigger), not by re-running
      `begin()`, which would re-ramp and re-allocate the DMA channel.
    * 192kHz cannot come in this way: `usb_desc.c` hardcodes `tSamFreq 44100`
      with `bSamFreqType 1` and a 180-byte endpoint. That is the SD-card path.
- **The microSD socket is EMPTY, and an empty socket hangs the board.**
  `SD.begin(BUILTIN_SDCARD)` does not return false with no card — it spins in
  SDIO init forever. Probed from the console ('s', see sdprobe.cpp); it never
  reached "mounted" or "NO CARD". So the 192kHz path needs the clock opened once
  to seat a card; there is nothing in there to read.
  The probe now refuses to run until the watchdog is armed (20s uptime), which
  turns that hang into a 2s reset instead of a dead clock needing a reflash.
- **A dead link is usually a dead CABLE, and only a REPLUG fixes it.** Two hours
  went on treating "no Status arriving" as a protocol wedge. It was not: the
  Teensy's host port had never claimed a device and the ESP32 had never been
  enumerated by anything. Neither `/api/relink` (a soft `SYSTEM_USB_DEVICE_RST`)
  nor power-cycling the whole clock cleared it — only physically unplugging the
  AtomS3U and pushing it back in. The likely reason is that a simultaneous cold
  boot RACES: USBHost_t36 will not claim a device that was already sitting there
  when its stack came up, and a replug into an already-running Teensy is the one
  thing that gives it a clean attach event. Reflashing the Teensy and OTAing the
  bridge back to back is the reliable way to land in it; leave ~30s between.
  Ask both ends before theorising — they now answer. The device prints
  `link down: no device claimed` once a second on the front-jack console while
  its host port holds nothing, and `/api/state` carries `usb`, which is whether
  the ESP32's own peripheral has been enumerated by anybody. Those two separate
  the three cases the link trace cannot: wedged one way, wedged both ways, and
  never connected.
- **Do NOT retry the recovery kick on a fixed short interval.** Resetting the
  peripheral yanks the USB device away from the far side, so a host that keeps
  losing it can never finish claiming one — a 30s retry is strictly worse than a
  single shot. It backs off (12s, 45s, 2min, 5min) and then gives up.
- **A hung Teensy is recoverable over USB — the physical button is not needed.**
  `pio run -t upload` rebooted a fully hung board (the 134-baud reboot request
  is handled in the USB ISR, which keeps running when the main loop does not).
  Worth knowing against the OTA-rejection reasoning below: a *hang* is not the
  same as a failed FlasherX leaving no valid application.
- **Host-fed faces advance themselves.** `nowplaying` gets a track when the
  track CHANGES and runs the progress ring off millis() in between, so a song is
  a few hundred bytes rather than an update a second down a link that wedges.
  Same shape as the RTC: the host supplies truth, the device keeps time against
  it. `gauges` is deliberately generic — n labelled percentages and a footer,
  nothing about where they came from — so any topic can drive it.
- **`char` is UNSIGNED on the Teensy and SIGNED on the sim's host.** The font's
  walk loops compared `char >= 0x20`, which accepts a katakana code of 0xA0 on
  the device (160) and rejects it in the sim (-96) — the tube would have drawn
  the rain while the harness measured an empty string. Both loops in text.cpp
  use uint8_t now. Anything above 0x7F in a string is exposed to this.
- **The font has TYPEFACES now, selected per text item.** `Item.font` is 0 for
  the stroke face and 1 for seven segment; `DrawList::text(...,font)` and
  `txt::drawString/measure/inkWidth` all take it, and `centerLines` measures with
  the item's own face. Adding a third means one glyph table and one branch in
  `glyph()`.
  The segment page is a SUBSET on purpose — a seven-segment cell cannot render
  most letters, and the ones people fake (K, M, W) read as noise, so anything
  outside digits/hex/a few others falls back to a space rather than drawing a
  lie. Watch side bearings: its ink spans x 1..11, and an advance of 14 walked a
  row of digits right of centre.
- **The font has a second page at 0xA0: 32 katakana.** They suit a stroke font —
  two to four straight strokes each — where hiragana would need curves it cannot
  draw. Reached through the same `glyph()` lookup, so faces, pushed scenes and
  the ticker all get them without knowing.
- **Never send a time-derived value before the clock is set.** `sendZones()` ran
  on Hello, which happens well before NTP lands, so every delta was measured
  against an offset of zero — i.e. the raw UTC offset — and Auckland read 22:40
  instead of 12:44, exactly Melbourne's own ten hours out. It now refuses to send
  until `time()` is valid, and re-sends whenever the bridge's UTC offset CHANGES.
  That one test covers NTP arriving late, summer time starting or ending, and the
  timezone being edited, where the hourly timer it replaced covered only the
  middle case and left a wrong clock up for an hour.
- **The world clock never learns what a timezone is.** The host sends deltas in
  minutes relative to the DEVICE'S LOCAL TIME, not to UTC, so the device only
  ever adds a number. The bridge computes its own UTC offset from localtime vs
  gmtime (watch the year-end case, where tm_yday differs by ~365) and re-sends
  hourly, which is the only thing that corrects a zone when summer time shifts.
  That is hard rule 4 honoured rather than worked around.
- **An Item keeps the char* it is handed — one buffer per row, always.** This has
  now bitten twice: every gauge label identical, then every world-clock row
  showing the same city. If a face formats N strings it needs N buffers.
- **The sim needs a CLOCK.** `fake/Arduino.h` had `millis()` return 0, so the
  ticker rendered an empty window and measured as a two-item face; the
  notification expiry, now-playing progress and the wobble were all frozen too.
  It now steps 17ms a rendered frame. Any face driven by time is untested
  without it.
- **The sim's dot count is NOT the whole frame cost.** Every item also pays
  settling, glow and the blanked jump to its start — about 20us each. Ignoring
  that made oganesson look like 86% of budget when the tube reported 102-109%.
  `faces6` and `atomz` now add `items * 0.020ms`, which reproduces the hardware
  to within a few percent. Trust it over the raw dot count.
- **Text on a round face is limited by the CHORD at its height, not the width.**
  A configuration string fits the field easily and still puts its corners past
  the glass near the top. And ink runs from one descender BELOW the baseline to
  the cell height above it — Mg, Ag, Hg, Rg, Np and Dy all have tails, and
  ignoring them clipped exactly those symbols and no others.
- **Anti-burn-in is a continuous drift now, not an hourly jump.** `vec::tickWobble`
  walks the whole image round a 45-count circle every four minutes, called from
  the loop in EVERY mode — burn does not care which mode put the image there.
  It supersedes `updateScreenSaver`'s hourly raster while on (two things writing
  saveX/saveY would fight), and saveX/saveY are in DAC counts, added after the
  3/2 display scale. Switchable from the page, MQTT and a Home Assistant switch;
  the bridge persists it and re-sends on Hello.
- **The sky faces are GENERATED, and the chart is mirrored if you are careless.**
  `tools/gen_stars.py` fetches Stellarium's constellation figures (GPL-2.0+, so
  compatible) and the HYG catalogue, and bakes 725 stars to UNIT VECTORS plus a
  per-figure view basis and fitting scale — the device never does trigonometry
  on a star or normalizes anything. The trap: `cross(z, w)` points EAST, which
  puts east on the right, i.e. the celestial sphere seen from OUTSIDE. A chart
  is drawn looking UP, east to the LEFT. Every figure comes out a mirror image
  and Orion is near enough symmetric that you will not see it. The generator now
  asserts Betelgeuse lands up-and-left of Rigel and Alkaid left of Dubhe, and
  those checks were confirmed to FAIL on the old basis before being kept.
  Regenerate with `python3 tools/gen_stars.py`; it writes the device's
  `stars.h` AND the bridge's `constellations.h`, so the picker cannot drift.
- **On a round face the bound is a RADIUS, not a box — and `faces6` measures a
  box.** A name inside +-1200 in x and y can still have its corners off the
  glass, because at y = -1000 the chord is only about 560 either side. The
  constellation name is fitted to the chord at its LOWEST ink, and the figure is
  fitted small (880) and lifted 200 to leave that strip clear.
  `scratchpad/hostsim/constells.cpp` measures worst RADIUS over all 88 — a face
  that cycles on a 9s timer also needs a way to be pinned, or a 1100-frame sweep
  at 17ms only ever sees two of them.
- **Two Q16 values multiplied overflow int32.** Everywhere else in the render
  path one side is a coordinate under 10^4 and there is room; the globe's view
  vector is `sin * sin`, i.e. 2^32, and UBSan caught it in the host sim. Three
  64-bit multiplies once a frame is nothing.
- **NO leading zeros in coordinate tables.** A tidily column-aligned `-060` is
  octal 48, and `-0960` will not compile at all. Both happened while padding the
  teapot's spout and handle to line up, and only the second was an error the
  compiler could see.
- **Judge a rotating face over a full turn, not one frame.** The teapot looked
  like a stack of dashes at pitch 0 (the eye lies in the plane of every parallel,
  so each ring collapses to a line) and lopsided with meridians over half a turn.
  `scratchpad/hostsim/phases.cpp` renders quarter turns side by side.
- **SVG import belongs in the browser, not the firmware.** The page already has
  a complete SVG engine: `getPointAtLength` flattens beziers, arcs and nested
  transforms exactly, where an ESP32 parsing path data by hand would get a dozen
  cases wrong. The device only ever receives straight lines. The 192-item cap is
  the real constraint, so outlines are Douglas-Peucker simplified with the
  tolerance BINARY-SEARCHED rather than guessed — the value that fits depends
  entirely on the drawing. `tools/vec2scene.py` still exists for rasters and for
  scripting; this is the same job for a file you have to hand.
- **Auto-showing a face must be edge-triggered and overridable.** The first
  now-playing version switched whenever a track message arrived and the device
  was on a local face, so every update yanked the screen back and navigating
  away while music played was impossible. It now fires only on a transition
  (playback starting, or the song id changing), and a deliberate face choice —
  web, MQTT or the knob — suppresses it until playback stops. The subtlety that
  bit next: an override only counts if something was PLAYING at the time, or
  picking a face during silence would mute the next track's arrival.
- **RUN tools/check_faces.py after adding a face.** Adding `nowplaying` and
  `gauges` to the device registry and forgetting the bridge's table cost a flash
  cycle: the picker showed 27 faces and neither new one could be selected. The
  checker names it in one line and needs no hardware.
- **Device units are NOT DAC counts, and the tube's radius is ~1800.** Measured,
  not assumed: push concentric rings (`C 0 0 600` … `C 0 0 1800`) and look. Every
  face here is authored against a 1200-unit working edge, so `vec::` scales by
  3/2 on the way to the DAC — one place, because `line()` and `ellipseArc()` are
  the only primitives and text, scenes and overlays all go through them. Drawing
  1:1 used two thirds of the glass.
  Consequences worth knowing: `kLineStride` went 1 → 2, because a 1.5x bigger
  picture at the same dot spacing costs 1.5x the beam travel and put two faces
  over budget — 2 counts is far finer than the spot, and `tuneDwell` restores the
  brightness. And anything in device units that must stay on the glass has to sit
  at or under 1200 (the notification strips were at 1215, which was inside the
  old field and outside this one).
- **There is a settings MENU on the knob, and that is what makes it standalone.**
  The RTC holds local time and the bridge was the only thing that could write
  it, so a clock with no bridge could not be corrected for drift, a flat backup
  cell or summer time — "the clock is autonomous" was not quite true. The
  gesture escalates off the one that already existed: 800ms enters size mode,
  keep holding to 2.5s and it drops size mode and opens the menu. Turn to move,
  tap to select; entries are set time, set date, face size, typeface, burn-in
  drift, info, exit. The selection rule for what belongs here: a setting is in
  the menu if changing it means anything with NO bridge attached. Wi-Fi and MQTT
  stay on the config page.
  Drawn as a jog dial — three rows with the selection fixed in the middle —
  because seven labels stacked down a round tube run off the chord at the top
  and bottom, and a rotary encoder does not read as "scroll a page".
  `settime.cpp` REPLACES the frame rather than overlaying it, because you have
  to see which field the knob is on before you turn it. An abandoned edit
  expires after 30s WITHOUT committing — a half-set clock is worse than the
  drift. Seeding and committing are flags on DeviceState: the RTC belongs to the
  main loop, and `hal::input` has no business knowing it exists.
  Two traps found here. A menu entry that hands the knob to an existing mode
  must ARM that mode's timeout — size mode reads a deadline it did not set, and
  an unarmed one is already in the past, so it expired on the next poll. And the
  day's range is a FUNCTION of the month and year, not 31: moving off the 31st
  into February has to leave a real date behind, so `clampDay` runs after every
  change. The weekday needs no such care — `rtc_ds3232.cpp` derives it from the
  date on every read rather than trusting the DS3232's own counter.
- **A local change the host also tracks needs an up-event or it is undone.**
  `EventScale` already existed; picking a typeface or toggling the drift at the
  knob had nothing, so the next Hello would have replayed the bridge's old value
  over it. `EventFont`/`EventWobble` close that. The bridge stores and publishes
  but does NOT send it back — echoing races the next local change. Watch the NVS
  type while you are there: `putBool` against the `getUChar` the rest of the
  file uses reads back as the default, silently.
- **The per-face size table must be at least as long as the registry.** It was
  `kMaxFaces = 32` while there were 46 faces, and nothing failed loudly: the
  device indexed it `faceScale[faceId % kMaxFaces]`, so the atom (face 34)
  aliased face 2 — its own size control did nothing, and adjusting it silently
  resized the tick dial. Fourteen faces were affected. Both sides now hold 64
  and index with a bounds check rather than a modulo, and `check_faces.py`
  proves the bound from source. A wrapping index turns an out-of-range value
  into a plausible wrong answer; a bounds check turns it into a default.
- **Per-face scale defaults to 70%, and the floor is 20%.** 100% puts a face's own 1200-unit edge on
  the rim, which suits a full-bleed animation and crowds a dial whose numerals
  then touch the glass. The bridge owns the persistence (NVS, `fscale`) because
  the Teensy has nowhere to keep it, and re-sends the whole table on Hello.
  Long-press the knob's button on the clock to adjust the showing face by hand.
- **A calibration target has to opt OUT of everything that moves it.** The
  centring face draws rings at 400/800/1200 device units, the outer one landing
  on the measured 1800-count rim, so "is it centred and how much glass am I
  using" is answerable by looking. That only holds if nothing rescales or
  shifts it: `faces::rawScale()` exempts it from the per-face size, and
  `vec::holdWobble()` parks the anti-burn-in drift while it is up WITHOUT
  touching the user's setting. A reference that is itself wandering 45 counts
  round a circle is not a reference. `vec::setAlign()` is additive on top of the
  internal trimmer pots rather than replacing them, so the pots still work.
- **The tube is ROUND; the draw list is not.** An animation laid out in a
  rectangle spends beam time in the corners where there is no phosphor. The
  matrix face gives each column the chord the circle actually allows it —
  half-height sqrt(R^2 - x^2) — and a trail scaled to that chord, or the short
  outer columns spend their whole cycle off the face and the rim looks empty.
  To check an animation's envelope rather than one frame, accumulate lit dots
  over a few hundred frames (`scratchpad/hostsim/cover.cpp`) and look at the
  shape: it also counts any ink that lands outside the rim.
- **A face must stay inside ±1200; an overlay may use ±1250.** `faces6` enforces
  the former, which is why the matrix rain's TOP is the edge less one glyph
  height — the bound is on the text BASELINE and ink runs upward from it.
- **A strip fits ONE line; only the centred card fits two.** The clear air
  between the numeral ring (ink to ~1100) and the edge of the field is about
  150 units, and one line at scale 7 is 140. Stacking a title above a body in a
  strip lands it squarely on the numerals — `notify.cpp` joins them into one
  line instead and shrinks to fit. Ink width is exactly linear in scale below
  40, so the fitting scale is a division, not a search.
- **Debug the Teensy over its own front-jack console, not through the bridge.**
  Reflashing drops the USB-host port and USBHost_t36 never re-claims it, so
  every experiment routed through the link costs a multi-minute recovery (or an
  `/api/relink`). `include/debug.h` prints are gated on `availableForWrite()`,
  and `dbg::resetCause()` decodes RCM_SRS0/1 at boot — a Teensy hard fault does
  not reset the chip, it hangs, and our own watchdog turns that into a reset
  2s later, so "it rebooted" and "it faulted" are indistinguishable without it.
  Note the console cannot keep up with a fast loop: gate traces to ~1Hz, or
  longer strings get dropped by the buffer and absence proves nothing.
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

- **The link can be WATCHED now, not inferred.** The bridge keeps a 96-frame
  ring of both directions (`traceAdd` off the single `sendFrame`/`onFrame` choke
  points) and the page polls `/api/proto?after=<seq>` with a cursor, so a poll
  normally returns the two or three frames since the last one. Capture is a
  memcpy and a counter; the decoding to English costs more than storing the
  bytes, so it happens when the page asks. This is the view that makes the
  one-way failure obvious — arrows going one way only — rather than something
  you deduce from `linkSilentS` after the tube has already stopped updating.

## Verifying without eyes on the tube

Two habits that have caught real bugs repeatedly, both worth continuing:
`scratchpad/hostsim` compiles the real `vector.cpp`/`text.cpp`/`faces.cpp`
against a fake DAC and renders a frame to SVG, so geometry can be checked before
flashing; and the PushList decoder is fuzzed under ASan/UBSan with guard bytes,
since it is the only place untrusted bytes become a structure.

## Hard rules (do not violate)

1. **Nothing in the loop may block.** The main loop *is* the CRT refresh; a stall
   is visible flicker. All link/RTC/input calls stay non-blocking or time-bounded.
   (Upstream flags this against itself: `userial.begin()` carries the note
   "This hangs if splash screen is too big, awaiting proper fix". Same hazard,
   and the reason every link call here is time-bounded.)
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
