# scope-clock

A rewrite of the SCTV Scope Clock firmware as a **thin vector-rendering client**
with an NTP-disciplined RTC, plus an **ESP32-S3 Wi-Fi bridge** (M5Stack AtomS3U).

Derivative of [nixiebunny/SCTVcode](https://github.com/nixiebunny/SCTVcode)
(David Forbes / Cathode Corner), **GPL-2.0-or-later**.

## Layout

    scope-clock/
    ├─ scope-clock.code-workspace   ← open this in VS Code (multi-root)
    ├─ shared/protocol.h            ← single source of truth for the wire protocol
    ├─ display-teensy/              ← the thin client (Teensy 3.6 + DS3232 + CRT)
    └─ bridge-esp32/                ← Wi-Fi bridge (ESP32-S3 / AtomS3U)

## Prerequisites

1. VS Code + the **PlatformIO IDE** extension.
2. Teensy flashing: install **Teensyduino**; on Linux add the `49-teensy.rules` udev rules.
3. ESP32 needs nothing extra — the `espressif32` platform is fetched by PlatformIO.

## Build

Open the workspace, then per project use the PlatformIO toolbar (Build ✓ / Upload →),
or the CLI:

    cd display-teensy && pio run            # build
    cd display-teensy && pio run -t upload   # flash (board plugged in)

    cd bridge-esp32   && pio run
    cd bridge-esp32   && pio run -t upload

## Status

**The roadmap is complete and running on hardware.** Forty-seven built-in faces
draw from the DS3232; the ESP32 bridge disciplines the RTC from NTP over a
USB-host link with no wiring; the host can push arbitrary vector scenes,
banners, and *face templates* that keep telling the time on their own; and the
clock appears in Home Assistant over MQTT discovery with controls, diagnostics
and device triggers.

![the built-in faces](faces.png)

The knob walks the fifteen families — dials, digital, solids, curves, motion,
wireframes, the sky and the rest — and the button changes the style within one.

### Every face

Rendered by compiling the firmware's own `vector.cpp` against a fake DAC and
integrating the dots with a decay, which is what phosphor does — so these are
the real geometry and the real beam path, not mock-ups, but they are renders
rather than photographs of the tube. Each clip runs three seconds at the speed
the clock runs, and the second hand is really ticking. Faces drawn from pushed
data show sample data.

<!-- FACES:BEGIN -->

| | | | | | |
|:-:|:-:|:-:|:-:|:-:|:-:|
| <img src="docs/faces/hands.gif" width="132" alt="hands"><br>**hands**<br><sub>Analog</sub> | <img src="docs/faces/numbers.gif" width="132" alt="numbers"><br>**numbers**<br><sub>Analog</sub> | <img src="docs/faces/tickdial.gif" width="132" alt="tickdial"><br>**tickdial**<br><sub>Analog</sub> | <img src="docs/faces/orbit.gif" width="132" alt="orbit"><br>**orbit**<br><sub>Analog</sub> | <img src="docs/faces/sector.gif" width="132" alt="sector"><br>**sector**<br><sub>Analog</sub> | <img src="docs/faces/digital.gif" width="132" alt="digital"><br>**digital**<br><sub>Digital</sub> |
| <img src="docs/faces/datetime.gif" width="132" alt="datetime"><br>**datetime**<br><sub>Digital</sub> | <img src="docs/faces/wordclock.gif" width="132" alt="wordclock"><br>**wordclock**<br><sub>Digital</sub> | <img src="docs/faces/binary.gif" width="132" alt="binary"><br>**binary**<br><sub>Digital</sub> | <img src="docs/faces/tetra.gif" width="132" alt="tetra"><br>**tetra**<br><sub>Solids</sub> | <img src="docs/faces/cube.gif" width="132" alt="cube"><br>**cube**<br><sub>Solids</sub> | <img src="docs/faces/octa.gif" width="132" alt="octa"><br>**octa**<br><sub>Solids</sub> |
| <img src="docs/faces/icosa.gif" width="132" alt="icosa"><br>**icosa**<br><sub>Solids</sub> | <img src="docs/faces/dodeca.gif" width="132" alt="dodeca"><br>**dodeca**<br><sub>Solids</sub> | <img src="docs/faces/tesseract.gif" width="132" alt="tesseract"><br>**tesseract**<br><sub>Solids</sub> | <img src="docs/faces/torus.gif" width="132" alt="torus"><br>**torus**<br><sub>Solids</sub> | <img src="docs/faces/lissajous.gif" width="132" alt="lissajous"><br>**lissajous**<br><sub>Curves</sub> | <img src="docs/faces/harmonograph.gif" width="132" alt="harmonograph"><br>**harmonograph**<br><sub>Curves</sub> |
| <img src="docs/faces/spirograph.gif" width="132" alt="spirograph"><br>**spirograph**<br><sub>Curves</sub> | <img src="docs/faces/rose.gif" width="132" alt="rose"><br>**rose**<br><sub>Curves</sub> | <img src="docs/faces/lorenz.gif" width="132" alt="lorenz"><br>**lorenz**<br><sub>Curves</sub> | <img src="docs/faces/starpoly.gif" width="132" alt="starpoly"><br>**starpoly**<br><sub>Curves</sub> | <img src="docs/faces/starfield.gif" width="132" alt="starfield"><br>**starfield**<br><sub>Motion</sub> | <img src="docs/faces/tunnel.gif" width="132" alt="tunnel"><br>**tunnel**<br><sub>Motion</sub> |
| <img src="docs/faces/midiscope.gif" width="132" alt="midiscope"><br>**midiscope**<br><sub>MIDI</sub> | <img src="docs/faces/midichord.gif" width="132" alt="midichord"><br>**midichord**<br><sub>MIDI</sub> | <img src="docs/faces/matrix.gif" width="132" alt="matrix"><br>**matrix**<br><sub>Effects</sub> | <img src="docs/faces/nowplaying.gif" width="132" alt="nowplaying"><br>**nowplaying**<br><sub>Data</sub> | <img src="docs/faces/gauges.gif" width="132" alt="gauges"><br>**gauges**<br><sub>Data</sub> | <img src="docs/faces/teapot.gif" width="132" alt="teapot"><br>**teapot**<br><sub>Wireframes</sub> |
| <img src="docs/faces/sphere.gif" width="132" alt="sphere"><br>**sphere**<br><sub>Wireframes</sub> | <img src="docs/faces/knot.gif" width="132" alt="knot"><br>**knot**<br><sub>Wireframes</sub> | <img src="docs/faces/mobius.gif" width="132" alt="mobius"><br>**mobius**<br><sub>Wireframes</sub> | <img src="docs/faces/helix.gif" width="132" alt="helix"><br>**helix**<br><sub>Wireframes</sub> | <img src="docs/faces/atom.gif" width="132" alt="atom"><br>**atom**<br><sub>Science</sub> | <img src="docs/faces/solar.gif" width="132" alt="solar"><br>**solar**<br><sub>Sky</sub> |
| <img src="docs/faces/moon.gif" width="132" alt="moon"><br>**moon**<br><sub>Sky</sub> | <img src="docs/faces/weather.gif" width="132" alt="weather"><br>**weather**<br><sub>Sky</sub> | <img src="docs/faces/pong.gif" width="132" alt="pong"><br>**pong**<br><sub>Games</sub> | <img src="docs/faces/life.gif" width="132" alt="life"><br>**life**<br><sub>Games</sub> | <img src="docs/faces/trailclock.gif" width="132" alt="trailclock"><br>**trailclock**<br><sub>Extra</sub> | <img src="docs/faces/ticker.gif" width="132" alt="ticker"><br>**ticker**<br><sub>Extra</sub> |
| <img src="docs/faces/worldclock.gif" width="132" alt="worldclock"><br>**worldclock**<br><sub>Extra</sub> | <img src="docs/faces/asteroids.gif" width="132" alt="asteroids"><br>**asteroids**<br><sub>Arcade</sub> | <img src="docs/faces/constell.gif" width="132" alt="constell"><br>**constell**<br><sub>Stars</sub> | <img src="docs/faces/starglobe.gif" width="132" alt="starglobe"><br>**starglobe**<br><sub>Stars</sub> |  |  |

<!-- FACES:END -->

Regenerate with `python3 tools/gen_gallery.py` after adding a face. Two further faces are driven live over
**USB-MIDI** on the front jack: `midiscope` plots the lower note against the
upper, so an interval draws its own frequency ratio the way an oscilloscope in
X-Y mode does (a fifth is the 3:2 figure), and `midichord` shows the sounding
pitch classes as a shape on the chromatic wheel. `tools/play_midi.py --demo`
walks the intervals if there is no keyboard to hand.

### Without the bridge

The clock is autonomous: all 46 faces live on the Teensy and render from the
DS3232, the knob and button work locally, and nothing in boot or the render loop
waits on the link. Unplug the bridge and it keeps time and keeps drawing.

**Hold the knob's button for 2.5 seconds for the settings menu** — past the 0.8s
that enters size mode. Turn to move, tap to select: set time, set date, face
size, typeface, burn-in drift, info, exit. The editors take three fields each —
turn to change, tap for the next, the last tap commits — and an edit you walk
away from expires after 30s without changing anything.

A setting is in that menu if changing it means something with no bridge
attached. Wi-Fi, MQTT and the rest stay on the config page.

The config page carries a **link trace**: every frame between the two MCUs in
both directions, decoded, newest last. Both arrows moving means the link is
healthy; only inbound moving is the one-way failure this hardware is prone to,
and it shows there before any symptom reaches the tube.

The config page also has **centring**, which shifts the whole image on top of
the trimmer pots inside the case, and a **target** face of concentric rings to
set it against. The outer ring sits on the tube's usable radius, so it should
touch the glass all the way round when the picture is centred.

What the bridge adds is NTP (and therefore summer time, which the device never
reasons about), persistence for the per-face sizes and the other settings, the
five host-fed faces — `weather`, `ticker`, `worldclock`, `nowplaying`, `gauges` —
and everything networked: MQTT, Home Assistant, pushed scenes and the config
page.

The bridge also serves a config page at `http://scope-clock-bridge.local/`
(guarded by the OTA password) for setting Wi-Fi, MQTT and timezone without
rebuilding.

| phase | what | state |
|-------|------|-------|
| P0 | render engine: vectors, stroke font, RTC | done |
| P1 | framed link, real CRC, encoder/button, NTP `SET_TIME` | done |
| P2 | `PushList` + `Banner` — true thin client | done |
| P3 | MQTT / Home Assistant on the bridge | done |
| P4 | face templates, web config | done |

Flashing: `pio run -t upload` in `display-teensy/`, and
`pio run -e atoms3u_ota -t upload` in `bridge-esp32/` (over Wi-Fi).
Copy `bridge-esp32/.env.example` to `.env` and fill it in first.

## Licence

Copyright (C) 2026 Kayden D'Mello.
Derived from [SCTVcode](https://github.com/nixiebunny/SCTVcode),
Copyright (C) 2008-2022 David Forbes (Cathode Corner).

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; **either version 2 of the License, or (at your option) any later
version**. It is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See [`LICENSE`](LICENSE) for the full text.
