#!/bin/sh
# Print the assembly for the big-endian loads with the benchmark flags.
set -eu
flags=$(grep CMAKE_CXX_FLAGS_RELEASE CMakeLists.txt | sed 's/.*"\(.*\)".*/\1/')
echo "c++ $flags  ($(c++ --version | head -1))"
# shellcheck disable=SC2086
c++ -std=c++20 $flags -Iinclude -S -o - -fno-asynchronous-unwind-tables -g0 tools/codegen_probe.cpp |
    grep -v -E '^\s*\.(file|text|p2align|globl|type|size|ident|section|align)' | c++filt
