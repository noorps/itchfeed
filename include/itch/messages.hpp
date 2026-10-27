#pragma once
// Nasdaq TotalView-ITCH 5.0 message layouts. Offsets are from the spec
// (revision dated April 28, 2023) and are relative to the message type byte,
// i.e. after the 2-byte length prefix used in the sample files.
#include <cstdint>
#include <cstring>

#include "itch/endian.hpp"

namespace itch {

// Payload length for every ITCH 5.0 message type, 0 for unknown types.
constexpr uint16_t message_length(uint8_t type) {
    switch (type) {
        case 'S': return 12;
        case 'R': return 39;
        case 'H': return 25;
        case 'Y': return 20;
        case 'L': return 26;
        case 'V': return 35;
        case 'W': return 12;
        case 'K': return 28;
        case 'J': return 35;
        case 'h': return 21;
        case 'A': return 36;
        case 'F': return 40;
        case 'E': return 31;
        case 'C': return 36;
        case 'X': return 23;
        case 'D': return 19;
        case 'U': return 35;
        case 'P': return 44;
        case 'Q': return 40;
        case 'B': return 19;
        case 'I': return 50;
        case 'N': return 20;
        case 'O': return 48;
        default:  return 0;
    }
}

// Common header: type(0) locate(1) tracking(3) timestamp(5..10).
inline uint16_t locate(const uint8_t* m) { return be16(m + 1); }
inline uint64_t timestamp(const uint8_t* m) { return be48(m + 5); }

struct SystemEvent {   // 'S'
    uint64_t ts;
    char event_code;
};

struct StockDirectory {  // 'R'
    uint16_t locate;
    char stock[8];
};

struct AddOrder {      // 'A' and 'F' (F adds a 4-byte MPID at 36)
    uint16_t locate;
    uint64_t ref;
    char side;
    uint32_t shares;
    uint32_t price;
};

struct OrderExecuted {  // 'E' and 'C' (C adds printable + price)
    uint16_t locate;
    uint64_t ref;
    uint32_t shares;
    uint64_t match;
};

struct OrderCancel {   // 'X'
    uint16_t locate;
    uint64_t ref;
    uint32_t shares;
};

struct OrderDelete {   // 'D'
    uint16_t locate;
    uint64_t ref;
};

struct OrderReplace {  // 'U'
    uint16_t locate;
    uint64_t old_ref;
    uint64_t new_ref;
    uint32_t shares;
    uint32_t price;
};

struct Trade {         // 'P'
    uint16_t locate;
    uint64_t ref;
    char side;
    uint32_t shares;
    uint32_t price;
    uint64_t match;
};

inline SystemEvent decode_S(const uint8_t* m) {
    return {timestamp(m), static_cast<char>(m[11])};
}

inline StockDirectory decode_R(const uint8_t* m) {
    StockDirectory r{};
    r.locate = locate(m);
    std::memcpy(r.stock, m + 11, 8);
    return r;
}

inline AddOrder decode_A(const uint8_t* m) {
    return {locate(m), be64(m + 11), static_cast<char>(m[19]), be32(m + 20), be32(m + 32)};
}

inline OrderExecuted decode_E(const uint8_t* m) {
    return {locate(m), be64(m + 11), be32(m + 19), be64(m + 23)};
}

inline OrderCancel decode_X(const uint8_t* m) {
    return {locate(m), be64(m + 11), be32(m + 19)};
}

inline OrderDelete decode_D(const uint8_t* m) {
    return {locate(m), be64(m + 11)};
}

inline OrderReplace decode_U(const uint8_t* m) {
    return {locate(m), be64(m + 11), be64(m + 19), be32(m + 27), be32(m + 31)};
}

inline Trade decode_P(const uint8_t* m) {
    return {locate(m), be64(m + 11), static_cast<char>(m[19]), be32(m + 20), be32(m + 32), be64(m + 36)};
}

}  // namespace itch
