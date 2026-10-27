#!/usr/bin/env python3
"""Collect itchbench output and summarise it across runs.

    itchbench ... | python3 tools/summarize.py append RUN >> runs.jsonl
    python3 tools/summarize.py table runs.jsonl

The table reports, for each config, the median across runs and the min-max
range across runs. Nothing is averaged within a run: each run's p50/p99/p99.9
/max comes straight from that run's full sample set.
"""
import json
import statistics
import sys

STATS = [("p50", "p50"), ("p99", "p99"), ("p99.9", "p99.9"), ("max", "max")]


def config_key(d):
    k = f"{d['ingest']} / {d['book']}"
    if d["ingest"] == "read" and d.get("read_buf", 65536) != 65536:
        k += f" / buf {d['read_buf']}"
    if d["ingest"] == "mmap" and d.get("populate"):
        k += " / populate"
    return k


def fmt_range(vals, digits=1):
    med = statistics.median(vals)
    return f"{med:.{digits}f} [{min(vals):.{digits}f}-{max(vals):.{digits}f}]"


def append(run):
    d = json.load(sys.stdin)
    d["run"] = int(run)
    print(json.dumps(d))


def table(path):
    rows = [json.loads(line) for line in open(path) if line.strip()]
    by = {}
    for d in rows:
        by.setdefault(config_key(d), []).append(d)
    msgs = {d["messages"] for d in rows}
    print(f"messages per run: {', '.join(str(m) for m in sorted(msgs))}")
    print(f"TSC: {fmt_range([d['tsc_ghz'] for d in rows], 4)} GHz (calibrated per run against CLOCK_MONOTONIC)")
    print(f"timer floor p50: {fmt_range([d['timer_floor_ns']['p50'] for d in rows])} ns "
          "(included in every latency sample, not subtracted)")
    print()
    print("Per-message latency in ns, median across runs [min-max across runs]:")
    print()
    print("| config | runs | p50 | p99 | p99.9 | max | throughput (M msgs/s) |")
    print("|---|---|---|---|---|---|---|")
    for k, ds in by.items():
        cells = [fmt_range([d["latency_ns"][s] for d in ds]) for _, s in STATS]
        tp = fmt_range([d["throughput_msgs_per_sec"] / 1e6 for d in ds], 2)
        print(f"| {k} | {len(ds)} | " + " | ".join(cells) + f" | {tp} |")
    print()
    print("Per run:")
    print()
    print("| config | run | cpu | timer floor | p50 | p99 | p99.9 | p99.99 | max | max on | read() calls | M msgs/s |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for k, ds in by.items():
        for d in sorted(ds, key=lambda x: x["run"]):
            l = d["latency_ns"]
            m = d["max_at"]
            on = m["type"] + (" + read()" if m["read_call"] else "")
            print(f"| {k} | {d['run']} | {d['cpu_start']}->{d['cpu_end']} | {d['timer_floor_ns']['p50']:.1f} | {l['p50']:.1f} | {l['p99']:.1f} | "
                  f"{l['p99.9']:.1f} | {l['p99.99']:.1f} | {l['max']:.1f} | {on} | {d['read_calls']} | "
                  f"{d['throughput_msgs_per_sec'] / 1e6:.2f} |")
    print()
    print("read() path, samples split by whether a read() call landed on that message (medians across runs):")
    print()
    for k, ds in by.items():
        w = [d for d in ds if "with_read_call" in d]
        if not w:
            continue
        print(f"- {k}: {statistics.median(d['with_read_call']['n'] for d in w):.0f} messages carried a read(), "
              f"p50 {statistics.median(d['with_read_call']['p50'] for d in w):.1f} ns; the rest p50 "
              f"{statistics.median(d['without_read_call']['p50'] for d in w):.1f} ns, p99.9 "
              f"{statistics.median(d['without_read_call']['p99.9'] for d in w):.1f} ns")
    print()
    print("By message type, p50 / p99 ns (median across runs):")
    print()
    types = sorted({t for d in rows for t in d["by_type_ns"]})
    print("| config | " + " | ".join(types) + " |")
    print("|---|" + "---|" * len(types))
    for k, ds in by.items():
        cells = []
        for t in types:
            v = [d["by_type_ns"][t] for d in ds if t in d["by_type_ns"]]
            if not v:
                cells.append("-")
                continue
            cells.append(f"{statistics.median(x['p50'] for x in v):.0f} / {statistics.median(x['p99'] for x in v):.0f}")
        print(f"| {k} | " + " | ".join(cells) + " |")


if __name__ == "__main__":
    if sys.argv[1] == "append":
        append(sys.argv[2])
    else:
        table(sys.argv[2])
