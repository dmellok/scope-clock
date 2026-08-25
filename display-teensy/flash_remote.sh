#!/bin/sh
# Flash the Teensy over the network, via the Pi on its front micro-USB jack.
#
# The local upload_teensy.sh cannot be used remotely: it drives PJRC's
# teensy_reboot, which is only a client — it relays the reboot request through
# the Teensy Loader *GUI*, so a windowing system has to be running. tofu is
# headless, so that whole path is unavailable.
#
# What replaces it is the 134-baud reboot request. The Teensy core watches the
# CDC line coding and drops into HalfKay when the host asks for 134 baud, and it
# does that in the USB ISR — which is why it still works on a board whose main
# loop has hung. That is the same mechanism `pio run -t upload` uses locally to
# recover a wedged board, so it is already proven on this hardware.
#
#   1. build (unless a .hex is passed)
#   2. copy the hex over
#   3. knock: stty 134 -> the sketch reboots into HalfKay, ttyACM0 disappears
#   4. teensy_loader_cli -w catches HalfKay and programs it
#
# After programming, the Teensy re-enumerates and the front-jack console comes
# back. The BRIDGE link does not come back on its own straight away: reflashing
# drops the USB-host port and USBHost_t36 will not re-claim a device that was
# already sitting there, so the device's own 40s-no-claim self-restart is what
# recovers it. That is budgeted at two attempts per power-on (a .noinit counter),
# so if it does not come up after ~90s, replug the AtomS3U by hand. Leave ~30s
# before OTAing the bridge.
set -e

HOST="${SCOPE_HOST:-kayden@tofu}"
PORT="${SCOPE_PORT:-/dev/ttyACM0}"
MCU="${SCOPE_MCU:-mk66fx1m0}"
HERE="$(cd "$(dirname "$0")" && pwd)"

# pio is not on PATH in a non-interactive shell (it lives in PlatformIO's own
# venv), so resolve it rather than assuming the caller's login profile ran.
PIO="$(command -v pio || true)"
[ -n "$PIO" ] || [ ! -x "$HOME/.platformio/penv/bin/pio" ] || PIO="$HOME/.platformio/penv/bin/pio"

HEX="$1"
if [ -z "$HEX" ]; then
  [ -n "$PIO" ] || { echo "flash: pio not found; pass a .hex instead" >&2; exit 1; }
  echo "== building"
  ( cd "$HERE" && "$PIO" run -e teensy36 )
  HEX="$HERE/.pio/build/teensy36/firmware.hex"
fi
[ -f "$HEX" ] || { echo "flash: no such file: $HEX" >&2; exit 1; }

echo "== copying $(basename "$HEX") to $HOST"
scp -q "$HEX" "$HOST:/tmp/scope-firmware.hex"

# The knock and the programming run in ONE ssh session: the loader has to be
# waiting locally on the Pi when HalfKay appears, and a second ssh round-trip
# is long enough to matter on a cold link.
#
# Order is knock-then-wait rather than the local script's wait-then-knock,
# because HalfKay sits there indefinitely once entered — there is no race to
# lose. If the board is ALREADY in HalfKay the knock fails harmlessly (no
# ttyACM0) and the loader catches it immediately, so a half-finished previous
# run self-heals.
ssh "$HOST" "sh -s" <<'REMOTE'
set -e
PORT="${PORT:-/dev/ttyACM0}"
if [ -e "$PORT" ]; then
  echo "== knocking $PORT into HalfKay (134 baud)"
  stty -F "$PORT" 134 2>/dev/null || true
  sleep 2
else
  echo "== $PORT absent; assuming board is already in HalfKay"
fi

echo "== programming"
timeout 90 teensy_loader_cli --mcu=mk66fx1m0 -w -v /tmp/scope-firmware.hex
REMOTE

echo "== done; the bridge link needs ~40-90s to re-claim (see header)"
