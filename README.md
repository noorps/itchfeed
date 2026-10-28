# itchfeed

itchfeed reads Nasdaq TotalView-ITCH 5.0 files, rebuilds the order book, and
reports how long each message took. I built it to put real latency numbers on a
feed handler and to be able to say exactly how they were measured. Parsing
ITCH is not hard; defending a p99 is the part I wanted to practice.

The parser handles S, R, A, F, E, C, X, D, U and P and counts every other type
without decoding it. The book tracks every order by reference number and every
price level by (stock, side, price), for all symbols in the file. Two ingest
paths feed the same parser: `read()` into a 64 KiB buffer, and `mmap` of the
whole file. Two book implementations sit behind the same parser: one on
`std::unordered_map` and `std::map`, and one on two open-addressed tables that
never allocate after startup.

The input is real Nasdaq sample data: the 2019-12-30 TotalView-ITCH 5.0 file
Nasdaq publishes at `emi.nasdaq.com/ITCH/Nasdaq%20ITCH/12302019.NASDAQ_ITCH50.gz`
(3.5 GB compressed). The benchmark runs on its first 25,000,000 messages, cut
on a frame boundary (sha256 `e9133f5f...`). The only synthetic data in the repo
is a 50,000-message fixture that CI generates for a quick correctness check; no
number below came from it.

## the numbers

Per-message latency on the first 25,000,000 messages of the 2019-12-30 sample
file, one process pinned to one core, five runs per configuration. Each cell is
the median across the five runs with the min-max across runs in brackets. The
time per message includes the timer's own 20 ns floor. `std` is the baseline
book on `std::unordered_map` and `std::map`; `flat` is the open-addressed book.

GitHub-hosted runner, AMD EPYC 9V74 vCPU, `taskset -c 1`
([raw results](bench/results/03-gha-runner/summary.md)):

| ingest / book | p50 ns | p99 ns | p99.9 ns | max ns | throughput M msgs/s |
|---|---|---|---|---|---|
| read / std | 200.3 [180.3-220.3] | 1472.2 [1241.8-1762.6] | 2223.3 [1962.9-2704.0] | 68892 [57926-3687088] | 3.71 [3.20-4.37] |
| mmap / std | 190.3 [190.3-210.3] | 1362.0 [1211.8-1622.4] | 1912.8 [1762.6-2193.3] | 79618 [67630-94390] | 3.69 [2.98-4.02] |
| read / flat | 170.3 [170.3-180.3] | 420.6 [410.6-470.7] | 600.9 [590.9-701.0] | 74731 [57315-242330] | 13.51 [12.57-13.71] |
| mmap / flat | 170.3 [170.3-180.3] | 430.6 [410.6-450.7] | 560.8 [540.8-590.9] | 62743 [54942-170533] | 13.69 [12.76-13.82] |

My laptop, i7-1355U inside Docker Desktop's WSL2 VM, `taskset -c 2`, with a
browser open on the host ([raw results](bench/results/02-flat-vs-std/summary.md)):

| ingest / book | p50 ns | p99 ns | p99.9 ns | max ns | throughput M msgs/s |
|---|---|---|---|---|---|
| read / std | 308.7 [223.7-455.0] | 2877.2 [1486.1-3823.3] | 15526 [6390-32246] | 6985504 [1817644-10386934] | 1.97 [1.70-3.08] |
| mmap / std | 284.2 [268.9-339.4] | 2498.8 [2295.8-2926.2] | 5869 [5472-14565] | 5172809 [2240990-8506606] | 2.15 [2.04-2.88] |
| read / flat | 187.7 [150.9-232.9] | 848.8 [422.8-898.6] | 4684 [785-6901] | 5436057 [1221698-8543896] | 8.06 [6.19-13.37] |
| mmap / flat | 182.3 [150.9-248.2] | 705.5 [437.4-914.6] | 1476 [646-4835] | 6404408 [1792335-22371334] | 7.95 [7.05-12.46] |

The runner table is the one to read. Its spread is a few percent at p50 and
p99; the laptop's spread is 2x at p50 because the host was busy, and its max
column is milliseconds of the VM losing the core. A single laptop run with the
host quiet (run 5 of read/flat, run 4 of mmap/flat) lands at 151 ns p50 and
420 to 440 ns p99, the same place as the runner. An earlier run of the same
code on a different runner VM
([run 40592802686](https://github.com/noorps/itchfeed/actions/runs/40592802686))
gave 220 / 1392 ns for std and 170 / 411 ns for flat, so the runner numbers
hold across two VMs.

What the table says:

- **The flat book is the win.** Against the std containers, on the runner, p50
  drops 200 to 170 ns (15%), p99 drops 1472 to 421 ns (71%), p99.9 drops 2223
  to 601 ns (73%), and throughput goes from 3.7 to 13.5 million messages per
  second (3.6x). Allocations inside the timed loop go from 19,083,458 (one per
  add, one per new price level) to 0, and neither table grew during a run.
- **mmap did not beat read() at any percentile I can distinguish.** p50 and
  p99 are inside each other's min-max range on both machines, for both books.
  The read() path's cost is concentrated: 12,192 of the 25M messages carry a
  `read()` syscall (one per 64 KiB buffer refill) and those sit at 5.0 µs p50 on
  the runner, the rest at 170 ns. That is 0.05% of messages, so it shows above
  p99.9 and in the max, not at p99. mmap trades those 12,192 syscalls for 1,309
  page faults. Throughput is 13.51 vs 13.69 M msgs/s, ranges overlapping.
- **The max column is not the parser.** Every run's largest sample is 50 µs to
  several ms, lands on an ordinary order message (A, D, U, F or X in the
  runner runs), and happens on the mmap path too where there is no syscall to
  blame. On a shared VM that is preemption. p99.99, in the per-run tables, is
  9 to 11 µs on the runner for every configuration.

### perf stat

Hardware counters are **not available on either machine**. Both are VMs and
neither exposes a PMU: the WSL2 kernel has none, and on the Azure runner `perf
stat` reports `<not supported>` for cycles, instructions, cache-references,
cache-misses, branches and branch-misses. The output is committed in
`bench/results/03-gha-runner/perf_read.txt` and `perf_mmap.txt`. What it did
count, over the throughput pass of the flat book, 5 repeats each:

| ingest | task-clock | page-faults | context-switches | cpu-migrations |
|---|---|---|---|---|
| read | 1852 ms (±0.6%) | 0 | 13 | 0 |
| mmap | 1802 ms (±0.5%) | 1309 | 13 | 0 |

So I cannot report instructions per cycle, cache misses or branch misses. I am
not estimating them. `bench/perf.sh` requests them and will record them on a
machine that has a PMU.

## hardware

| | GitHub runner | laptop |
|---|---|---|
| CPU | AMD EPYC 9V74 80-core (2 vCPUs visible, 1 core 2 threads) | 13th Gen Intel Core i7-1355U (12 logical CPUs visible) |
| pinned to | cpu 1 | cpu 2 |
| cache | L1d 32 KiB, L2 1 MiB, L3 32 MiB | L1d 48 KiB/core, L2 1.25 MiB/core, L3 12 MiB |
| RAM | 7.8 GiB | 7.6 GiB in the VM, 16 GB on the host |
| kernel | Linux 6.17.0-1022-azure | Linux 6.18.33.2-microsoft-standard-WSL2 |
| virtualisation | Hyper-V (Azure), `hypervisor` flag set | Hyper-V (WSL2), `hypervisor` flag set |
| TSC | 2.5961 GHz, constant_tsc nonstop_tsc rdtscp | 2.6109 GHz, constant_tsc nonstop_tsc rdtscp |
| compiler | g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) | same, inside the `ops/dev.Dockerfile` image |
| flags | `-O2 -g -march=native`, C++20 | same |
| cpufreq governor | not exposed | not exposed |

The full `lscpu` for each is in the `hardware.txt` next to each result set.
`-g` adds debug info only. I did not use `-O3` or LTO; `-O2` is what I measured.

## how I measured

`itchbench` is one run in one process. It does, in order:

1. Calibrate `rdtscp` against `CLOCK_MONOTONIC` over 500 ms. The rate is
   printed with every run (2.61 GHz on the laptop, 2.60 GHz on the runner) and
   used to convert cycles to nanoseconds. Both machines report `constant_tsc`
   and `nonstop_tsc`.
2. Measure the timer's own floor: a million back-to-back `rdtscp; lfence`
   reads with the same store pattern as the real loop. That floor is 14 to
   30 ns depending on the machine and run (20 ns in the runner table above)
   and it is **included in every latency sample, not subtracted**. A
   number below is time from the end of one message to the end of the next,
   as seen by the timer.
3. Warm up with one full untimed pass over the file, then clear the book.
   Nothing from this pass is reported. After it the page cache is warm for
   both ingest paths.
4. Throughput pass: open the file again, parse everything with no per-message
   timing, wall clock from `CLOCK_MONOTONIC`. This is the pass `perf stat`
   counts (it is enabled and disabled around it over a control fd), so the
   hardware counters cover parsing only, not warmup, not sorting.
5. Latency pass: open the file again, one `rdtscp` after every message, every
   sample kept in a preallocated array. Percentiles are nearest-rank over all
   25,000,000 samples. Nothing is averaged within a run.

`bench/run.sh` builds in Release, records the hardware, and runs `itchbench`
five times per configuration under `taskset -c N`, interleaving configurations
round by round so slow drift hits both paths equally. The table reports the
**median across the five runs with the min-max range in brackets**, so a lucky
run cannot hide. Every raw run is in `bench/results/<label>/runs.jsonl`.

Each pass reopens the file (new fd or new mapping) so each pays its own
syscalls and page faults. `itchbench` also counts every `operator new` in the
process and reports how many happened inside the timed loops, and the flat
book reports whether either table grew during a run. Those are in the summary
files so "zero allocation" is a measurement, not a claim.

Both machines are VMs. The laptop numbers were taken inside Docker Desktop's
WSL2 VM, which has no PMU, so `perf stat` hardware counters come from the
GitHub runner only. The runner is a shared Azure VM with 2 vCPUs. Neither is a
tuned bare-metal box with isolated cores and a fixed clock, and the max column
shows it: single samples of 50 µs to several ms are the hypervisor or the
kernel taking the core, not the parser.

## correctness

`itchfeed FILE` parses a file, rebuilds the book and prints a JSON summary:
message count, count per type, running share totals, live orders, and the full
price ladder (price, shares, order count) for six symbols. It exits non-zero if
any of these fail:

- `added == executed + cancelled + deleted + replaced_out + live` shares, where
  `added` includes the new side of every replace. Applying an update twice,
  missing one, or applying it to the wrong order breaks the balance.
- the order counts summed over every price level equal the number of live
  orders, and the shares summed over every level equal the live shares.
- no update referenced an order the book had never seen, no execute or cancel
  took more shares than were left, no add reused a live reference, and every
  length prefix matched the spec length for its type.

`tools/itch_ref.py` is a second decoder written separately in Python (plain
dicts and `struct.unpack_from`) that prints the same JSON. `tools/compare.py`
diffs the two. On the 25M-message prefix the C++ handler and the Python
decoder agree on all 17 message-type counts, all share totals, 1,675,597 live
orders, and 18,737 price levels across the six symbols, for both ingest paths
and both books:

```
match: 25000000 messages, 17 types, 18737 price levels compared
```

Message counts in the 25M prefix, as counted by both decoders:

| type | count | type | count |
|---|---:|---|---:|
| A add order | 10,162,044 | P trade (non-cross) | 98,886 |
| D delete | 9,093,298 | C execute with price | 7,494 |
| U replace | 2,300,637 | S system event | 3 |
| F add with MPID | 896,632 | R stock directory | 8,906 |
| X cancel | 727,588 | I, L, Y, Q, H, K, V (counted, not decoded) | 1,314,911 |
| E execute | 389,601 | | |

Share totals: 5,757,141,328 added, 67,255,104 executed, 216,502,490 cancelled,
3,925,442,574 deleted, 745,972,963 replaced out, 801,968,197 live at the end of
the prefix. The balance holds exactly.

The whole 2019-12-30 file, streamed through `gzip -dc` into both decoders at
once, agrees the same way: 268,744,780 messages, 18 types, 47,597,537,971
shares added and 0 live at the close, last system event `C` (end of messages),
every balance check passing. The ladders are empty at that point because the
book is empty at the end of the day, which is why the benchmark and the ladder
comparison use the 25M prefix, where 1.68M orders are live.

```
match: 268744780 messages, 18 types, 0 price levels compared
```

`itchbench` also checks itself: the warmup pass, throughput pass and latency
pass must end with the same share totals, or the run exits non-zero.

## what I changed, with the before and after

I profiled before changing anything. Every change here has its numbers
committed next to it; the one thing I looked at and did not change is listed
too.

**Baseline** (`bench/results/01-baseline-std`, commit `94ca2da`). std
containers, 25M messages, laptop: 493 ns p50 and 3478 ns p99 on read(), 494
and 3329 on mmap, about 1.4 M msgs/s. The cpu-clock profile
(`docs/profile_baseline_std.txt`) put 78% of samples in `StdBook::on_add`,
`StdBook::remove` and the `std::_Hashtable` symbols under them, and 2% in
dispatch and decode. The two ingest paths were already inside each other's
spread. So the book was the problem, not the ingest and not the parsing.

**Flat book** (`include/itch/book_flat.hpp`, commit `47ca607`, results in
`02-flat-vs-std` and `03-gha-runner`). Orders go in one open-addressed table
keyed on the order reference number; price levels go in a second one keyed on
(locate, side, price) packed into 64 bits. Linear probing, Fibonacci hashing on
the key because reference numbers arrive nearly sequential, backward-shift
deletion so there are no tombstones. Both tables are sized at startup (2^23
order slots, 2^21 level slots) and every page is touched before the first
message; on this file they peak at 1,703,856 live orders and 734,763 live
levels, so both stay under 50% load and neither grows. Growth is counted and
reported in case a different file needs it. Runner, read path: p50 200 to
170 ns, p99 1472 to 421 ns, p99.9 2223 to 601 ns, throughput 3.71 to 13.51 M
msgs/s. Allocations in the timed loop 19,083,458 to 0. The flat profile
(`docs/profile_flat.txt`) is now 60% in `FlatBook::add` and `remove`, which is
the probe walk itself: with 1.7M live orders the order table is 200 MB and
every lookup is a cache miss. Dispatch and decode are 19% because the
denominator shrank, not because they got slower.

**Big-endian loads** (`docs/codegen.md`, commit `286c920`). No change made.
`memcpy` into an integer plus `__builtin_bswap` already compiles to one `movbe`
per field with `-march=native`, and `mov` plus `bswap` without it. The 6-byte
timestamp is one 8-byte `movbe` and a mask. Decode was 2% of the baseline
profile. There was nothing to take out, so I recorded the assembly and left it.

**mmap versus read()**. Both paths were in from the start so this was a
measurement, not an optimisation. It did not pay: see the numbers above. The
read() path's syscall cost is real (5.0 µs p50 per refill) but there are only 12,192
refills in 25M messages, and the mmap path pays 1,309 page faults instead. If I
wanted the read() tail down I would use a bigger buffer (`--read-buf` exists for
that), not mmap.

**Not done.** I did not experiment with struct layout beyond keeping the order
slot at 24 bytes and the level slot at 24 bytes, I did not try prefetching the
next order's slot, and I did not try `-O3`, LTO or PGO. Each of those would need
its own before and after, and the profile says the next real step is the cache
miss per lookup, which is a data layout problem (smaller slots, or a two-level
table that keeps recent references hot), not a compiler flag.

## reproducing the numbers

You need Linux (or Docker), a C++20 compiler, CMake and Python 3. Get the data,
then run the harness:

```bash
curl -sS https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/12302019.NASDAQ_ITCH50.gz |
    gzip -dc | python3 tools/cut_itch.py 25000000 > data/real_12302019_first25m.itch
CONFIGS="read:std mmap:std read:flat mmap:flat" bench/run.sh data/real_12302019_first25m.itch mine
```

That builds, records the hardware, runs five pinned runs per configuration and
writes `bench/results/mine/summary.md`. `CPU=N` picks the core (default 2) and
`RUNS=N` the repeat count. `bench/perf.sh FILE mine` adds `perf stat` counters
for both ingest paths, if the machine exposes a PMU. For the correctness check:

```bash
./build/itchfeed data/real_12302019_first25m.itch > cpp.json
python3 tools/itch_ref.py data/real_12302019_first25m.itch > ref.json
python3 tools/compare.py cpp.json ref.json
```

On a machine where Linux is only available through Docker, `ops/dev.sh CMD`
runs CMD inside the build image with the repo at `/src` and a data volume at
`/data`. The laptop numbers were taken that way.

## ci

GitHub Actions builds on every push, runs the unit tests, generates the
50,000-message synthetic fixture (checked against a committed sha256), and
verifies the handler against the generator's own expected summary and against
the Python decoder, for every ingest and book combination plus stdin. That is
about 20 seconds and keeps the parser from breaking quietly. Dispatching the
workflow with `bench=true` downloads the real sample file, runs the 25M-message
correctness check, the benchmark and `perf stat` on the runner, and uploads the
results as an artifact. The `Jenkinsfile` runs the same checks inside the build
image and takes a `DATA_FILE` parameter for the benchmark stage, the same shape
as [tern](https://github.com/noorps/tern).

## layout

```
include/itch/messages.hpp   ITCH 5.0 layouts and field decoders, offsets from the spec
include/itch/parser.hpp     one switch on the type byte, calls the book
include/itch/ingest.hpp     ReadIngest (read() into a buffer) and MmapIngest
include/itch/book_std.hpp   baseline book on std::unordered_map + std::map
include/itch/book_flat.hpp  open-addressed tables, no allocation per message
include/itch/tsc.hpp        rdtscp timer and its calibration
include/itch/report.hpp     balance checks and the JSON summary
src/itchfeed.cpp            parse, rebuild, check, print
src/itchbench.cpp           one benchmark run
bench/                      run.sh, perf.sh, hwinfo.sh and the recorded results
tools/                      Python reference decoder, comparer, prefix cutter, fixture generator
tests/                      unit tests and the CI fixture check
docs/                       profiles and the generated assembly
```

The spec I worked from is Nasdaq's "Nasdaq TotalView-ITCH 5.0" document,
revision dated April 28, 2023. Offsets in `messages.hpp` are relative to the
message type byte, after the 2-byte length prefix the sample files use.
