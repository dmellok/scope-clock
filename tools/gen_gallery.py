#!/usr/bin/env python3
"""Rewrite the face gallery in README.md from the bridge's face table.

The gallery is 46 rows of name, family and image path, which is exactly the
information already in kFaces[] — so it is generated rather than typed. The
same reasoning as tools/check_faces.py: a hand-maintained second copy of the
face list has already shipped a picker that was two faces short.

The clips themselves come from scratchpad/hostsim/gifs.cpp, which renders the
real vector.cpp against a fake DAC and integrates the dots with a decay, the
way phosphor does.

    python3 tools/gen_gallery.py

Edits only the block between the FACES markers; everything else is left alone.
SPDX-License-Identifier: GPL-2.0-or-later
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
README = ROOT / "README.md"
GIFS = "docs/faces"
COLS = 6
BEGIN = "<!-- FACES:BEGIN -->"
END = "<!-- FACES:END -->"


def faces():
    src = (ROOT / "bridge-esp32/src/main.cpp").read_text()
    i = src.index("kFaces[] = {")
    block = src[i:src.index("};", i)]
    return re.findall(r'\{"([^"]+)"\s*,\s*"([^"]+)"\}', block)


def main():
    rows = faces()
    missing = [n for n, _ in rows if not (ROOT / GIFS / f"{n}.gif").exists()]
    if missing:
        sys.stderr.write(f"warn: no clip for {', '.join(missing)}\n")

    out = [BEGIN, ""]
    out.append("|" + "|".join([" "] * COLS) + "|")
    out.append("|" + "|".join([":-:"] * COLS) + "|")
    for i in range(0, len(rows), COLS):
        chunk = rows[i:i + COLS]
        cells = []
        for n, g in chunk:
            cells.append(f'<img src="{GIFS}/{n}.gif" width="132" alt="{n}"><br>'
                         f'**{n}**<br><sub>{g}</sub>')
        cells += [""] * (COLS - len(chunk))
        out.append("| " + " | ".join(cells) + " |")
    out += ["", END]

    text = README.read_text()
    if BEGIN not in text or END not in text:
        sys.stderr.write("README is missing the FACES markers\n")
        return 1
    head, rest = text.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    README.write_text(head + "\n".join(out) + tail)
    print(f"gallery: {len(rows)} faces, {COLS} across")
    return 0


if __name__ == "__main__":
    sys.exit(main())
