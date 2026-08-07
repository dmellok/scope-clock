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

## Status: skeleton

Every module is stubbed with `TODO(port)` markers pointing at the function in the
original firmware to port. Suggested order = the phases in `docs/architecture.md`:
get it building, port the render engine, add the link, open the generic path.
