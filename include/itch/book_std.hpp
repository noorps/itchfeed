#pragma once
// Baseline book: std::unordered_map for orders, std::map per side for price
// levels. Every new order and every new price level allocates. This is the
// "before" for the allocation and hash table experiments.
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "itch/book_common.hpp"

namespace itch {

class StdBook {
public:
    static constexpr const char* name = "std";

    StdBook() : sides_(65536) {}

    void on_system(const SystemEvent& e) { totals.last_system_event = e.event_code; }
    void on_directory(const StockDirectory& r) { dir.set(r); }
    void on_trade(const Trade& t) { totals.trade_shares += t.shares; }

    void on_add(const AddOrder& a) {
        auto [it, inserted] = orders_.try_emplace(a.ref, Order{a.locate, a.side, a.shares, a.price});
        if (!inserted) {
            ++totals.duplicate_ref;
            return;
        }
        totals.added_shares += a.shares;
        Level& lv = level(a.locate, a.side, a.price);
        lv.shares += a.shares;
        lv.orders += 1;
    }

    void on_execute(const OrderExecuted& e) {
        uint32_t n = e.shares;
        if (reduce(e.ref, n)) totals.executed_shares += n;
    }

    void on_cancel(const OrderCancel& x) {
        uint32_t n = x.shares;
        if (reduce(x.ref, n)) totals.cancelled_shares += n;
    }

    void on_delete(const OrderDelete& d) {
        auto it = orders_.find(d.ref);
        if (it == orders_.end()) {
            ++totals.unknown_ref;
            return;
        }
        totals.deleted_shares += it->second.shares;
        remove(it);
    }

    void on_replace(const OrderReplace& u) {
        auto it = orders_.find(u.old_ref);
        if (it == orders_.end()) {
            ++totals.unknown_ref;
            return;
        }
        const Order old = it->second;
        totals.replaced_out_shares += old.shares;
        remove(it);
        on_add(AddOrder{old.locate, u.new_ref, old.side, u.shares, u.price});
    }

    // ---- inspection (not on the hot path) ----
    uint64_t table_grows() const { return 0; }
    uint64_t live_orders() const { return orders_.size(); }
    uint64_t live_shares() const {
        uint64_t s = 0;
        for (auto& [ref, o] : orders_) s += o.shares;
        return s;
    }
    // Sum over every level of every symbol, to cross-check against the orders.
    void level_sums(uint64_t& orders, uint64_t& shares) const {
        orders = shares = 0;
        for (auto& sb : sides_) {
            for (auto& [p, lv] : sb.bids) { orders += lv.orders; shares += lv.shares; }
            for (auto& [p, lv] : sb.asks) { orders += lv.orders; shares += lv.shares; }
        }
    }
    // Bids best-first (descending), asks best-first (ascending).
    std::vector<LevelView> levels(uint16_t locate, char side) const {
        std::vector<LevelView> out;
        const auto& sb = sides_[locate];
        if (side == 'B')
            for (auto& [p, lv] : sb.bids) out.push_back({p, lv.shares, lv.orders});
        else
            for (auto& [p, lv] : sb.asks) out.push_back({p, lv.shares, lv.orders});
        return out;
    }

    void clear() {
        orders_.clear();
        for (auto& sb : sides_) { sb.bids.clear(); sb.asks.clear(); }
        totals = BookTotals{};
        dir.clear();
    }

    BookTotals totals;
    Directory dir;

private:
    struct Order {
        uint16_t locate;
        char side;
        uint32_t shares;
        uint32_t price;
    };
    struct Level {
        uint64_t shares = 0;
        uint32_t orders = 0;
    };
    struct Sides {
        std::map<uint32_t, Level, std::greater<uint32_t>> bids;
        std::map<uint32_t, Level> asks;
    };
    using OrderIt = std::unordered_map<uint64_t, Order>::iterator;

    Level& level(uint16_t locate, char side, uint32_t price) {
        auto& sb = sides_[locate];
        return side == 'B' ? sb.bids[price] : sb.asks[price];
    }

    void drop_level(uint16_t locate, char side, uint32_t price) {
        auto& sb = sides_[locate];
        if (side == 'B') sb.bids.erase(price); else sb.asks.erase(price);
    }

    // Execute or cancel part of an order. Returns false if the ref is unknown;
    // otherwise sets shares to the amount actually taken off the book.
    bool reduce(uint64_t ref, uint32_t& shares) {
        auto it = orders_.find(ref);
        if (it == orders_.end()) {
            ++totals.unknown_ref;
            return false;
        }
        Order& o = it->second;
        if (shares > o.shares) {
            ++totals.over_execution;
            shares = o.shares;
        }
        Level& lv = level(o.locate, o.side, o.price);
        lv.shares -= shares;
        o.shares -= shares;
        if (o.shares == 0) {
            if (--lv.orders == 0) drop_level(o.locate, o.side, o.price);
            orders_.erase(it);
        }
        return true;
    }

    void remove(OrderIt it) {
        const Order& o = it->second;
        Level& lv = level(o.locate, o.side, o.price);
        lv.shares -= o.shares;
        if (--lv.orders == 0) drop_level(o.locate, o.side, o.price);
        orders_.erase(it);
    }

    std::unordered_map<uint64_t, Order> orders_;
    std::vector<Sides> sides_;
};

}  // namespace itch
