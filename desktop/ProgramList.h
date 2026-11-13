#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mu_pilot {
constexpr int programCount = 128;
constexpr int sfxKitBank = 126, drumKitBank = 127;

inline bool isKitBank(int bank) { return bank == sfxKitBank || bank == drumKitBank; }

struct Program {
    int number;
    std::string name;
};

// An unknown catalogue entry is different from a confirmed empty program.
template<class ProgramName>
std::vector<Program> programsFor(ProgramName programName) {
    std::vector<Program> items;
    items.reserve(programCount);
    for (int pc = 0; pc < programCount; ++pc) {
        auto name = programName(pc);
        if (!name || !name->empty()) items.push_back({pc, name ? std::move(*name) : std::string{}});
    }
    return items;
}

inline int atOrAfter(const std::vector<Program>& items, int program) {
    const auto next = std::lower_bound(items.begin(), items.end(), program,
        [](const Program& item, int pc) { return item.number < pc; });
    return next == items.end() ? (items.empty() ? -1 : items.back().number) : next->number;
}
}
