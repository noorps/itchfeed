#pragma once
// Message dispatch. The parser is a single switch on the type byte; it decodes
// the fields a handler needs and calls one method. Both ingest paths and both
// book implementations go through this same function.
#include <array>
#include <cstdint>

#include "itch/messages.hpp"

namespace itch {

struct TypeCounts {
    std::array<uint64_t, 256> n{};
    uint64_t total = 0;
    uint64_t bad_length = 0;  // length prefix disagrees with the spec length
    void clear() { *this = TypeCounts{}; }
};

// Handler must provide: on_system(SystemEvent), on_directory(StockDirectory),
// on_add(AddOrder), on_execute(OrderExecuted), on_cancel(OrderCancel),
// on_delete(OrderDelete), on_replace(OrderReplace), on_trade(Trade).
template <class Handler>
inline void dispatch(const uint8_t* m, uint16_t len, Handler& h, TypeCounts& c) {
    const uint8_t type = m[0];
    ++c.n[type];
    ++c.total;
    if (__builtin_expect(len != message_length(type), 0)) {
        ++c.bad_length;
        return;
    }
    switch (type) {
        case 'A':
        case 'F': h.on_add(decode_A(m)); break;
        case 'E':
        case 'C': h.on_execute(decode_E(m)); break;
        case 'X': h.on_cancel(decode_X(m)); break;
        case 'D': h.on_delete(decode_D(m)); break;
        case 'U': h.on_replace(decode_U(m)); break;
        case 'P': h.on_trade(decode_P(m)); break;
        case 'S': h.on_system(decode_S(m)); break;
        case 'R': h.on_directory(decode_R(m)); break;
        default: break;  // other ITCH types are counted and skipped
    }
}

}  // namespace itch
