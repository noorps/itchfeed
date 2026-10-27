// Hand-built messages with known field values, decoded and applied to the
// book. No framework; a failed CHECK prints the line and exits non-zero.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "itch/book_std.hpp"
#include "itch/parser.hpp"
#include "itch/report.hpp"

using namespace itch;

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

namespace {

struct Msg {
    std::vector<uint8_t> b;
    explicit Msg(char type) { b.push_back(uint8_t(type)); }
    Msg& u(uint64_t v, int bytes) {
        for (int i = bytes - 1; i >= 0; --i) b.push_back(uint8_t(v >> (8 * i)));
        return *this;
    }
    Msg& s(const char* str, int width) {
        int n = int(std::strlen(str));
        for (int i = 0; i < width; ++i) b.push_back(uint8_t(i < n ? str[i] : ' '));
        return *this;
    }
    // locate, tracking number, timestamp
    Msg& hdr(uint16_t loc, uint64_t ts = 0x0102030405) { return u(loc, 2).u(7, 2).u(ts, 6); }
};

Msg add(uint16_t loc, uint64_t ref, char side, uint32_t sh, uint32_t px) {
    return std::move(Msg('A').hdr(loc).u(ref, 8).s(side == 'B' ? "B" : "S", 1).u(sh, 4).s("TEST", 8).u(px, 4));
}

template <class Book>
void feed(Book& book, TypeCounts& c, const Msg& m) {
    dispatch(m.b.data(), uint16_t(m.b.size()), book, c);
}

void test_lengths_match_spec() {
    // Builders above produce exactly the spec length for each type.
    CHECK(add(1, 1, 'B', 1, 1).b.size() == message_length('A'));
    CHECK(Msg('E').hdr(1).u(1, 8).u(1, 4).u(1, 8).b.size() == message_length('E'));
    CHECK(Msg('X').hdr(1).u(1, 8).u(1, 4).b.size() == message_length('X'));
    CHECK(Msg('D').hdr(1).u(1, 8).b.size() == message_length('D'));
    CHECK(Msg('U').hdr(1).u(1, 8).u(2, 8).u(1, 4).u(1, 4).b.size() == message_length('U'));
    CHECK(message_length('Z') == 0);
}

void test_decode_fields() {
    Msg a = add(0x1234, 0x0102030405060708ull, 'S', 0xA0B0C0D0, 1234500);
    AddOrder d = decode_A(a.b.data());
    CHECK(d.locate == 0x1234);
    CHECK(d.ref == 0x0102030405060708ull);
    CHECK(d.side == 'S');
    CHECK(d.shares == 0xA0B0C0D0);
    CHECK(d.price == 1234500);
    CHECK(timestamp(a.b.data()) == 0x0102030405ull);

    Msg u = Msg('U').hdr(9).u(11, 8).u(22, 8).u(300, 4).u(999, 4);
    OrderReplace r = decode_U(u.b.data());
    CHECK(r.old_ref == 11 && r.new_ref == 22 && r.shares == 300 && r.price == 999);

    Msg p = Msg('P').hdr(3).u(5, 8).s("B", 1).u(100, 4).s("TEST", 8).u(250000, 4).u(77, 8);
    CHECK(p.b.size() == message_length('P'));
    Trade t = decode_P(p.b.data());
    CHECK(t.ref == 5 && t.side == 'B' && t.shares == 100 && t.price == 250000 && t.match == 77);

    Msg rd = Msg('R').hdr(42).s("AAPL", 8).s("Q", 1).s("N", 1).u(100, 4).s("N", 1).s("C", 1).s("Z", 2)
                 .s("P", 1).s("N", 1).s(" ", 1).s("N", 1).s("N", 1).u(1, 4).s("N", 1);
    CHECK(rd.b.size() == message_length('R'));
    StockDirectory sd = decode_R(rd.b.data());
    CHECK(sd.locate == 42 && trim_symbol(sd.stock) == "AAPL");
}

void test_book_lifecycle() {
    StdBook book;
    TypeCounts c;
    feed(book, c, add(1, 100, 'B', 500, 10000));
    feed(book, c, add(1, 101, 'B', 200, 10000));
    feed(book, c, add(1, 102, 'B', 300, 9900));
    feed(book, c, add(1, 103, 'S', 400, 10100));

    auto bids = book.levels(1, 'B');
    CHECK(bids.size() == 2);
    CHECK(bids[0].price == 10000 && bids[0].shares == 700 && bids[0].orders == 2);
    CHECK(bids[1].price == 9900 && bids[1].orders == 1);

    // Partial execute, then execute the rest: order disappears, level stays.
    feed(book, c, Msg('E').hdr(1).u(100, 8).u(200, 4).u(1, 8));
    feed(book, c, Msg('E').hdr(1).u(100, 8).u(300, 4).u(2, 8));
    bids = book.levels(1, 'B');
    CHECK(bids[0].shares == 200 && bids[0].orders == 1);

    // Partial cancel.
    feed(book, c, Msg('X').hdr(1).u(101, 8).u(50, 4));
    CHECK(book.levels(1, 'B')[0].shares == 150);

    // Replace moves the order to a new price and reference.
    feed(book, c, Msg('U').hdr(1).u(101, 8).u(200, 8).u(120, 4).u(9900, 4));
    bids = book.levels(1, 'B');
    CHECK(bids.size() == 1);
    CHECK(bids[0].price == 9900 && bids[0].shares == 420 && bids[0].orders == 2);

    // Execute with price (C) reduces at the resting price.
    feed(book, c, Msg('C').hdr(1).u(103, 8).u(100, 4).u(3, 8).s("Y", 1).u(10200, 4));
    CHECK(book.levels(1, 'S')[0].shares == 300);

    // Delete removes the remaining shares and the level.
    feed(book, c, Msg('D').hdr(1).u(103, 8));
    CHECK(book.levels(1, 'S').empty());

    // Updates for an unknown order are counted, not applied.
    feed(book, c, Msg('D').hdr(1).u(555, 8));
    CHECK(book.totals.unknown_ref == 1);
    book.totals.unknown_ref = 0;

    CHECK(book.live_orders() == 2);
    CHECK(check_book(book, c).ok);
    CHECK(c.n['A'] == 4 && c.n['E'] == 2 && c.n['U'] == 1 && c.total == 11);
}

void test_bad_length_is_counted() {
    StdBook book;
    TypeCounts c;
    Msg a = add(1, 1, 'B', 1, 1);
    dispatch(a.b.data(), uint16_t(a.b.size() - 1), book, c);
    CHECK(c.bad_length == 1);
    CHECK(book.live_orders() == 0);
}

}  // namespace

int main() {
    test_lengths_match_spec();
    test_decode_fields();
    test_book_lifecycle();
    test_bad_length_is_counted();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("all parser tests passed");
    return 0;
}
