#!/usr/bin/env python3
"""Scan the local network and drive the clock's radar face.

Runs on the always-on host (tofu), not on either MCU — hard rule 5 keeps the
radio off the beam, and discovery of this kind is exactly the sort of thing the
device has no business doing. It publishes to MQTT and the bridge relays it,
which is the same path nowplaying and gauges already take.

The wire format the bridge expects is deliberately a line rather than JSON:

    name,bearing,range,flags;name,bearing,range,flags|footer

  bearing 0..255 over a full turn, 0 = north, clockwise
  range   0..255 out to the rim
  flags   bit0 new since the last scan, bit1 this host

Two choices worth knowing about:

  * BEARING IS A HASH OF THE MAC, not of the IP. A host keeps its place on the
    face across a DHCP lease change, which is what makes the display readable
    over time — if bearings shuffled every renewal the picture would be noise.
    It falls back to the IP only when no MAC is known.

  * RANGE IS LOG RTT. LAN latencies span 0.2ms to 200ms, three decades, and a
    linear map puts every wired host in a heap at the centre. 60 units per
    decade spreads them over the face and still leaves the slow ones distinct.

No dependencies beyond ping, ip and mosquitto_pub.

Credentials come from the environment (MQTT_HOST/PORT/USER/PASS/PREFIX) so that
nothing secret lands in this repo, which is public. On tofu they live in
/etc/scope-radar.env.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import concurrent.futures
import hashlib
import ipaddress
import json
import math
import os
import re
import socket
import subprocess
import sys
import time

STATE = os.environ.get("RADAR_STATE", "/tmp/scope-radar-seen.json")
MAX_CONTACTS = 12          # rdr::kMax on the device
MAX_LABEL = 13             # rdr::Contact::label holds 13 + NUL


def sh(cmd, timeout=15):
    try:
        return subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout).stdout
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return ""


def local_subnet():
    """The /24 this host sits on, and its own address."""
    out = sh(["ip", "-4", "route", "get", "1.1.1.1"])
    m = re.search(r"\bsrc\s+(\d+\.\d+\.\d+\.\d+)", out)
    if not m:
        return None, None
    me = m.group(1)
    return ipaddress.ip_network(me + "/24", strict=False), me


def ping(ip):
    """One echo. Returns RTT in ms, or None."""
    out = sh(["ping", "-c", "1", "-W", "1", str(ip)], timeout=3)
    m = re.search(r"time[=<]([\d.]+)\s*ms", out)
    return float(m.group(1)) if m else None


def neighbours():
    """ip -> MAC from the kernel's neighbour table."""
    macs = {}
    for line in sh(["ip", "neigh"]).splitlines():
        f = line.split()
        if len(f) >= 5 and f[1] == "dev" and "lladdr" in f:
            macs[f[0]] = f[f.index("lladdr") + 1]
    return macs


def name_for(ip, mac):
    """Something short and human. Reverse DNS, else the last octet."""
    try:
        host = socket.gethostbyaddr(ip)[0]
        host = host.split(".")[0]           # strip the domain; no room for it
        if host:
            return host[:MAX_LABEL]
    except (socket.herror, socket.gaierror, OSError):
        pass
    return ("." + ip.split(".")[-1])[:MAX_LABEL]


def bearing_of(ip, mac):
    """Stable angle. Keyed on the MAC so a new lease does not move the blip."""
    key = (mac or ip).encode()
    return hashlib.sha1(key).digest()[0]


def range_of(rtt):
    """Log RTT -> 0..255. Unreachable-but-present hosts sit at the rim."""
    if rtt is None:
        return 250
    r = 30 + 60 * math.log10(max(rtt, 0.2) / 0.2)
    return max(20, min(250, int(r)))


def load_seen():
    try:
        with open(STATE) as f:
            return set(json.load(f))
    except (OSError, ValueError):
        return set()


def save_seen(keys):
    try:
        with open(STATE, "w") as f:
            json.dump(sorted(keys), f)
    except OSError:
        pass


def scan(net, me, workers):
    hosts = list(net.hosts())
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
        rtts = dict(zip(hosts, ex.map(ping, hosts)))
    macs = neighbours()

    found = []
    for ip, rtt in rtts.items():
        s = str(ip)
        # Present if it answered, or if the kernel has a MAC for it — a host
        # that ignores ICMP is still on the network and still worth drawing.
        if rtt is None and s not in macs:
            continue
        found.append({"ip": s, "mac": macs.get(s), "rtt": rtt})
    return found


def build_line(found, me, seen):
    for h in found:
        h["key"] = h["mac"] or h["ip"]
        h["range"] = range_of(h["rtt"])

    # Nearest first, and keep only what the device can hold. Closest is the
    # useful truncation: the far edge of a big subnet is mostly noise.
    found.sort(key=lambda h: h["range"])
    shown = found[:MAX_CONTACTS]

    recs = []
    for h in shown:
        flags = 0
        if h["key"] not in seen:
            flags |= 1                       # FlagNew
        if h["ip"] == me:
            flags |= 2                       # FlagSelf
        label = name_for(h["ip"], h["mac"])
        # The separators are structural; a hostname containing one would shift
        # every field after it.
        label = label.replace(",", "").replace(";", "").replace("|", "")
        recs.append("%s,%d,%d,%d" % (label, bearing_of(h["ip"], h["mac"]),
                                     h["range"], flags))

    footer = "%s  %d UP" % (str(net_of(me)), len(found))
    return ";".join(recs) + "|" + footer, {h["key"] for h in found}


def net_of(me):
    return ipaddress.ip_network(me + "/24", strict=False)


def publish(line, args):
    topic = "%s/radar/set" % args.prefix.rstrip("/")
    cmd = ["mosquitto_pub", "-h", args.host, "-p", str(args.port),
           "-t", topic, "-m", line]
    if args.user:
        cmd += ["-u", args.user]
    if args.password:
        cmd += ["-P", args.password]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write("publish failed: %s\n" % r.stderr.strip())
    return r.returncode == 0


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default=os.environ.get("MQTT_HOST", "localhost"))
    p.add_argument("--port", type=int, default=int(os.environ.get("MQTT_PORT", 1883)))
    p.add_argument("--user", default=os.environ.get("MQTT_USER", ""))
    p.add_argument("--password", default=os.environ.get("MQTT_PASS", ""))
    p.add_argument("--prefix", default=os.environ.get("MQTT_PREFIX", "scopeclock"))
    p.add_argument("--interval", type=float, default=0,
                   help="seconds between scans; 0 = scan once and exit")
    p.add_argument("--workers", type=int, default=64)
    p.add_argument("--dry-run", action="store_true",
                   help="print the line instead of publishing")
    args = p.parse_args()

    net, me = local_subnet()
    if not net:
        sys.exit("could not work out the local subnet")

    while True:
        t0 = time.time()
        found = scan(net, me, args.workers)
        seen = load_seen()
        line, keys = build_line(found, me, seen)
        save_seen(keys)

        if args.dry_run:
            print(line)
        else:
            publish(line, args)
        if args.interval <= 0:
            return
        time.sleep(max(0.0, args.interval - (time.time() - t0)))


if __name__ == "__main__":
    main()
