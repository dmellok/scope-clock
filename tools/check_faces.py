#!/usr/bin/env python3
"""Check the bridge's face table against the device's registry.

Face order IS the wire id: Status reports an index, and the bridge turns it back
into a name. If the two lists drift, the clock shows one face while the web page
and Home Assistant claim another — which looks like a firmware bug and is not.

The families matter too. The knob walks between them, so the bridge's groups
have to be exactly the contiguous runs that faces.cpp declares, not merely a
plausible carve-up.

Reads source only, so it needs no hardware:

    tools/check_faces.py            # exit 0 if they agree

SPDX-License-Identifier: GPL-2.0-or-later
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def strip_comments(t):
    return re.sub(r"//[^\n]*", "", t)


def device_faces():
    src = (ROOT / "display-teensy/src/faces.cpp").read_text()
    body = re.search(r"RenderFn kFaces\[\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
    names = [n.strip() for n in strip_comments(body).replace("\n", " ").split(",") if n.strip()]
    fam = re.search(r"const Family kFamilies\[\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
    runs = [(int(a), int(b)) for a, b in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", fam)]
    return names, runs


def bridge_faces():
    src = (ROOT / "bridge-esp32/src/main.cpp").read_text()
    body = re.search(r"static const FaceEntry kFaces\[\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
    return re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}', strip_comments(body))


def main():
    names, runs = device_faces()
    pairs = bridge_faces()
    bad = []

    if len(names) != len(pairs):
        bad.append(f"count: device has {len(names)}, bridge has {len(pairs)}")

    for i, (dev, (br, _)) in enumerate(zip(names, pairs)):
        if dev != br:
            bad.append(f"index {i}: device '{dev}' vs bridge '{br}'")

    # Each declared family must be exactly one contiguous run of one group.
    covered = 0
    for first, count in runs:
        groups = {pairs[i][1] for i in range(first, min(first + count, len(pairs)))}
        if len(groups) != 1:
            bad.append(f"faces {first}..{first+count-1} span groups {sorted(groups)}")
            continue
        g = groups.pop()
        span = [i for i, (_, gg) in enumerate(pairs) if gg == g]
        if span != list(range(first, first + count)):
            bad.append(f"group '{g}' is at {span[0]}..{span[-1]}, family says {first}..{first+count-1}")
        covered += count
    if covered != len(names):
        bad.append(f"families cover {covered} faces, registry has {len(names)}")

    if bad:
        print("face tables disagree:")
        for b in bad:
            print("  -", b)
        return 1
    print(f"ok: {len(names)} faces, {len(runs)} families, names and groups in step")
    return check_fonts() or check_scale_slots(len(names))


# The per-face size table has to be at least as long as the registry. It was 32
# while there were 46, and nothing failed: the device indexed it with
# faceId % kMaxFaces, so the atom (face 34) quietly adjusted the tick dial's
# size and its own control did nothing at all.
def check_scale_slots(n):
    dev = re.search(r"kMaxFaces\s*=\s*(\d+)",
                    (ROOT / "display-teensy/include/state.h").read_text())
    bri = re.search(r"faceScale\[(\d+)\]",
                    (ROOT / "bridge-esp32/src/main.cpp").read_text())
    if not dev or not bri:
        print("cannot find the scale table sizes")
        return 1
    d, b = int(dev.group(1)), int(bri.group(1))
    if d < n or b < n:
        print(f"scale table too short for {n} faces: device {d}, bridge {b}")
        return 1
    print(f"ok: scale slots {d} device / {b} bridge, for {n} faces")
    return 0


# The typeface list is spelled out in three places for three different reasons:
# the device needs the metrics, the bridge needs the Home Assistant options, and
# the page needs something to put in a <select>. Only the order ties them.
def check_fonts():
    dev = [m.group(1).strip().lower() for m in re.finditer(
        r"^\s*\{[^}]*\},\s*//\s*\d+\s+(.+?)\s*$",
        block(ROOT / "display-teensy/src/text.cpp", "const Face kFaces[]"), re.M)]
    bri = re.findall(r'"([^"]+)"',
                     block(ROOT / "bridge-esp32/src/main.cpp", "kFontNames[] ="))
    web = re.findall(r'"([^"]+)"',
                     block(ROOT / "bridge-esp32/src/webui.h", "var FONTS=", "];"))
    if dev and dev == bri == web:
        print(f"ok: {len(dev)} typefaces in step")
        return 0
    print("typeface tables disagree:")
    print("  device:", dev)
    print("  bridge:", bri)
    print("  web:   ", web)
    return 1


def block(path, marker, end="};"):
    text = path.read_text()
    i = text.index(marker)
    return text[i:text.index(end, i)]


if __name__ == "__main__":
    sys.exit(main())
