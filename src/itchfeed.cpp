// itchfeed verify: parse a file, rebuild the book, check that it balances and
// print a JSON summary. Used by CI and for the correctness section of the
// README.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "itch/book_std.hpp"
#include "itch/ingest.hpp"
#include "itch/parser.hpp"
#include "itch/report.hpp"

using namespace itch;

namespace {

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t a = 0;
    while (a <= s.size()) {
        size_t b = s.find(',', a);
        if (b == std::string::npos) b = s.size();
        if (b > a) out.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return out;
}

template <class Ingest, class Book>
int verify(Ingest& in, Book& book, const std::vector<std::string>& symbols) {
    TypeCounts counts;
    uint16_t len;
    while (const uint8_t* m = in.next(len)) dispatch(m, len, book, counts);
    CheckResult chk = check_book(book, counts);
    write_json(stdout, book, counts, symbols, chk);
    for (auto& f : chk.failures) std::fprintf(stderr, "FAIL: %s\n", f.c_str());
    return chk.ok ? 0 : 1;
}

template <class Book>
int with_ingest(const std::string& ingest, const std::string& path, const std::vector<std::string>& syms) {
    Book book;
    if (ingest == "mmap") {
        MmapIngest in(path);
        return verify(in, book, syms);
    }
    ReadIngest in(path);
    return verify(in, book, syms);
}

}  // namespace

int main(int argc, char** argv) {
    std::string path, ingest = "read", book = "std", symbols = "AAPL,MSFT,AMZN,TSLA,SPY,QQQ";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--ingest") ingest = val();
        else if (a == "--book") book = val();
        else if (a == "--symbols") symbols = val();
        else if (a == "-h" || a == "--help") {
            std::puts("usage: itchfeed FILE|- [--ingest read|mmap] [--book std] [--symbols A,B,C]");
            return 0;
        } else path = a;
    }
    if (path.empty()) {
        std::fputs("itchfeed: no input file (use - for stdin)\n", stderr);
        return 2;
    }
    if (path == "-") ingest = "read";
    auto syms = split(symbols);
    try {
        return with_ingest<StdBook>(ingest, path, syms);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "itchfeed: %s\n", e.what());
        return 2;
    }
}
