# Architecture (summary)

Three tiers, one direction of flow:

    Orchestrator (optional host)  — decides *what* to show (MQTT/HA/scenes)
        │  draw lists · set-time · banners
    Wi-Fi bridge (ESP32-S3)       — Wi-Fi, NTP, MQTT; translates to the protocol
        │  framed serial  ▲ input · status
    Display MCU (Teensy)          — THIN CLIENT: renders vectors + keeps time (RTC)
        │  X / Y / Z analog
    Analog + CRT                  — untouched

The device renders **draw lists**. A small set of *local* faces render from the
RTC so the clock is autonomous offline; everything else arrives as pushed lists.

## Physical (zero-hardware-mod route)

- Rear **USB-A (host)** jack  ← AtomS3U  → Wi-Fi: time, notifications, MQTT.
- **Micro-USB (device)** jack ← computer → USB audio: oscilloscope-music source.
  (The MK66 has two independent USB controllers, so both run at once.)

## Phases

- P0  De-spaghettify in place (this skeleton): real modules, state in structs.
- P1  Serial link + SET_TIME; ESP32 does Wi-Fi+NTP. Retire GPS.
- P2  PUSH_LIST + BANNER — true thin client.
- P3  MQTT/HA on the bridge; scenes as draw lists.
- P4  OTA, templates, web config.
