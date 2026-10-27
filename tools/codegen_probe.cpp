// Compiled with the benchmark flags by tools/codegen.sh to show what the
// big-endian loads turn into. Not linked into anything.
#include "itch/messages.hpp"

using namespace itch;

uint16_t probe_be16(const uint8_t* p) { return be16(p); }
uint32_t probe_be32(const uint8_t* p) { return be32(p); }
uint64_t probe_be48(const uint8_t* p) { return be48(p); }
uint64_t probe_be64(const uint8_t* p) { return be64(p); }
AddOrder probe_decode_A(const uint8_t* m) { return decode_A(m); }
