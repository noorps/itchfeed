#!/bin/sh
# Build, run the unit tests, then check the handler against the synthetic
# fixture three ways: the generator's own expected summary, the independent
# Python decoder, and itself across every ingest/book combination.
set -eu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

tmp=$(mktemp -d)
python3 tools/gen_itch.py --messages 50000 --seed 7 --out "$tmp/fixture.itch"
echo "$(cat tests/fixture.sha256)  $tmp/fixture.itch" | sha256sum -c -

python3 tools/itch_ref.py "$tmp/fixture.itch" > "$tmp/ref.json"
python3 tools/compare.py tests/fixture.expected.json "$tmp/ref.json"
for book in std flat; do
    for ingest in read mmap; do
        ./build/itchfeed "$tmp/fixture.itch" --ingest "$ingest" --book "$book" > "$tmp/$ingest-$book.json"
        printf '%s/%s: ' "$ingest" "$book"
        python3 tools/compare.py "$tmp/$ingest-$book.json" tests/fixture.expected.json
    done
done
# stdin goes through the read path
./build/itchfeed - < "$tmp/fixture.itch" > "$tmp/stdin.json"
printf 'stdin: '
python3 tools/compare.py "$tmp/stdin.json" tests/fixture.expected.json
# the benchmark harness must also run end to end
./build/itchbench "$tmp/fixture.itch" --ingest mmap --book flat > /dev/null
rm -rf "$tmp"
echo "fixture checks passed"
