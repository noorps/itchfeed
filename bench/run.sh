#!/bin/sh
# Reproduce the latency/throughput numbers.
#
#   bench/run.sh FILE LABEL
#
# Builds in Release, records the hardware, then runs itchbench RUNS times for
# each config, pinned to CPU with taskset. Configs are interleaved round by
# round (read, mmap, read, mmap, ...) so slow drift on the machine hits both
# paths equally. Each run is a separate process. Results land in
# bench/results/LABEL/.
#
# Environment: RUNS (default 5), CPU (default 2), CONFIGS (default
# "read:std mmap:std", each INGEST:BOOK), EXTRA (extra itchbench flags).
set -eu
file=${1:?usage: bench/run.sh FILE LABEL}
label=${2:?usage: bench/run.sh FILE LABEL}
RUNS=${RUNS:-5}
CPU=${CPU:-2}
CONFIGS=${CONFIGS:-"read:std mmap:std"}
EXTRA=${EXTRA:-}

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)" >/dev/null

out=bench/results/$label
mkdir -p "$out"
sh bench/hwinfo.sh "$CPU" > "$out/hardware.txt"
{
    echo "file: $file ($(stat -c %s "$file") bytes, sha256 $(sha256sum "$file" | cut -c1-16)...)"
    echo "runs: $RUNS  cpu: $CPU  configs: $CONFIGS  extra: ${EXTRA:-none}"
    echo "commit: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)$(git diff --quiet 2>/dev/null || echo ' (dirty)')"
} > "$out/setup.txt"
: > "$out/runs.jsonl"

for r in $(seq 1 "$RUNS"); do
    for cfg in $CONFIGS; do
        ingest=${cfg%%:*}
        book=${cfg#*:}
        echo "run $r/$RUNS: $ingest $book" >&2
        # shellcheck disable=SC2086
        taskset -c "$CPU" build/itchbench "$file" --ingest "$ingest" --book "$book" $EXTRA |
            python3 tools/summarize.py append "$r" >> "$out/runs.jsonl"
    done
done

python3 tools/summarize.py table "$out/runs.jsonl" > "$out/summary.md"
cat "$out/summary.md"
