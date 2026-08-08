#!/usr/bin/env python3
"""Play a MIDI file (or a demo of intervals) into the clock's front USB jack.

The clock enumerates as a USB-MIDI device — the Teensy's USB type is
USB_MIDI_AUDIO_SERIAL, so the micro-USB jack on the front is a MIDI port named
something like "Teensy MIDI". Anything that speaks MIDI can drive the
`midiscope` / `midichord` faces: a DAW, a keyboard through a host, or this.

    pip install mido python-rtmidi

    ./play_midi.py --list
    ./play_midi.py --demo                  # walk the intervals, slowly
    ./play_midi.py song.mid
    ./play_midi.py song.mid --port "Teensy"

Nothing here is required to use the feature; it exists so the faces can be
exercised without a keyboard on the desk.

SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import sys
import time

try:
    import mido
except ImportError:
    sys.exit("needs mido: pip install mido python-rtmidi")

# What each interval draws, which is the whole point of the face: a scope plots
# one channel against the other, so the figure IS the frequency ratio.
DEMO = [
    ("unison      1:1  a circle",        [0]),
    ("octave      2:1",                  [12]),
    ("fifth       3:2  the classic",     [7]),
    ("fourth      4:3",                  [5]),
    ("major third 5:4",                  [4]),
    ("minor third 6:5",                  [3]),
    ("tritone     7:5",                  [6]),
    ("major triad",                      [4, 7]),
    ("minor triad",                      [3, 7]),
    ("dim seventh",                      [3, 6, 9]),
    ("major 7th chord",                  [4, 7, 11]),
]
ROOT = 60  # middle C


def open_port(match):
    names = mido.get_output_names()
    if not names:
        sys.exit("no MIDI outputs found — is the clock's front jack connected?")
    for n in names:
        if match.lower() in n.lower():
            return mido.open_output(n), n
    sys.exit(f"no MIDI output matching {match!r}. Available:\n  " + "\n  ".join(names))


def demo(port, hold):
    for label, offsets in DEMO:
        notes = [ROOT] + [ROOT + o for o in offsets if o]
        print(f"  {label:32s} {notes}")
        for n in notes:
            port.send(mido.Message("note_on", note=n, velocity=100))
        time.sleep(hold)
        for n in notes:
            port.send(mido.Message("note_off", note=n))
        time.sleep(0.35)


def play(port, path):
    # mido's play iterator already sleeps for each message's delta time, so this
    # runs in real time without a scheduler of its own.
    for msg in mido.MidiFile(path).play():
        if not msg.is_meta:
            port.send(msg)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", nargs="?", help="a .mid file to play")
    ap.add_argument("--port", default="Teensy", help="output port substring (default: Teensy)")
    ap.add_argument("--demo", action="store_true", help="walk the intervals instead")
    ap.add_argument("--hold", type=float, default=2.5, help="demo seconds per interval")
    ap.add_argument("--list", action="store_true", help="list MIDI outputs and exit")
    args = ap.parse_args()

    if args.list:
        for n in mido.get_output_names():
            print(n)
        return
    if not args.demo and not args.file:
        ap.error("give a .mid file or --demo")

    port, name = open_port(args.port)
    print(f"-> {name}")
    print("   set the clock to the 'midiscope' face (web UI, Home Assistant, or the knob)")
    try:
        if args.demo:
            demo(port, args.hold)
        else:
            play(port, args.file)
    except KeyboardInterrupt:
        pass
    finally:
        # Always: a sequencer that stops mid-chord otherwise leaves the figure
        # frozen on notes nobody is playing, which looks like a crashed clock.
        for ch in range(16):
            port.send(mido.Message("control_change", channel=ch, control=123, value=0))
        port.close()
        print("\nall notes off")


if __name__ == "__main__":
    main()
