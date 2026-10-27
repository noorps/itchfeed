#!/usr/bin/env python3
"""Write a synthetic ITCH 5.0 file for tests.

The output is spec-conformant framing and field layout (2-byte big-endian
length, then the message), but the order flow is made up. It is used for the
CI fixture only. It is not market data.

The generator keeps its own book while it writes, so it can also write the
expected summary (message counts, share totals, final price levels). CI checks
the C++ handler against that, and against tools/itch_ref.py.

    python3 tools/gen_itch.py --messages 50000 --seed 7 \
        --out tests/fixture.itch --expect tests/fixture.expected.json
"""
import argparse
import json
import random
import struct

SYMBOLS = ["AAPL", "MSFT", "AMZN", "TSLA", "SPY", "QQQ", "ZVZZT", "ZXZZT"]


def frame(body):
    return struct.pack(">H", len(body)) + body


def hdr(kind, locate, ts):
    # type, locate, tracking number, 6-byte timestamp
    return kind + struct.pack(">HH", locate, 0) + ts.to_bytes(6, "big")


def sym8(s):
    return s.encode().ljust(8)


class Gen:
    def __init__(self, seed):
        self.rng = random.Random(seed)
        self.ts = 3 * 3600 * 10**9  # 03:00 ET, nanoseconds since midnight
        self.next_ref = 1
        self.next_match = 1
        self.orders = {}  # ref -> [locate, side, shares, price]
        self.out = []
        self.counts = {}
        self.totals = dict(added=0, executed=0, cancelled=0, deleted=0, replaced_out=0, trade_P=0)

    def emit(self, kind, body):
        self.ts += self.rng.randint(1, 50_000)
        self.out.append(frame(body))
        self.counts[kind] = self.counts.get(kind, 0) + 1

    def system(self, code):
        self.emit("S", hdr(b"S", 0, self.ts) + code.encode())

    def directory(self, locate, sym):
        body = (hdr(b"R", locate, self.ts) + sym8(sym) + b"Q" + b"N" + struct.pack(">I", 100)
                + b"N" + b"C" + b"Z " + b"P" + b"N" + b" " + b"1" + b"N" + struct.pack(">I", 0) + b"N")
        assert len(body) == 39
        self.emit("R", body)

    def add(self, locate, base):
        side = self.rng.choice("BS")
        off = self.rng.randint(1, 20) * 100
        price = base - off if side == "B" else base + off
        shares = self.rng.choice([100, 100, 200, 300, 500, 1000]) + self.rng.randint(0, 99)
        ref = self.next_ref
        self.next_ref += self.rng.randint(1, 4)
        self.orders[ref] = [locate, side, shares, price]
        self.totals["added"] += shares
        if self.rng.random() < 0.2:
            body = (hdr(b"F", locate, self.ts) + struct.pack(">Q", ref) + side.encode()
                    + struct.pack(">I", shares) + sym8(SYMBOLS[locate - 1]) + struct.pack(">I", price) + b"GSCO")
            self.emit("F", body)
        else:
            body = (hdr(b"A", locate, self.ts) + struct.pack(">Q", ref) + side.encode()
                    + struct.pack(">I", shares) + sym8(SYMBOLS[locate - 1]) + struct.pack(">I", price))
            self.emit("A", body)

    def reduce(self, ref, n):
        o = self.orders[ref]
        o[2] -= n
        if o[2] == 0:
            del self.orders[ref]

    def update(self):
        ref = self.rng.choice(list(self.orders))
        locate, side, shares, price = self.orders[ref]
        r = self.rng.random()
        if r < 0.25:
            n = self.rng.randint(1, shares)
            self.emit("E", hdr(b"E", locate, self.ts) + struct.pack(">QIQ", ref, n, self.next_match))
            self.next_match += 1
            self.totals["executed"] += n
            self.reduce(ref, n)
        elif r < 0.32:
            n = self.rng.randint(1, shares)
            body = hdr(b"C", locate, self.ts) + struct.pack(">QIQ", ref, n, self.next_match) + b"Y" + struct.pack(">I", price + 1)
            self.emit("C", body)
            self.next_match += 1
            self.totals["executed"] += n
            self.reduce(ref, n)
        elif r < 0.45 and shares > 1:
            n = self.rng.randint(1, shares - 1)
            self.emit("X", hdr(b"X", locate, self.ts) + struct.pack(">QI", ref, n))
            self.totals["cancelled"] += n
            self.reduce(ref, n)
        elif r < 0.80:
            self.emit("D", hdr(b"D", locate, self.ts) + struct.pack(">Q", ref))
            self.totals["deleted"] += shares
            del self.orders[ref]
        else:
            new_ref = self.next_ref
            self.next_ref += 1
            new_shares = self.rng.randint(100, 1500)
            new_price = price + self.rng.choice([-200, -100, 100, 200])
            self.emit("U", hdr(b"U", locate, self.ts) + struct.pack(">QQII", ref, new_ref, new_shares, new_price))
            self.totals["replaced_out"] += shares
            self.totals["added"] += new_shares
            del self.orders[ref]
            self.orders[new_ref] = [locate, side, new_shares, new_price]

    def trade(self, locate, base):
        shares = self.rng.randint(1, 500)
        body = (hdr(b"P", locate, self.ts) + struct.pack(">Q", 0) + self.rng.choice("BS").encode()
                + struct.pack(">I", shares) + sym8(SYMBOLS[locate - 1]) + struct.pack(">I", base)
                + struct.pack(">Q", self.next_match))
        self.next_match += 1
        self.emit("P", body)
        self.totals["trade_P"] += shares

    def run(self, n):
        self.system("O")
        for i, s in enumerate(SYMBOLS, start=1):
            self.directory(i, s)
        self.system("S")
        self.system("Q")
        bases = {i: self.rng.randint(50, 500) * 10000 for i in range(1, len(SYMBOLS) + 1)}
        # Stop with room for the closing system events.
        while len(self.out) < n - 3:
            locate = self.rng.randint(1, len(SYMBOLS))
            r = self.rng.random()
            if r < 0.45 or len(self.orders) < 50:
                self.add(locate, bases[locate])
            elif r < 0.95:
                self.update()
            else:
                self.trade(locate, bases[locate])
        self.system("M")
        self.system("E")
        self.system("C")

    def expected(self, symbols):
        books = {}
        for i, s in enumerate(SYMBOLS, start=1):
            if s not in symbols:
                continue
            levels = {"B": {}, "S": {}}
            for locate, side, shares, price in self.orders.values():
                if locate == i:
                    lv = levels[side].setdefault(price, [0, 0])
                    lv[0] += shares
                    lv[1] += 1
            books[s] = {
                "B": [[p, *levels["B"][p]] for p in sorted(levels["B"], reverse=True)],
                "S": [[p, *levels["S"][p]] for p in sorted(levels["S"])],
            }
        totals = dict(self.totals)
        totals["live"] = sum(o[2] for o in self.orders.values())
        return {
            "messages": len(self.out),
            "counts": dict(sorted(self.counts.items())),
            "totals": totals,
            "live_orders": len(self.orders),
            "last_system_event": "C",
            "books": books,
        }


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--messages", type=int, default=50_000)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--out", required=True)
    ap.add_argument("--expect", help="write the generator's own summary here")
    ap.add_argument("--symbols", default="AAPL,MSFT,AMZN,TSLA,SPY,QQQ")
    a = ap.parse_args()
    g = Gen(a.seed)
    g.run(a.messages)
    with open(a.out, "wb") as f:
        f.write(b"".join(g.out))
    if a.expect:
        with open(a.expect, "w") as f:
            json.dump(g.expected(a.symbols.split(",")), f, indent=2)
            f.write("\n")


if __name__ == "__main__":
    main()
