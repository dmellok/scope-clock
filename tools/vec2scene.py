#!/usr/bin/env python3
"""Turn line art into a scope-clock scene.

    vec2scene.py cat.png                 # trace a raster
    vec2scene.py drawing.svg             # flatten an SVG
    vec2scene.py cat.png --push          # ...and send it over MQTT

Output is the scene text the bridge already understands (see shared/protocol.h):

    L <x0> <y0> <x1> <y1>      C <cx> <cy> <r>      T <x> <y> <scale> <text>

A vector display draws strokes, not areas, so for raster input this traces the
CENTRE-LINE of each stroke rather than the outline of each blob. On pixel art
that matters twice over: outlining would draw every one-pixel line as a long
thin rectangle — double the segments for a fatter, blurrier result — and the
centre-line of a staircased diagonal simplifies to one clean diagonal, which is
what the beam wants to draw anyway.

The item budget is the real constraint: the device holds 128 and the simplifier
is tightened automatically until the drawing fits.
"""
import argparse
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

FIELD = 1150          # half-width of the usable display area, in display units
MAX_ITEMS = 128       # DrawList::CAP on the device


# ---------------------------------------------------------------- raster ----

def load_grid(path, thresh, grid_w, grid_h=0):
    """Recover the pixel-art grid from a raster: a boolean array of cells."""
    from PIL import Image
    import numpy as np

    a = np.array(Image.open(path).convert("L"))
    lit = a > thresh
    ys, xs = np.nonzero(lit)
    if not len(xs):
        sys.exit("nothing above the threshold — try --threshold lower")
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    sub = lit[y0:y1 + 1, x0:x1 + 1]
    h, w = sub.shape

    if grid_w:
        gw = grid_w
        gh = grid_h or max(1, int(round(h / (w / gw))))
    else:
        # Pick the grid that best reconstructs the image, preferring coarser
        # ones: a finer grid always fits better, so without that bias this
        # just returns the raster back.
        best = None
        for cand in range(8, 65):
            ch = max(1, int(round(h / (w / cand))))
            g = _downsample(sub, cand, ch)
            up = _upsample(g, w, h)
            score = (up == sub).mean() - cand * 0.0015
            if best is None or score > best[0]:
                best = (score, cand, ch)
        _, gw, gh = best

    return _downsample(sub, gw, gh)


def _downsample(sub, gw, gh):
    import numpy as np
    h, w = sub.shape
    g = np.zeros((gh, gw), bool)
    for r in range(gh):
        for c in range(gw):
            blk = sub[int(r * h / gh):int((r + 1) * h / gh),
                      int(c * w / gw):int((c + 1) * w / gw)]
            g[r, c] = blk.mean() > 0.45
    return g


def _upsample(g, w, h):
    from PIL import Image
    import numpy as np
    return np.array(Image.fromarray((g * 255).astype("uint8")).resize((w, h),
                    Image.NEAREST)) > 127


def trace_centrelines(g):
    """Lit cells -> polylines through their centres.

    Cells are nodes and adjacency is the edges, so a one-cell-wide stroke
    becomes a path straight down its middle. Diagonal neighbours count, or
    every staircase would come out as a flight of tiny steps.
    """
    gh, gw = g.shape
    lit = {(r, c) for r in range(gh) for c in range(gw) if g[r, c]}

    def neighbours(n):
        r, c = n
        out = []
        for dr, dc in ((-1, 0), (1, 0), (0, -1), (0, 1),
                       (-1, -1), (-1, 1), (1, -1), (1, 1)):
            m = (r + dr, c + dc)
            if m not in lit:
                continue
            # Only take a diagonal in a true staircase, i.e. when BOTH of its
            # orthogonal neighbours are empty. Allowing it whenever they are
            # not *both* filled invents diagonals across dense areas — the
            # whiskers came out as a cat's cradle of little triangles.
            if dr and dc and ((r + dr, c) in lit or (r, c + dc) in lit):
                continue
            out.append(m)
        return out

    edges = set()
    for n in lit:
        for m in neighbours(n):
            edges.add((n, m) if n < m else (m, n))

    unused = set(edges)
    deg = {}
    for a, b in edges:
        deg[a] = deg.get(a, 0) + 1
        deg[b] = deg.get(b, 0) + 1

    def walk(start):
        path = [start]
        cur = start
        while True:
            nxt = None
            for a, b in list(unused):
                if a == cur:
                    nxt = b
                elif b == cur:
                    nxt = a
                else:
                    continue
                unused.discard((a, b))
                break
            if nxt is None:
                return path
            path.append(nxt)
            cur = nxt

    polys = []
    # Ends first, so open strokes come out as one path rather than two halves.
    for n in sorted(lit, key=lambda n: (deg.get(n, 0) != 1, n)):
        while any(n in e for e in unused):
            p = walk(n)
            if len(p) > 1:
                polys.append(p)
    return polys


# ------------------------------------------------------------------- svg ----

def svg_polys(path, steps=12):
    """Flatten an SVG's drawable elements into polylines in user units."""
    tree = ET.parse(path)
    polys = []
    num = re.compile(r"[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?")

    def bez3(p0, p1, p2, p3):
        return [(
            (1 - t) ** 3 * p0[0] + 3 * (1 - t) ** 2 * t * p1[0] + 3 * (1 - t) * t * t * p2[0] + t ** 3 * p3[0],
            (1 - t) ** 3 * p0[1] + 3 * (1 - t) ** 2 * t * p1[1] + 3 * (1 - t) * t * t * p2[1] + t ** 3 * p3[1],
        ) for t in [i / steps for i in range(1, steps + 1)]]

    def bez2(p0, p1, p2):
        return [(
            (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
            (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1],
        ) for t in [i / steps for i in range(1, steps + 1)]]

    for el in tree.iter():
        tag = el.tag.split("}")[-1]
        if tag == "line":
            polys.append([(float(el.get("x1", 0)), float(el.get("y1", 0))),
                          (float(el.get("x2", 0)), float(el.get("y2", 0)))])
        elif tag in ("polyline", "polygon"):
            pts = [float(v) for v in num.findall(el.get("points", ""))]
            p = list(zip(pts[0::2], pts[1::2]))
            if tag == "polygon" and p:
                p.append(p[0])
            if len(p) > 1:
                polys.append(p)
        elif tag == "rect":
            x, y = float(el.get("x", 0)), float(el.get("y", 0))
            w, h = float(el.get("width", 0)), float(el.get("height", 0))
            polys.append([(x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)])
        elif tag == "path":
            d = el.get("d", "")
            toks = re.findall(r"[MmLlHhVvCcQqZz]|[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?", d)
            i, cur, start, cmd, poly = 0, (0.0, 0.0), (0.0, 0.0), None, []
            while i < len(toks):
                t = toks[i]
                if re.match(r"[A-Za-z]", t):
                    cmd = t
                    i += 1
                    if cmd in "Zz":
                        if poly:
                            poly.append(start)
                            polys.append(poly)
                            poly = []
                        cur = start
                    continue
                f = lambda k: float(toks[i + k])
                rel = cmd.islower()
                bx, by = cur if rel else (0.0, 0.0)
                if cmd in "Mm":
                    cur = (bx + f(0), by + f(1)); i += 2
                    if poly:
                        polys.append(poly)
                    poly = [cur]; start = cur
                    cmd = "l" if rel else "L"
                elif cmd in "Ll":
                    cur = (bx + f(0), by + f(1)); i += 2; poly.append(cur)
                elif cmd in "Hh":
                    cur = (bx + f(0), cur[1]); i += 1; poly.append(cur)
                elif cmd in "Vv":
                    cur = (cur[0], by + f(0)); i += 1; poly.append(cur)
                elif cmd in "Cc":
                    p1 = (bx + f(0), by + f(1)); p2 = (bx + f(2), by + f(3))
                    p3 = (bx + f(4), by + f(5)); i += 6
                    poly += bez3(cur, p1, p2, p3); cur = p3
                elif cmd in "Qq":
                    p1 = (bx + f(0), by + f(1)); p2 = (bx + f(2), by + f(3)); i += 4
                    poly += bez2(cur, p1, p2); cur = p2
                else:
                    i += 1
            if poly:
                polys.append(poly)
    return [p for p in polys if len(p) > 1]


# -------------------------------------------------------------- simplify ----

def rdp(points, eps):
    """Douglas-Peucker: drop points that do not change the shape."""
    if len(points) < 3:
        return points
    x0, y0 = points[0]
    x1, y1 = points[-1]
    dx, dy = x1 - x0, y1 - y0
    norm = math.hypot(dx, dy)
    worst, idx = -1.0, 0
    for i in range(1, len(points) - 1):
        px, py = points[i]
        d = (abs(dy * px - dx * py + x1 * y0 - y1 * x0) / norm) if norm else math.hypot(px - x0, py - y0)
        if d > worst:
            worst, idx = d, i
    if worst <= eps:
        return [points[0], points[-1]]
    return rdp(points[:idx + 1], eps)[:-1] + rdp(points[idx:], eps)


def fit(polys, field):
    """Scale and centre into the display field, flipping y (screen -> tube)."""
    xs = [p[0] for poly in polys for p in poly]
    ys = [p[1] for poly in polys for p in poly]
    w, h = max(xs) - min(xs), max(ys) - min(ys)
    s = min((2 * field) / w if w else 1e9, (2 * field) / h if h else 1e9)
    cx, cy = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    return [[(round((x - cx) * s), round((cy - y) * s)) for x, y in poly]
            for poly in polys]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("--threshold", type=int, default=90, help="raster: lit above this")
    ap.add_argument("--grid", type=int, default=0, help="raster: force grid width")
    ap.add_argument("--grid-h", type=int, default=0, help="raster: force grid height")
    ap.add_argument("--field", type=int, default=FIELD)
    ap.add_argument("--max-items", type=int, default=MAX_ITEMS)
    ap.add_argument("--preview", metavar="FILE.svg", help="write an SVG of the result")
    ap.add_argument("--push", action="store_true", help="publish over MQTT")
    ap.add_argument("--topic", default="scopeclock/scene/set")
    args = ap.parse_args()

    if args.input.lower().endswith(".svg"):
        polys = svg_polys(args.input)
    else:
        g = load_grid(args.input, args.threshold, args.grid, args.grid_h)
        sys.stderr.write("grid %dx%d\n" % (g.shape[1], g.shape[0]))
        polys = [[(c, r) for r, c in p] for p in trace_centrelines(g)]

    if not polys:
        sys.exit("nothing to draw")

    # Tighten the simplifier until it fits. Reporting this matters: silently
    # dropping half a drawing would look like a bug in the display.
    eps = 0.35
    for _ in range(40):
        simp = [rdp(p, eps) for p in polys]
        segs = sum(len(p) - 1 for p in simp)
        if segs <= args.max_items:
            break
        eps *= 1.25
    else:
        sys.stderr.write("warning: still %d segments, will be truncated\n" % segs)

    out = fit(simp, args.field)
    lines = []
    for poly in out:
        for (x0, y0), (x1, y1) in zip(poly, poly[1:]):
            if (x0, y0) != (x1, y1):
                lines.append("L %d %d %d %d" % (x0, y0, x1, y1))
    scene = "\n".join(lines[:args.max_items])
    sys.stderr.write("%d segments (eps %.2f)\n" % (len(lines), eps))

    if args.preview:
        with open(args.preview, "w") as f:
            f.write("<svg xmlns='http://www.w3.org/2000/svg' width='620' height='620' "
                    "viewBox='-1250 -1250 2500 2500'><rect x='-1250' y='-1250' "
                    "width='2500' height='2500' fill='#000'/>")
            for ln in lines[:args.max_items]:
                _, x0, y0, x1, y1 = ln.split()
                f.write("<line x1='%s' y1='%s' x2='%s' y2='%s' stroke='#39ff88' "
                        "stroke-width='14'/>" % (x0, -int(y0), x1, -int(y1)))
            f.write("</svg>\n")

    if args.push:
        import paho.mqtt.client as mqtt
        host = os.environ.get("MQTT_HOST"); user = os.environ.get("MQTT_USER")
        pw = os.environ.get("MQTT_PASS"); port = int(os.environ.get("MQTT_PORT", "1883"))
        if not host:
            sys.exit("set MQTT_HOST (and MQTT_USER/MQTT_PASS) to push")
        c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        if user:
            c.username_pw_set(user, pw)
        c.connect(host, port, 10)
        c.loop_start(); c.publish(args.topic, scene); import time; time.sleep(3)
        c.loop_stop(); c.disconnect()
        sys.stderr.write("pushed to %s\n" % args.topic)
    else:
        print(scene)


if __name__ == "__main__":
    main()
