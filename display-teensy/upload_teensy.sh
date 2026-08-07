#!/bin/sh
# Flash the Teensy without touching the board.
#
# PlatformIO's stock teensy-gui protocol fails here with "Teensy Loader could
# not find the file firmware", and teensy-cli's own soft reboot (-s) cannot find
# the serial interface once our USB type is MIDI/Audio/Serial rather than plain
# Serial — it reports "Error opening USB device" and then waits for a button
# press that nobody is there to give.
#
# What does work is PJRC's teensy_reboot, but it is a client: it relays the
# reboot request through the Teensy Loader GUI, so the GUI has to be running
# first. That is the whole trick.
#
#   1. start the Loader GUI (harmless if already up)
#   2. start teensy_loader_cli -w, which waits for HalfKay to appear
#   3. ask teensy_reboot to drop the running sketch into HalfKay
#   4. the waiting loader programs it
#
# macOS-specific in step 1 only. If the board is already in HalfKay (reset
# button held), step 2 catches it immediately and step 3 harmlessly fails.
set -e

HEX="$1"
[ -n "$HEX" ] || { echo "usage: $0 <firmware.hex>" >&2; exit 1; }
[ -f "$HEX" ] || { echo "upload: no such file: $HEX" >&2; exit 1; }

TOOL="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/tool-teensy"
[ -x "$TOOL/teensy_loader_cli" ] || {
  echo "upload: teensy tools not found under $TOOL" >&2; exit 1; }

if [ -d "$TOOL/teensy.app" ]; then
  open -g "$TOOL/teensy.app" 2>/dev/null || true   # -g: do not steal focus
  sleep 2
fi

"$TOOL/teensy_loader_cli" --mcu=TEENSY36 -w -v "$HEX" &
loader=$!

# Give the loader a moment to start waiting, then knock. Retry: the GUI may
# still be coming up on a cold start.
for _ in 1 2 3; do
  kill -0 "$loader" 2>/dev/null || break     # already programmed
  sleep 2
  "$TOOL/teensy_reboot" >/dev/null 2>&1 || true
done

wait "$loader"
