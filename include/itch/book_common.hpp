#pragma once
// Pieces shared by both order book implementations.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "itch/messages.hpp"

namespace itch {

// Running share totals. These must balance at any point in the file:
//   added == executed + cancelled + deleted + replaced_out + live
// where "added" includes the new side of every replace. If an order update is
// applied twice, missed, or applied to the wrong order, this stops balancing.
struct BookTotals {
    uint64_t added_shares = 0;
    uint64_t executed_shares = 0;   // E and C
    uint64_t cancelled_shares = 0;  // X
    uint64_t deleted_shares = 0;    // remaining shares removed by D
    uint64_t replaced_out_shares = 0;  // remaining shares removed by U
    uint64_t trade_shares = 0;      // P, non-displayed, does not touch the book
    uint64_t unknown_ref = 0;       // update for an order we never saw
    uint64_t over_execution = 0;    // execute/cancel larger than what was left
    uint64_t duplicate_ref = 0;     // add for a reference already live
    char last_system_event = 0;
};

struct LevelView {
    uint32_t price;
    uint64_t shares;
    uint32_t orders;
};

// Symbols are 8 bytes, space padded.
inline std::string trim_symbol(const char s[8]) {
    std::string out(s, 8);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Locate -> symbol, filled from R messages. Fixed storage so a directory
// message never allocates.
struct Directory {
    std::vector<std::array<char, 8>> raw = std::vector<std::array<char, 8>>(65536);
    std::vector<bool> known = std::vector<bool>(65536);
    void set(const StockDirectory& r) {
        std::memcpy(raw[r.locate].data(), r.stock, 8);
        known[r.locate] = true;
    }
    std::string name(int locate) const { return trim_symbol(raw[locate].data()); }
    int find(const std::string& sym) const {
        for (int i = 0; i < 65536; ++i)
            if (known[i] && name(i) == sym) return i;
        return -1;
    }
    void clear() {
        std::fill(known.begin(), known.end(), false);
    }
};

}  // namespace itch
