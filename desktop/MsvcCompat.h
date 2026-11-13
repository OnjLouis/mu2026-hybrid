#pragma once

#include <bit>

// The pinned core uses the GNU spelling in diagnostic counters.
inline int __builtin_popcountll(unsigned long long value) { return std::popcount(value); }
