#!/bin/sh
# Hardware counters for each ingest path with perf stat.
#
#   bench/perf.sh FILE LABEL
#
# Counting is enabled only around itchbench's throughput pass (perf --control
# plus PERF_CTL_FD/PERF_ACK_FD), so the warmup, the latency pass and the
# percentile sorting are not in these numbers. Writes
# bench/results/LABEL/perf_<ingest>.txt.
set -eu
file=${1:?usage: bench/perf.sh FILE LABEL}
label=${2:?usage: bench/perf.sh FILE LABEL}
CPU=${CPU:-2}
BOOK=${BOOK:-std}
RUNS=${RUNS:-5}
out=bench/results/$label
mkdir -p "$out"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)" >/dev/null

events=task-clock,cycles,instructions,cache-references,cache-misses,branches,branch-misses,L1-dcache-load-misses,LLC-load-misses,page-faults,context-switches,cpu-migrations

tmp=$(mktemp -d)
mkfifo "$tmp/ctl" "$tmp/ack"
exec 8<>"$tmp/ctl" 9<>"$tmp/ack"
for ingest in read mmap; do
    echo "perf stat: $ingest ($RUNS repeats)" >&2
    PERF_CTL_FD=8 PERF_ACK_FD=9 perf stat -e "$events" --delay=-1 --control fd:8,9 -r "$RUNS" \
        -o "$out/perf_$ingest.txt" \
        taskset -c "$CPU" build/itchbench "$file" --ingest "$ingest" --book "$BOOK" > /dev/null
    cat "$out/perf_$ingest.txt"
done
rm -rf "$tmp"
