#pragma once
// Cycle timer. rdtscp waits for earlier instructions to finish before reading
// the TSC; the lfence after it keeps later instructions from starting early.
// The TSC rate is calibrated against CLOCK_MONOTONIC at startup, and the
// result is printed with every run.
#include <x86intrin.h>
#include <time.h>

#include <cstdint>

namespace itch {

inline uint64_t tsc() {
    unsigned aux;
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

inline uint64_t mono_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

// TSC ticks per nanosecond, measured over `ms` milliseconds of busy-waiting.
inline double calibrate_tsc_ghz(int ms = 500) {
    const uint64_t n0 = mono_ns();
    const uint64_t c0 = tsc();
    uint64_t n1;
    do { n1 = mono_ns(); } while (n1 - n0 < uint64_t(ms) * 1000000ull);
    const uint64_t c1 = tsc();
    return double(c1 - c0) / double(n1 - n0);
}

}  // namespace itch
