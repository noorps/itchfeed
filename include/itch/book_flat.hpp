#pragma once
// Allocation-free book. Orders live in one open-addressed table keyed on the
// order reference number; price levels live in a second open-addressed table
// keyed on (locate, side, price). Both are sized up front and use linear
// probing with backward-shift deletion, so there are no tombstones and no
// allocation per message. Each table grows (doubles) only if it passes 50%
// load; growth is counted and reported so a run that grew is visible.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "itch/book_common.hpp"

namespace itch {

namespace detail {

inline uint64_t mix(uint64_t k) {
    // Fibonacci hashing; refs are mostly sequential, so a multiply spreads
    // neighbours across the table.
    return k * 0x9E3779B97F4A7C15ull;
}

// Open-addressed map from a non-zero 64-bit key to a small POD value.
template <class V>
class FlatTable {
public:
    struct Slot {
        uint64_t key;  // 0 = empty
        V val;
    };

    explicit FlatTable(unsigned log2_cap) { alloc(log2_cap); }
    ~FlatTable() { std::free(slots_); }
    FlatTable(const FlatTable&) = delete;
    FlatTable& operator=(const FlatTable&) = delete;

    V* find(uint64_t key) {
        size_t i = home(key);
        for (;;) {
            Slot& s = slots_[i];
            if (s.key == key) return &s.val;
            if (s.key == 0) return nullptr;
            i = (i + 1) & mask_;
        }
    }

    // Returns the value for key, inserting a zeroed one if absent.
    V& get_or_insert(uint64_t key, bool& inserted) {
        size_t i = home(key);
        for (;;) {
            Slot& s = slots_[i];
            if (s.key == key) {
                inserted = false;
                return s.val;
            }
            if (s.key == 0) {
                if (__builtin_expect((size_ + 1) * 2 > cap(), 0)) {
                    grow();
                    return get_or_insert(key, inserted);
                }
                s.key = key;
                s.val = V{};
                if (++size_ > peak) peak = size_;
                inserted = true;
                return s.val;
            }
            i = (i + 1) & mask_;
        }
    }

    // Remove the slot that `v` points into.
    void erase(V* v) {
        Slot* s = reinterpret_cast<Slot*>(reinterpret_cast<char*>(v) - offsetof(Slot, val));
        size_t i = size_t(s - slots_);
        // Backward-shift: pull later entries of the probe run into the hole.
        size_t j = i;
        for (;;) {
            j = (j + 1) & mask_;
            if (slots_[j].key == 0) break;
            const size_t h = home(slots_[j].key);
            // Move j into i if its home is not in the cyclic range (i, j].
            if (((j - h) & mask_) >= ((j - i) & mask_)) {
                slots_[i] = slots_[j];
                i = j;
            }
        }
        slots_[i].key = 0;
        --size_;
    }

    void clear() {
        std::memset(slots_, 0, cap() * sizeof(Slot));
        size_ = 0;
    }

    size_t size() const { return size_; }
    size_t cap() const { return mask_ + 1; }
    uint64_t grows = 0;
    size_t peak = 0;  // most live entries seen

    template <class F>
    void for_each(F&& f) const {
        for (size_t i = 0; i < cap(); ++i)
            if (slots_[i].key) f(slots_[i].key, slots_[i].val);
    }

private:
    size_t home(uint64_t key) const { return size_t(mix(key) >> shift_); }

    void alloc(unsigned log2_cap) {
        const size_t n = size_t(1) << log2_cap;
        slots_ = static_cast<Slot*>(std::aligned_alloc(64, n * sizeof(Slot)));
        if (!slots_) throw std::bad_alloc();
        std::memset(slots_, 0, n * sizeof(Slot));  // touch every page now
        mask_ = n - 1;
        shift_ = 64 - log2_cap;
        size_ = 0;
    }

    __attribute__((noinline)) void grow() {
        Slot* old = slots_;
        const size_t old_cap = cap();
        alloc(64 - shift_ + 1);
        ++grows;
        for (size_t i = 0; i < old_cap; ++i) {
            if (!old[i].key) continue;
            size_t j = home(old[i].key);
            while (slots_[j].key) j = (j + 1) & mask_;
            slots_[j] = old[i];
            ++size_;
        }
        std::free(old);
    }

    Slot* slots_ = nullptr;
    size_t mask_ = 0;
    unsigned shift_ = 0;
    size_t size_ = 0;
};

}  // namespace detail

class FlatBook {
public:
    static constexpr const char* name = "flat";

    // 2^23 order slots (~8.4M) and 2^21 level slots by default, so both stay
    // under 50% load at the peaks seen in the 2019-12-30 sample file.
    explicit FlatBook(unsigned order_bits = 23, unsigned level_bits = 21)
        : orders_(order_bits), levels_(level_bits) {}

    void on_system(const SystemEvent& e) { totals.last_system_event = e.event_code; }
    void on_directory(const StockDirectory& r) { dir.set(r); }
    void on_trade(const Trade& t) { totals.trade_shares += t.shares; }

    void on_add(const AddOrder& a) { add(a.ref, a.locate, a.side, a.shares, a.price); }

    void on_execute(const OrderExecuted& e) { totals.executed_shares += reduce(e.ref, e.shares); }
    void on_cancel(const OrderCancel& x) { totals.cancelled_shares += reduce(x.ref, x.shares); }

    void on_delete(const OrderDelete& d) {
        Order* o = orders_.find(key(d.ref));
        if (!o) {
            ++totals.unknown_ref;
            return;
        }
        totals.deleted_shares += o->shares;
        remove(o);
    }

    void on_replace(const OrderReplace& u) {
        Order* o = orders_.find(key(u.old_ref));
        if (!o) {
            ++totals.unknown_ref;
            return;
        }
        const uint16_t locate = o->locate;
        const char side = o->side;
        totals.replaced_out_shares += o->shares;
        remove(o);
        add(u.new_ref, locate, side, u.shares, u.price);
    }

    // ---- inspection (not on the hot path) ----
    uint64_t live_orders() const { return orders_.size(); }
    uint64_t live_shares() const {
        uint64_t s = 0;
        orders_.for_each([&](uint64_t, const Order& o) { s += o.shares; });
        return s;
    }
    void level_sums(uint64_t& orders, uint64_t& shares) const {
        orders = shares = 0;
        levels_.for_each([&](uint64_t, const Level& l) {
            orders += l.orders;
            shares += l.shares;
        });
    }
    std::vector<LevelView> levels(uint16_t locate, char side) const;
    uint64_t table_grows() const { return orders_.grows + levels_.grows; }
    size_t order_capacity() const { return orders_.cap(); }
    size_t level_capacity() const { return levels_.cap(); }
    size_t peak_orders() const { return orders_.peak; }
    size_t peak_levels() const { return levels_.peak; }

    void clear() {
        orders_.clear();
        levels_.clear();
        totals = BookTotals{};
        dir.clear();
    }

    BookTotals totals;
    Directory dir;

private:
    struct Order {
        uint32_t shares;
        uint32_t price;
        uint16_t locate;
        char side;
    };
    struct Level {
        uint64_t shares;
        uint32_t orders;
    };

    // Order refs are never 0 in practice, but 0 marks an empty slot, so
    // shift every key by one.
    static uint64_t key(uint64_t ref) { return ref + 1; }
    static uint64_t level_key(uint16_t locate, char side, uint32_t price) {
        return (uint64_t(1) << 63) | (uint64_t(locate) << 33) | (uint64_t(side == 'B') << 32) | price;
    }

    void add(uint64_t ref, uint16_t locate, char side, uint32_t shares, uint32_t price) {
        bool inserted;
        Order& o = orders_.get_or_insert(key(ref), inserted);
        if (!inserted) {
            ++totals.duplicate_ref;
            return;
        }
        o = Order{shares, price, locate, side};
        totals.added_shares += shares;
        Level& l = levels_.get_or_insert(level_key(locate, side, price), inserted);
        l.shares += shares;
        l.orders += 1;
    }

    uint32_t reduce(uint64_t ref, uint32_t shares) {
        Order* o = orders_.find(key(ref));
        if (!o) {
            ++totals.unknown_ref;
            return 0;
        }
        if (shares > o->shares) {
            ++totals.over_execution;
            shares = o->shares;
        }
        Level* l = levels_.find(level_key(o->locate, o->side, o->price));
        l->shares -= shares;
        o->shares -= shares;
        if (o->shares == 0) {
            if (--l->orders == 0) levels_.erase(l);
            orders_.erase(o);
        }
        return shares;
    }

    void remove(Order* o) {
        Level* l = levels_.find(level_key(o->locate, o->side, o->price));
        l->shares -= o->shares;
        if (--l->orders == 0) levels_.erase(l);
        orders_.erase(o);
    }

    detail::FlatTable<Order> orders_;
    detail::FlatTable<Level> levels_;
};

inline std::vector<LevelView> FlatBook::levels(uint16_t locate, char side) const {
    std::vector<LevelView> out;
    levels_.for_each([&](uint64_t k, const Level& l) {
        if (uint16_t(k >> 33) == locate && ((k >> 32) & 1) == uint64_t(side == 'B'))
            out.push_back({uint32_t(k), l.shares, l.orders});
    });
    std::sort(out.begin(), out.end(), [&](const LevelView& a, const LevelView& b) {
        return side == 'B' ? a.price > b.price : a.price < b.price;
    });
    return out;
}

}  // namespace itch
