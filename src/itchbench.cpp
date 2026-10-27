// itchbench: one benchmark run in one process.
//
//   1. calibrate the TSC against CLOCK_MONOTONIC
//   2. measure the timer's own floor (back-to-back reads, same store pattern)
//   3. warmup: one full untimed pass over the file, then clear the book
//   4. throughput pass: fresh ingest, no per-message timing, wall clock only
//   5. latency pass: fresh ingest, one TSC read after every message
//
// Each pass opens the file again (new fd or new mapping), so every pass pays
// its own syscalls and page faults. The page cache is warm after step 3 for
// both paths. Results go to stdout as one JSON object.
#include <sched.h>
#include <unistd.h>

#include <algorithm>
#include <new>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "itch/book_flat.hpp"
#include "itch/book_std.hpp"
#include "itch/ingest.hpp"
#include "itch/parser.hpp"
#include "itch/tsc.hpp"

using namespace itch;

// Count every operator new in the process, so the report can show how many
// allocations happened inside the timed loops rather than asserting it.
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
static uint64_t g_allocs = 0;
void* operator new(size_t n) {
    ++g_allocs;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

namespace {

struct Options {
    std::string path;
    std::string ingest = "read";
    std::string book = "flat";
    size_t read_buf = 64 * 1024;
    bool populate = false;
};

// perf stat --control: when PERF_CTL_FD/PERF_ACK_FD are set, counting is
// enabled only around the throughput pass.
struct PerfControl {
    int ctl = -1, ack = -1;
    PerfControl() {
        if (const char* c = std::getenv("PERF_CTL_FD")) ctl = std::atoi(c);
        if (const char* a = std::getenv("PERF_ACK_FD")) ack = std::atoi(a);
    }
    void send(const char* cmd) {
        if (ctl < 0) return;
        if (::write(ctl, cmd, std::strlen(cmd)) < 0) return;
        char buf[16];
        if (ack >= 0 && ::read(ack, buf, sizeof buf) < 0) return;
    }
};

// Nearest-rank percentile on a sorted vector.
double pct(const std::vector<uint32_t>& sorted, double q) {
    size_t idx = size_t(std::ceil(q * double(sorted.size())));
    if (idx > 0) --idx;
    return double(sorted[std::min(idx, sorted.size() - 1)]);
}

template <class Ingest>
struct Opener;
template <>
struct Opener<ReadIngest> {
    static ReadIngest* open(const Options& o) { return new ReadIngest(o.path, o.read_buf); }
};
template <>
struct Opener<MmapIngest> {
    static MmapIngest* open(const Options& o) { return new MmapIngest(o.path, o.populate); }
};

template <class Ingest, class Book>
int bench(const Options& o) {
    const int cpu_start = sched_getcpu();
    const double ghz = calibrate_tsc_ghz();
    auto ns = [&](double cyc) { return cyc / ghz; };

    // Timer floor: the same chained read-and-store loop with no work inside.
    std::vector<uint32_t> floor_samples(1 << 20);
    {
        uint64_t t0 = tsc();
        for (auto& s : floor_samples) {
            const uint64_t t1 = tsc();
            s = uint32_t(t1 - t0);
            t0 = t1;
        }
        std::sort(floor_samples.begin(), floor_samples.end());
    }

    Book* book_p = new Book();  // heap: the std book's locate table is large
    Book& book = *book_p;
    TypeCounts counts;
    uint16_t len;

    // Warmup (excluded from every reported number).
    uint64_t n_msgs = 0;
    {
        Ingest* in = Opener<Ingest>::open(o);
        while (const uint8_t* m = in->next(len)) dispatch(m, len, book, counts);
        n_msgs = counts.total;
        delete in;
    }
    const BookTotals warm = book.totals;
    const uint64_t grows_after_warmup = book.table_grows();
    book.clear();
    counts.clear();

    // Throughput pass.
    PerfControl perf;
    uint64_t read_calls = 0, a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    perf.send("enable\n");
    const uint64_t w0 = mono_ns();
    {
        Ingest* in = Opener<Ingest>::open(o);
        a0 = g_allocs;
        while (const uint8_t* m = in->next(len)) dispatch(m, len, book, counts);
        a1 = g_allocs;
        read_calls = in->read_calls;
        delete in;
    }
    const uint64_t w1 = mono_ns();
    perf.send("disable\n");
    book.clear();
    counts.clear();

    // Latency pass. The sample buffer is allocated and touched before timing.
    std::vector<uint32_t> lat(n_msgs, 0);
    uint64_t i = 0;
    {
        Ingest* in = Opener<Ingest>::open(o);
        uint32_t* out = lat.data();
        a2 = g_allocs;
        uint64_t t0 = tsc();
        while (const uint8_t* m = in->next(len)) {
            dispatch(m, len, book, counts);
            const uint64_t t1 = tsc();
            out[i++] = uint32_t(t1 - t0);
            t0 = t1;
        }
        a3 = g_allocs;
        delete in;
    }
    const int cpu_end = sched_getcpu();
    const bool agree = i == n_msgs && book.totals.added_shares == warm.added_shares &&
                       book.totals.executed_shares == warm.executed_shares &&
                       book.totals.unknown_ref == warm.unknown_ref;

    // Where did the slow samples come from? Walk the file again, untimed, to
    // recover each message's type and whether a read() call happened on it.
    std::vector<uint8_t> type_of(n_msgs);
    std::vector<uint8_t> syscall_on(n_msgs);
    {
        Ingest* in = Opener<Ingest>::open(o);
        uint64_t k = 0, prev = 0;
        while (const uint8_t* m = in->next(len)) {
            type_of[k] = m[0];
            syscall_on[k] = in->read_calls != prev;
            prev = in->read_calls;
            ++k;
        }
        delete in;
    }
    const uint64_t max_idx = uint64_t(std::max_element(lat.begin(), lat.end()) - lat.begin());
    std::vector<uint32_t> with_sys, without_sys;
    for (uint64_t k = 0; k < n_msgs; ++k) (syscall_on[k] ? with_sys : without_sys).push_back(lat[k]);
    std::sort(with_sys.begin(), with_sys.end());
    std::sort(without_sys.begin(), without_sys.end());
    std::vector<uint32_t> sorted = lat;
    std::sort(sorted.begin(), sorted.end());

    std::printf("{\"ingest\": \"%s\", \"book\": \"%s\", \"file\": \"%s\", \"read_buf\": %zu, \"populate\": %s,\n",
                Ingest::name, Book::name, o.path.c_str(), o.read_buf, o.populate ? "true" : "false");
    std::printf(" \"cpu_start\": %d, \"cpu_end\": %d, \"tsc_ghz\": %.4f, \"messages\": %llu, \"read_calls\": %llu,\n",
                cpu_start, cpu_end, ghz, (unsigned long long)n_msgs, (unsigned long long)read_calls);
    std::printf(" \"allocations_in_loop\": {\"throughput\": %llu, \"latency\": %llu}, \"table_grows\": %llu,\n",
                (unsigned long long)(a1 - a0), (unsigned long long)(a3 - a2), (unsigned long long)(book.table_grows() - grows_after_warmup));
    std::printf(" \"timer_floor_ns\": {\"p50\": %.1f, \"p99\": %.1f},\n", ns(pct(floor_samples, 0.50)),
                ns(pct(floor_samples, 0.99)));
    std::printf(" \"throughput_msgs_per_sec\": %.0f, \"throughput_wall_ns\": %llu,\n",
                double(n_msgs) * 1e9 / double(w1 - w0), (unsigned long long)(w1 - w0));
    std::printf(" \"latency_ns\": {\"p50\": %.1f, \"p90\": %.1f, \"p99\": %.1f, \"p99.9\": %.1f, "
                "\"p99.99\": %.1f, \"max\": %.1f},\n",
                ns(pct(sorted, 0.50)), ns(pct(sorted, 0.90)), ns(pct(sorted, 0.99)), ns(pct(sorted, 0.999)),
                ns(pct(sorted, 0.9999)), ns(double(sorted.back())));
    std::printf(" \"max_at\": {\"index\": %llu, \"type\": \"%c\", \"read_call\": %s},\n",
                (unsigned long long)max_idx, char(type_of[max_idx]), syscall_on[max_idx] ? "true" : "false");
    if (!with_sys.empty())
        std::printf(" \"with_read_call\": {\"n\": %zu, \"p50\": %.1f, \"max\": %.1f},\n", with_sys.size(),
                    ns(pct(with_sys, 0.5)), ns(double(with_sys.back())));
    std::printf(" \"without_read_call\": {\"n\": %zu, \"p50\": %.1f, \"p99\": %.1f, \"p99.9\": %.1f, \"max\": %.1f},\n",
                without_sys.size(), ns(pct(without_sys, 0.5)), ns(pct(without_sys, 0.99)),
                ns(pct(without_sys, 0.999)), ns(double(without_sys.back())));

    // Per-type p50/p99 for the types the book handles.
    std::printf(" \"by_type_ns\": {");
    bool first = true;
    for (char t : {'A', 'F', 'E', 'C', 'X', 'D', 'U', 'P', 'S', 'R'}) {
        std::vector<uint32_t> v;
        for (uint64_t k = 0; k < n_msgs; ++k)
            if (type_of[k] == uint8_t(t)) v.push_back(lat[k]);
        if (v.empty()) continue;
        std::sort(v.begin(), v.end());
        std::printf("%s\"%c\": {\"n\": %zu, \"p50\": %.1f, \"p99\": %.1f}", first ? "" : ", ", t, v.size(),
                    ns(pct(v, 0.5)), ns(pct(v, 0.99)));
        first = false;
    }
    std::printf("},\n \"passes_agree\": %s}\n", agree ? "true" : "false");
    delete book_p;
    return agree ? 0 : 1;
}

template <class Book>
int pick_ingest(const Options& o) {
    if (o.ingest == "mmap") return bench<MmapIngest, Book>(o);
    return bench<ReadIngest, Book>(o);
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--ingest") o.ingest = val();
        else if (a == "--book") o.book = val();
        else if (a == "--read-buf") o.read_buf = std::stoul(val());
        else if (a == "--populate") o.populate = true;
        else if (a == "-h" || a == "--help") {
            std::puts("usage: itchbench FILE [--ingest read|mmap] [--book std|flat] [--read-buf BYTES] [--populate]");
            return 0;
        } else o.path = a;
    }
    if (o.path.empty() || o.path == "-") {
        std::fputs("itchbench: needs a regular file\n", stderr);
        return 2;
    }
    try {
        if (o.book == "std") return pick_ingest<StdBook>(o);
        return pick_ingest<FlatBook>(o);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "itchbench: %s\n", e.what());
        return 2;
    }
}
