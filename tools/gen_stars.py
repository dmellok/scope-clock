#!/usr/bin/env python3
"""Generate display-teensy/include/stars.h from real catalogue data.

Two sources, neither vendored — this fetches them, so the table can always be
rebuilt from scratch and nothing here is a hand-typed coordinate:

  * Constellation figures: Stellarium's western constellationship.fab, which
    lists each figure as pairs of HIP numbers. Stellarium is GPL-2.0-or-later,
    same as this repo.
  * Star positions and magnitudes: the HYG database (CC BY-SA 4.0). Only the
    coordinates are used, and they are facts.

What comes out is not RA/Dec. Every star is baked to a UNIT VECTOR, because
both faces that consume this want 3D: the globe rotates the sphere outright,
and the chart projects it through a per-constellation basis. Storing angles
would mean a sin/cos per star per frame on a chip with no FPU in the frame
path, and would re-quantize a position we already know exactly.

Likewise the basis and the fitting scale for each figure are computed HERE, in
floating point, once. The device never normalizes anything.

SPDX-License-Identifier: GPL-2.0-or-later
"""

import argparse
import csv
import gzip
import io
import math
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "display-teensy/include/stars.h"
# The bridge needs the same names in the same order to build a picker. Emitting
# them from here rather than retyping them is what keeps the two in step — the
# face list, which IS hand-maintained in two places, needed a checker script.
OUT_BRIDGE = ROOT / "bridge-esp32/src/constellations.h"

FAB_URL = ("https://raw.githubusercontent.com/Stellarium/stellarium/"
           "v0.21.3/skycultures/western/constellationship.fab")
HYG_URL = ("https://raw.githubusercontent.com/astronexus/HYG-Database/"
           "main/hyg/CURRENT/hygdata_v40.csv.gz")

# Stars this faint are carried only so the globe has a sky rather than a
# scattering of first-magnitude points. Every star named by a figure is kept
# whatever its magnitude.
GLOBE_MAG = 3.6

# Unit vectors and basis rows are stored as int16 at this scale: 1/10000 is
# about 0.006 degrees, far finer than the spot on the tube.
UNIT = 10000

# A figure is fitted so its widest star lands here, in device units, and the
# face then lifts it by FIT_LIFT so the name has clear air underneath. Both
# numbers are well short of the 1200 a face may use: the tube is ROUND, so the
# bottom of the field is a chord a few hundred units wide, not a full row.
FIT_RADIUS = 880
FIT_LIFT = 200          # documented here; applied on the device
MAX_SCALE = 20000       # int16 headroom; tiny figures stop growing here

NAMES = {
    "And": "Andromeda", "Ant": "Antlia", "Aps": "Apus", "Aqr": "Aquarius",
    "Aql": "Aquila", "Ara": "Ara", "Ari": "Aries", "Aur": "Auriga",
    "Boo": "Bootes", "Cae": "Caelum", "Cam": "Camelopardalis",
    "Cnc": "Cancer", "CVn": "Canes Venatici", "CMa": "Canis Major",
    "CMi": "Canis Minor", "Cap": "Capricornus", "Car": "Carina",
    "Cas": "Cassiopeia", "Cen": "Centaurus", "Cep": "Cepheus",
    "Cet": "Cetus", "Cha": "Chamaeleon", "Cir": "Circinus",
    "Col": "Columba", "Com": "Coma Berenices", "CrA": "Corona Australis",
    "CrB": "Corona Borealis", "Crv": "Corvus", "Crt": "Crater",
    "Cru": "Crux", "Cyg": "Cygnus", "Del": "Delphinus", "Dor": "Dorado",
    "Dra": "Draco", "Equ": "Equuleus", "Eri": "Eridanus", "For": "Fornax",
    "Gem": "Gemini", "Gru": "Grus", "Her": "Hercules", "Hor": "Horologium",
    "Hya": "Hydra", "Hyi": "Hydrus", "Ind": "Indus", "Lac": "Lacerta",
    "Leo": "Leo", "LMi": "Leo Minor", "Lep": "Lepus", "Lib": "Libra",
    "Lup": "Lupus", "Lyn": "Lynx", "Lyr": "Lyra", "Men": "Mensa",
    "Mic": "Microscopium", "Mon": "Monoceros", "Mus": "Musca",
    "Nor": "Norma", "Oct": "Octans", "Oph": "Ophiuchus", "Ori": "Orion",
    "Pav": "Pavo", "Peg": "Pegasus", "Per": "Perseus", "Phe": "Phoenix",
    "Pic": "Pictor", "Psc": "Pisces", "PsA": "Piscis Austrinus",
    "Pup": "Puppis", "Pyx": "Pyxis", "Ret": "Reticulum", "Sge": "Sagitta",
    "Sgr": "Sagittarius", "Sco": "Scorpius", "Scl": "Sculptor",
    "Sct": "Scutum", "Ser": "Serpens", "Sex": "Sextans", "Tau": "Taurus",
    "Tel": "Telescopium", "Tri": "Triangulum", "TrA": "Triangulum Australe",
    "Tuc": "Tucana", "UMa": "Ursa Major", "UMi": "Ursa Minor",
    "Vel": "Vela", "Vir": "Virgo", "Vol": "Volans", "Vul": "Vulpecula",
}


def fetch(url, cache):
    if cache and cache.exists():
        return cache.read_bytes()
    sys.stderr.write(f"fetching {url}\n")
    with urllib.request.urlopen(url, timeout=300) as r:
        data = r.read()
    if cache:
        cache.write_bytes(data)
    return data


def load_figures(raw):
    """Abbr, N, then 2N HIP ids: one pair of endpoints per segment."""
    figs = []
    for line in raw.decode("utf-8", "replace").splitlines():
        f = line.split()
        if len(f) < 3 or f[0].startswith("#"):
            continue
        abbr, n = f[0], int(f[1])
        hips = [int(x) for x in f[2:2 + 2 * n]]
        if len(hips) != 2 * n:
            sys.stderr.write(f"warn: {abbr} claims {n} segments, has {len(hips)//2}\n")
        figs.append((abbr, [(hips[i], hips[i + 1]) for i in range(0, len(hips) - 1, 2)]))
    return figs


def load_stars(raw, wanted):
    """HIP -> (unit vector, magnitude) for everything named or bright."""
    text = gzip.decompress(raw) if raw[:2] == b"\x1f\x8b" else raw
    out = {}
    for row in csv.DictReader(io.StringIO(text.decode("utf-8", "replace"))):
        hip = row["hip"]
        if not hip:
            continue
        hip = int(float(hip))
        try:
            mag = float(row["mag"])
        except ValueError:
            continue
        if hip not in wanted and mag > GLOBE_MAG:
            continue
        ra = float(row["ra"]) * math.pi / 12.0      # hours -> radians
        dec = float(row["dec"]) * math.pi / 180.0
        out[hip] = ((math.cos(dec) * math.cos(ra),
                     math.cos(dec) * math.sin(ra),
                     math.sin(dec)), mag)
    return out


def basis(c):
    """An orthonormal frame looking along c: u WEST, v north, w out.

    West, not east, and that is the whole subtlety. A chart of the sky is drawn
    as you see it — lying on your back looking up — where right ascension
    increases to the LEFT. Take the obvious cross(z, w) and you get east on the
    right, which is a view of the celestial sphere from outside it: every figure
    comes out a mirror image of the real one, and Orion in particular is near
    enough symmetric that the eye will not catch it.

    Near a celestial pole the reference direction is parallel to c and the cross
    product collapses, so swing to a different one — Octans and Ursa Minor are
    exactly the cases that would otherwise come out as noise.
    """
    w = normalize(c)
    ref = (0.0, 0.0, 1.0) if abs(w[2]) < 0.9 else (1.0, 0.0, 0.0)
    u = normalize(cross(w, ref))     # west
    v = cross(u, w)                  # north
    return u, v, w


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def normalize(a):
    n = math.sqrt(sum(x * x for x in a)) or 1.0
    return tuple(x / n for x in a)


# Two facts about the real sky, checked against the projection that came out.
# Both are chosen because they fail if the frame is mirrored or upside down,
# which is precisely the error no amount of looking at the render will catch.
CHECKS = [
    # Orion: Betelgeuse is the north-east shoulder, Rigel the south-west foot,
    # so on a chart Betelgeuse must sit up and to the LEFT of Rigel.
    ("Ori", 27989, 24436, "Betelgeuse", "Rigel", -1, +1),
    # Ursa Major: Alkaid ends the handle, Dubhe is the far lip of the bowl.
    # Alkaid is the more easterly, so it must sit to the LEFT of Dubhe.
    ("UMa", 67301, 54061, "Alkaid", "Dubhe", -1, 0),
]


def verify(cons, stars, index):
    by = {c["abbr"]: c for c in cons}
    for abbr, ha, hb, na, nb, wantx, wanty in CHECKS:
        c = by.get(abbr)
        if not c or ha not in stars or hb not in stars:
            sys.stderr.write(f"warn: cannot check {abbr}\n")
            continue
        pts = []
        for h in (ha, hb):
            s = stars[h][0]
            z = sum(s[k] * c["w"][k] for k in range(3))
            pts.append((sum(s[k] * c["u"][k] for k in range(3)) / z,
                        sum(s[k] * c["v"][k] for k in range(3)) / z))
        dx, dy = pts[0][0] - pts[1][0], pts[0][1] - pts[1][1]
        for got, want, axis in ((dx, wantx, "x"), (dy, wanty, "y")):
            if want and (got > 0) != (want > 0):
                raise SystemExit(
                    f"{abbr} is flipped in {axis}: {na} came out "
                    f"{'right' if got > 0 else 'left'} of / "
                    f"{'above' if got > 0 else 'below'} {nb}")
    sys.stderr.write(f"orientation checks pass ({len(CHECKS)})\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, default=None,
                    help="directory to keep the downloaded sources in")
    args = ap.parse_args()
    cdir = args.cache
    if cdir:
        cdir.mkdir(parents=True, exist_ok=True)

    figs = load_figures(fetch(FAB_URL, cdir / "cship.fab" if cdir else None))
    named = {h for _, segs in figs for pair in segs for h in pair}
    stars = load_stars(fetch(HYG_URL, cdir / "hyg.csv.gz" if cdir else None), named)

    missing = named - set(stars)
    if missing:
        sys.stderr.write(f"warn: {len(missing)} figure stars absent from the catalogue\n")

    # Brightest first, so the globe can take a prefix of the table as "the
    # naked-eye sky" without a second index.
    order = sorted(stars, key=lambda h: (stars[h][1], h))
    index = {h: i for i, h in enumerate(order)}

    cons, lines = [], []
    for abbr, segs in figs:
        segs = [(a, b) for a, b in segs if a in index and b in index]
        if not segs:
            sys.stderr.write(f"warn: {abbr} has no drawable segments, dropped\n")
            continue
        first = len(lines) // 2      # in segments; kLines holds two per segment
        for a, b in segs:
            lines += [index[a], index[b]]

        members = {h for pair in segs for h in pair}
        # Centroid of the member stars, not of their bounding box: a figure
        # with one far-flung star (Eridanus) should still be centred on its body.
        cen = normalize(tuple(sum(stars[h][0][k] for h in members) for k in range(3)))
        u, v, w = basis(cen)

        # Gnomonic, which is what a star chart is: it keeps the straight lines
        # of the figure straight. Fit so the outermost member lands on the ring.
        far = 0.0
        for h in members:
            s = stars[h][0]
            z = sum(s[k] * w[k] for k in range(3))
            if z < 0.2:                     # >78 deg across; nothing real is
                continue                    # and a small z would explode
            px = sum(s[k] * u[k] for k in range(3)) / z
            py = sum(s[k] * v[k] for k in range(3)) / z
            far = max(far, math.hypot(px, py))
        scale = min(MAX_SCALE, int(FIT_RADIUS / far)) if far > 0 else MAX_SCALE

        # Total flux, so the cycle opens on Orion and Crux rather than Antlia.
        flux = sum(10 ** (-0.4 * stars[h][1]) for h in members)
        cons.append(dict(abbr=abbr, name=NAMES.get(abbr, abbr), first=first,
                         count=len(segs), u=u, v=v, w=w, scale=scale, flux=flux))

    cons.sort(key=lambda c: -c["flux"])

    # Sorting the constellations moved their line ranges, so rebuild the line
    # array in the new order rather than leaving `first` pointing at the old one.
    packed = []
    for c in cons:
        old = c["first"]
        c["first"] = len(packed) // 2
        packed += lines[old * 2:(old + c["count"]) * 2]
    lines = packed

    verify(cons, stars, index)

    globe = sum(1 for h in order if stars[h][1] <= GLOBE_MAG)

    q = lambda f: max(-32768, min(32767, int(round(f * UNIT))))
    o = []
    w = o.append
    w("// stars.h — GENERATED by tools/gen_stars.py; do not edit by hand.")
    w("//")
    w("// Star positions and magnitudes derive from the HYG database")
    w("// (astronexus/HYG-Database, CC BY-SA 4.0); the constellation figures are")
    w("// Stellarium's western skyculture (GPL-2.0-or-later). Regenerate with")
    w("//   python3 tools/gen_stars.py")
    w("//")
    w("// Positions are unit vectors scaled by 10000 in equatorial coordinates:")
    w("// +z is the north celestial pole, +x points at RA 0h on the equator.")
    w("// SPDX-License-Identifier: GPL-2.0-or-later")
    w("#pragma once")
    w("#include <stdint.h>")
    w("")
    w("namespace sky {")
    w("")
    w("constexpr int16_t kUnit = %d;   // what a unit vector's components scale by" % UNIT)
    w("")
    w("struct Star {")
    w("  int16_t x, y, z;      // unit vector * kUnit")
    w("  int16_t mag10;        // visual magnitude * 10; smaller is brighter")
    w("};")
    w("")
    w("// The view frame for one figure: u east, v north, w out towards it, each")
    w("// a unit vector * kUnit. Project gnomonically through it and the figure")
    w("// arrives centred, upright and fitted, with no runtime trigonometry.")
    w("struct Constellation {")
    w("  const char* name;")
    w("  const char* abbr;")
    w("  uint16_t first, count;    // segments in kLines, two indices each")
    w("  int16_t ux, uy, uz;")
    w("  int16_t vx, vy, vz;")
    w("  int16_t wx, wy, wz;")
    w("  int16_t scale;            // multiply the gnomonic ratio by this")
    w("};")
    w("")
    w("// Brightest first, so kGlobeCount is simply a prefix: everything the")
    w("// naked eye would see, without a second index to keep in step.")
    w("constexpr uint16_t kStarCount = %d;" % len(order))
    w("constexpr uint16_t kGlobeCount = %d;   // magnitude %.1f and brighter" % (globe, GLOBE_MAG))
    w("")
    w("const Star kStars[kStarCount] = {")
    for h in order:
        (x, y, z), mag = stars[h]
        w("  { %6d, %6d, %6d, %4d },   // HIP %d" %
          (q(x), q(y), q(z), int(round(mag * 10)), h))
    w("};")
    w("")
    w("constexpr uint16_t kLineCount = %d;   // pairs" % (len(lines) // 2))
    w("const uint16_t kLines[kLineCount * 2] = {")
    for i in range(0, len(lines), 16):
        w("  " + " ".join("%4d," % v for v in lines[i:i + 16]))
    w("};")
    w("")
    w("constexpr uint8_t kConCount = %d;" % len(cons))
    w("const Constellation kCons[kConCount] = {")
    for c in cons:
        w('  { "%s", "%s", %4d, %3d,' % (c["name"], c["abbr"], c["first"], c["count"]))
        for k in "uvw":
            w("    %6d, %6d, %6d," % tuple(q(v) for v in c[k]))
        w("    %5d }," % c["scale"])
    w("};")
    w("")
    w("}  // namespace sky")
    w("")

    OUT.write_text("\n".join(o))

    OUT_BRIDGE.write_text("\n".join([
        "// constellations.h — GENERATED by tools/gen_stars.py; do not edit.",
        "//",
        "// The same names in the same order as sky::kCons in the device's stars.h.",
        "// The id on the wire is 1-based; 0 means cycle. Pipe-separated because the",
        "// only two consumers — the Home Assistant select and the config page — both",
        "// want to split it, and a 88-entry array of pointers costs more flash than",
        "// the string it points into.",
        "// SPDX-License-Identifier: GPL-2.0-or-later",
        "#pragma once",
        "",
        "#define SCOPE_CON_COUNT %d" % len(cons),
        'static const char SCOPE_CON_NAMES[] = "%s";' % "|".join(c["name"] for c in cons),
        "",
    ]))
    bytes_ = len(order) * 8 + len(lines) * 2 + len(cons) * 28
    print(f"wrote {OUT_BRIDGE.relative_to(ROOT)}: {len(cons)} names")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(order)} stars "
          f"({globe} for the globe), {len(lines)//2} segments, "
          f"{len(cons)} constellations, ~{bytes_//1024}K of flash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
