#pragma once
// Big-endian loads for ITCH fields. memcpy + bswap compiles to a single
// mov/movbe or mov+bswap at -O2; see docs/codegen.md for the emitted asm.
#include <cstdint>
#include <cstring>

namespace itch {

inline uint16_t be16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return __builtin_bswap16(v);
}

inline uint32_t be32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return __builtin_bswap32(v);
}

inline uint64_t be64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return __builtin_bswap64(v);
}

// Timestamps are 6 bytes. Load 8 bytes starting 2 before the field and mask;
// every message has at least 5 bytes ahead of the timestamp, so this never
// reads outside the message.
inline uint64_t be48(const uint8_t* p) {
    return be64(p - 2) & 0x0000FFFFFFFFFFFFull;
}

}  // namespace itch
