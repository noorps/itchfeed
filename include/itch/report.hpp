#pragma once
// Correctness summary written by `itchfeed verify`. tools/itch_ref.py writes
// the same JSON from an independent Python decoder so the two can be diffed.
#include <cstdio>
#include <string>
#include <vector>

#include "itch/book_common.hpp"
#include "itch/parser.hpp"

namespace itch {

struct CheckResult {
    bool ok = true;
    std::vector<std::string> failures;
    void fail(const std::string& why) { ok = false; failures.push_back(why); }
};

template <class Book>
CheckResult check_book(const Book& b, const TypeCounts& c) {
    CheckResult r;
    const BookTotals& t = b.totals;
    if (c.bad_length) r.fail("length prefix disagrees with spec length on " + std::to_string(c.bad_length) + " messages");
    if (t.unknown_ref) r.fail(std::to_string(t.unknown_ref) + " updates referenced an unknown order");
    if (t.over_execution) r.fail(std::to_string(t.over_execution) + " executes/cancels exceeded remaining shares");
    if (t.duplicate_ref) r.fail(std::to_string(t.duplicate_ref) + " adds reused a live order reference");
    const uint64_t live = b.live_shares();
    const uint64_t out = t.executed_shares + t.cancelled_shares + t.deleted_shares + t.replaced_out_shares + live;
    if (t.added_shares != out)
        r.fail("share balance: added " + std::to_string(t.added_shares) + " != removed+live " + std::to_string(out));
    uint64_t lv_orders, lv_shares;
    b.level_sums(lv_orders, lv_shares);
    if (lv_orders != b.live_orders())
        r.fail("level order counts sum to " + std::to_string(lv_orders) + " but " + std::to_string(b.live_orders()) + " orders are live");
    if (lv_shares != live)
        r.fail("level shares sum to " + std::to_string(lv_shares) + " but live orders hold " + std::to_string(live));
    return r;
}

template <class Book>
void write_json(FILE* f, const Book& b, const TypeCounts& c, const std::vector<std::string>& symbols,
                const CheckResult& chk) {
    const BookTotals& t = b.totals;
    std::fprintf(f, "{\n  \"messages\": %llu,\n  \"counts\": {", (unsigned long long)c.total);
    bool first = true;
    for (int i = 0; i < 256; ++i) {
        if (!c.n[i]) continue;
        std::fprintf(f, "%s\"%c\": %llu", first ? "" : ", ", char(i), (unsigned long long)c.n[i]);
        first = false;
    }
    std::fprintf(f, "},\n");
    std::fprintf(f, "  \"totals\": {\"added\": %llu, \"executed\": %llu, \"cancelled\": %llu, \"deleted\": %llu, "
                    "\"replaced_out\": %llu, \"trade_P\": %llu, \"live\": %llu},\n",
                 (unsigned long long)t.added_shares, (unsigned long long)t.executed_shares,
                 (unsigned long long)t.cancelled_shares, (unsigned long long)t.deleted_shares,
                 (unsigned long long)t.replaced_out_shares, (unsigned long long)t.trade_shares,
                 (unsigned long long)b.live_shares());
    std::fprintf(f, "  \"live_orders\": %llu,\n", (unsigned long long)b.live_orders());
    std::fprintf(f, "  \"last_system_event\": \"%c\",\n", t.last_system_event ? t.last_system_event : '?');
    std::fprintf(f, "  \"books\": {");
    for (size_t s = 0; s < symbols.size(); ++s) {
        const int loc = b.dir.find(symbols[s]);
        std::fprintf(f, "%s\n    \"%s\": {", s ? "," : "", symbols[s].c_str());
        for (char side : {'B', 'S'}) {
            std::fprintf(f, "%s\"%c\": [", side == 'S' ? ", " : "", side);
            if (loc >= 0) {
                auto lv = b.levels(uint16_t(loc), side);
                for (size_t i = 0; i < lv.size(); ++i)
                    std::fprintf(f, "%s[%u, %llu, %u]", i ? ", " : "", lv[i].price,
                                 (unsigned long long)lv[i].shares, lv[i].orders);
            }
            std::fprintf(f, "]");
        }
        std::fprintf(f, "}");
    }
    std::fprintf(f, "\n  },\n  \"checks_passed\": %s,\n  \"failures\": [", chk.ok ? "true" : "false");
    for (size_t i = 0; i < chk.failures.size(); ++i)
        std::fprintf(f, "%s\"%s\"", i ? ", " : "", chk.failures[i].c_str());
    std::fprintf(f, "]\n}\n");
}

}  // namespace itch
