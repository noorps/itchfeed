#!/usr/bin/env python3
"""Compare two correctness summaries (itchfeed, itch_ref.py, or gen_itch.py).

Compares the fields both files have: message count, per-type counts, share
totals, live orders, last system event and the full price levels of every
listed symbol. Exits non-zero on any difference.

    python3 tools/compare.py a.json b.json
"""
import json
import sys

KEYS = ["messages", "counts", "totals", "live_orders", "last_system_event", "books"]


def main():
    a_path, b_path = sys.argv[1], sys.argv[2]
    a = json.load(open(a_path))
    b = json.load(open(b_path))
    bad = 0
    for k in KEYS:
        if k not in a or k not in b:
            continue
        if a[k] != b[k]:
            bad += 1
            print(f"MISMATCH {k}:\n  {a_path}: {json.dumps(a[k])[:400]}\n  {b_path}: {json.dumps(b[k])[:400]}")
    for path, d in ((a_path, a), (b_path, b)):
        if d.get("checks_passed") is False:
            bad += 1
            print(f"{path}: book checks failed: {d.get('failures')}")
        if d.get("unknown_ref"):
            bad += 1
            print(f"{path}: {d['unknown_ref']} updates referenced unknown orders")
    levels = sum(len(v["B"]) + len(v["S"]) for v in a.get("books", {}).values())
    if bad:
        print(f"{bad} difference(s)")
        sys.exit(1)
    print(f"match: {a['messages']} messages, {len(a['counts'])} types, {levels} price levels compared")


if __name__ == "__main__":
    main()
