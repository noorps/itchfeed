#!/usr/bin/env python3
"""Independent reference decoder for ITCH 5.0 files.

Written separately from the C++ handler (plain dicts, struct.unpack_from) so a
shared bug is unlikely. It walks the 2-byte length framing, counts every
message type, rebuilds the book for all symbols, and prints the same JSON
summary as `itchfeed`. tools/compare.py diffs the two.

    python3 tools/itch_ref.py FILE [--symbols AAPL,MSFT] [--limit N]

FILE may be "-" for stdin, or end in .gz.
"""
import argparse
import gzip
import json
import struct
import sys

LENGTHS = {
    "S": 12, "R": 39, "H": 25, "Y": 20, "L": 26, "V": 35, "W": 12, "K": 28, "J": 35, "h": 21,
    "A": 36, "F": 40, "E": 31, "C": 36, "X": 23, "D": 19, "U": 35, "P": 44, "Q": 40, "B": 19,
    "I": 50, "N": 20, "O": 48,
}


def frames(f, limit):
    n = 0
    buf = b""
    pos = 0
    while limit is None or n < limit:
        if len(buf) - pos < 2:
            chunk = f.read(1 << 20)
            if not chunk:
                return
            buf = buf[pos:] + chunk
            pos = 0
            continue
        (ln,) = struct.unpack_from(">H", buf, pos)
        if len(buf) - pos < 2 + ln:
            chunk = f.read(1 << 20)
            if not chunk:
                return
            buf = buf[pos:] + chunk
            pos = 0
            continue
        yield buf, pos + 2, ln
        pos += 2 + ln
        n += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--symbols", default="AAPL,MSFT,AMZN,TSLA,SPY,QQQ")
    ap.add_argument("--limit", type=int, help="stop after N messages")
    a = ap.parse_args()

    if a.file == "-":
        f = sys.stdin.buffer
    elif a.file.endswith(".gz"):
        f = gzip.open(a.file, "rb")
    else:
        f = open(a.file, "rb")

    counts = {}
    total = 0
    bad_length = 0
    orders = {}  # ref -> [locate, side, shares, price]
    names = {}   # locate -> symbol
    t = dict(added=0, executed=0, cancelled=0, deleted=0, replaced_out=0, trade_P=0)
    unknown = 0
    last_event = "?"

    def reduce(ref, n, key):
        nonlocal unknown
        o = orders.get(ref)
        if o is None:
            unknown += 1
            return
        n = min(n, o[2])
        o[2] -= n
        t[key] += n
        if o[2] == 0:
            del orders[ref]

    for buf, p, ln in frames(f, a.limit):
        kind = chr(buf[p])
        counts[kind] = counts.get(kind, 0) + 1
        total += 1
        if LENGTHS.get(kind) != ln:
            bad_length += 1
            continue
        if kind in "AF":
            locate, = struct.unpack_from(">H", buf, p + 1)
            ref, side, shares, price = struct.unpack_from(">Qc I 8x I".replace(" ", ""), buf, p + 11)
            orders[ref] = [locate, side.decode(), shares, price]
            t["added"] += shares
        elif kind in "EC":
            ref, n = struct.unpack_from(">QI", buf, p + 11)
            reduce(ref, n, "executed")
        elif kind == "X":
            ref, n = struct.unpack_from(">QI", buf, p + 11)
            reduce(ref, n, "cancelled")
        elif kind == "D":
            (ref,) = struct.unpack_from(">Q", buf, p + 11)
            o = orders.pop(ref, None)
            if o is None:
                unknown += 1
            else:
                t["deleted"] += o[2]
        elif kind == "U":
            old, new, shares, price = struct.unpack_from(">QQII", buf, p + 11)
            o = orders.pop(old, None)
            if o is None:
                unknown += 1
            else:
                t["replaced_out"] += o[2]
                orders[new] = [o[0], o[1], shares, price]
                t["added"] += shares
        elif kind == "P":
            (shares,) = struct.unpack_from(">I", buf, p + 20)
            t["trade_P"] += shares
        elif kind == "S":
            last_event = chr(buf[p + 11])
        elif kind == "R":
            locate, = struct.unpack_from(">H", buf, p + 1)
            names[locate] = buf[p + 11:p + 19].decode().rstrip(" ")

    wanted = a.symbols.split(",")
    by_name = {v: k for k, v in names.items()}
    books = {}
    for s in wanted:
        levels = {"B": {}, "S": {}}
        loc = by_name.get(s)
        if loc is not None:
            for locate, side, shares, price in orders.values():
                if locate == loc:
                    lv = levels[side].setdefault(price, [0, 0])
                    lv[0] += shares
                    lv[1] += 1
        books[s] = {
            "B": [[p, *levels["B"][p]] for p in sorted(levels["B"], reverse=True)],
            "S": [[p, *levels["S"][p]] for p in sorted(levels["S"])],
        }
    t["live"] = sum(o[2] for o in orders.values())
    out = {
        "messages": total,
        "counts": dict(sorted(counts.items())),
        "totals": t,
        "live_orders": len(orders),
        "last_system_event": last_event,
        "books": books,
        "unknown_ref": unknown,
        "bad_length": bad_length,
    }
    json.dump(out, sys.stdout, indent=2)
    print()


if __name__ == "__main__":
    main()
