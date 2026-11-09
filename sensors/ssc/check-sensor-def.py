#!/usr/bin/env python3
#
# Copyright (C) 2026 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#
"""Check sensor_def_qcomdev.conf against the sensor daemon and Windows.

The registry item table (item id -> type, offset in sns.reg) is read from
the sensor daemon binary itself (sensors.qcom, or qcSensors8974.dll, the
Windows build of the same daemon). The conf items are then laid out the way
sensors.qcom writes them into /data/misc/sensors/sns.reg, and the SSI SMGR
(group 2690, offset 0x1700) and SSI GPIO (group 2800, offset 0x1f00) images
are compared with:

  --inf FILE      qcSensors8974.inf ([WdfExt_AddReg] values "23" and "31")
  --hive FILE     a Windows SOFTWARE hive (needs --regf DIR, the directory
                  holding the bandit_installer package with regf.py)
  --snsreg FILE   an sns.reg pulled from the phone

Nothing private is stored here: the Windows files are only read.

Example:
  check-sensor-def.py --daemon .../vendor/bin/sensors.qcom \
      --conf sensors/ssc/sensor_def_qcomdev.conf --hive .../SOFTWARE \
      --regf .../installer
"""

import argparse
import re
import struct
import sys

SMGR_OFF, SMGR_LEN = 0x1700, 256
GPIO_OFF, GPIO_LEN = 0x1f00, 34
WIN_GPIO_LEN = 12

# Item type -> size, from the offsets of consecutive items in the table
TYPE_SIZE = {0: 1, 1: 2, 2: 4, 3: 8, 4: 1, 5: 2, 6: 4, 7: 8, 9: 4}

WIN_KEY = ("Microsoft", "Windows NT", "CurrentVersion", "WUDF", "Services",
           "qcSensors", "Parameters")


def item_table(path):
    """Returns {item: (type, offset)} from a daemon binary.

    Records are five little-endian u32: type, offset, flags, version, id.
    The record of item 1902 (SSI SMGR cfg 0 UUID high, u64 at 0x1710) is
    the anchor; the table is walked both ways from it.
    """
    data = open(path, "rb").read()
    anchor = data.find(struct.pack("<5I", 3, 0x1710, 2, 6, 1902))
    if anchor < 0:
        sys.exit(f"{path}: registry item table not found")

    def valid(rec):
        return rec[0] < 16 and rec[1] < 0x10000 and rec[2] < 16 and rec[4] < 70000

    start = anchor
    while start >= 20 and valid(struct.unpack_from("<5I", data, start - 20)):
        start -= 20
    items = {}
    pos = start
    while pos + 20 <= len(data):
        rec = struct.unpack_from("<5I", data, pos)
        if not valid(rec):
            break
        items[rec[4]] = (rec[0], rec[1])
        pos += 20
    return items


def parse_conf(path):
    """Returns [(item, value)] in file order (metadata lines skipped)."""
    out = []
    for n, line in enumerate(open(path), 1):
        line = line.split("#", 1)[0].strip()
        if not line or line.startswith(":"):
            continue
        fields = line.split()
        if len(fields) < 3:
            sys.exit(f"{path}:{n}: expected <item> <value> <version>")
        out.append((int(fields[0], 0), int(fields[1], 0)))
    return out


def build_image(items, conf):
    image = bytearray(0x4000)
    missing = []
    for item, value in conf:
        if item not in items:
            missing.append(item)
            continue
        typ, off = items[item]
        size = TYPE_SIZE.get(typ)
        if size is None:
            sys.exit(f"item {item}: unknown type {typ}")
        value &= (1 << (8 * size)) - 1  # strtoull wraps negative values
        image[off:off + size] = value.to_bytes(size, "little")
    if missing:
        print(f"note: {len(missing)} items not in this daemon's table (newer than it): "
              + " ".join(map(str, missing)))
    return image


def windows_from_inf(path):
    lines = open(path, "rb").read().decode("utf-16").splitlines()
    values = {}
    for name in ("23", "31"):
        for i, line in enumerate(lines):
            if '"%s"' % name in line and "0x1," in line:
                body = line.split("0x1,", 1)[1]
                while body.rstrip().endswith("\\") and i + 1 < len(lines):
                    i += 1
                    body = body.rstrip()[:-1] + " " + lines[i]
                values[name] = bytes(int(h, 16) for h in re.findall(r"\b([0-9A-Fa-f]{2})\b", body))
                break
        else:
            sys.exit(f"{path}: value {name} not found")
    return values


def windows_from_hive(path, regf_dir):
    sys.path.insert(0, regf_dir)
    from bandit_installer.regf import Hive  # noqa: E402
    key = Hive.open(path).root.path(*WIN_KEY)
    if key is None:
        sys.exit(f"{path}: qcSensors\\Parameters not found")
    return {name: key.value(name)[1] for name in ("23", "31")}


def compare(label, got, want):
    if got == want:
        print(f"OK    {label}: {len(want)} bytes identical")
        return True
    print(f"DIFF  {label}:")
    for i in range(0, max(len(got), len(want)), 16):
        a, b = got[i:i + 16], want[i:i + 16]
        if a != b:
            print(f"  @0x{i:02x} conf    {a.hex(' ')}")
            print(f"  @0x{i:02x} other   {b.hex(' ')}")
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--daemon", required=True)
    ap.add_argument("--conf", required=True)
    ap.add_argument("--inf")
    ap.add_argument("--hive")
    ap.add_argument("--regf")
    ap.add_argument("--snsreg")
    args = ap.parse_args()

    items = item_table(args.daemon)
    print(f"{args.daemon}: {len(items)} registry items")
    image = build_image(items, parse_conf(args.conf))
    smgr = bytes(image[SMGR_OFF:SMGR_OFF + SMGR_LEN])
    gpio = bytes(image[GPIO_OFF:GPIO_OFF + GPIO_LEN])
    ok = True

    sources = []
    if args.inf:
        sources.append(("INF", windows_from_inf(args.inf)))
    if args.hive:
        if not args.regf:
            sys.exit("--hive needs --regf")
        sources.append(("hive", windows_from_hive(args.hive, args.regf)))
    for label, win in sources:
        ok &= compare(f"{label} \"23\" vs SSI SMGR config", smgr, win["23"])
        ok &= compare(f"{label} \"31\" vs SSI GPIO config", gpio[:WIN_GPIO_LEN], win["31"])

    if args.snsreg:
        reg = open(args.snsreg, "rb").read()
        ok &= compare("sns.reg SSI SMGR config", smgr, reg[SMGR_OFF:SMGR_OFF + SMGR_LEN])
        ok &= compare("sns.reg SSI GPIO config", gpio, reg[GPIO_OFF:GPIO_OFF + GPIO_LEN])
        for item in (700, 701, 702, 800, 801, 802, 900, 901, 902):
            off = items[item][1]
            if reg[off:off + 1] != image[off:off + 1]:
                print(f"DIFF  sns.reg item {item}: {reg[off]:#x}, conf {image[off]:#x}")
                ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
