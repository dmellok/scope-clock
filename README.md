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

**P0 (render engine) is done and running on hardware.** Both built-in faces —
analog hands and digital — draw live time from the DS3232, and the analog
centring pots work. Remaining modules still carry `TODO(port)` markers pointing
at the function in the original firmware to bring over; the phase order is in
`docs/architecture.md`.

| phase | what | state |
|-------|------|-------|
| P0 | render engine: vectors, stroke font, RTC | done |
| P1 | framed link, real CRC, encoder/button, NTP `SET_TIME` | next |
| P2 | `PushList` + `Banner` — true thin client | |
| P3 | MQTT / Home Assistant on the bridge | |
| P4 | OTA, face templates, web config | |

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
