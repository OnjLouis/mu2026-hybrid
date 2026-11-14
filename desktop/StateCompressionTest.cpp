#include "StateCompression.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::uint8_t> raw(6097261);
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary);
        raw.assign(std::istreambuf_iterator<char>(file), {});
        if (raw.empty()) return 1;
    } else {
        for (std::size_t i = 0; i < 65536; ++i) raw[2 * i] = std::uint8_t(i * 73);
        raw.back() = 0xa5;
    }
    const auto packed = mu_pilot::compressEngineState(raw);
    auto expand = [](const juce::MemoryBlock& bytes) {
        return mu_pilot::expandEngineState({static_cast<const std::uint8_t*>(bytes.getData()), bytes.getSize()});
    };
    const auto unpacked = expand(packed);
    if (packed.isEmpty() || packed.getSize() > raw.size() / 4 ||
        unpacked.getSize() != raw.size() || std::memcmp(raw.data(), unpacked.getData(), raw.size()) != 0) return 2;
    auto invalid = packed;
    static_cast<std::uint8_t*>(invalid.getData())[4] ^= 1;
    if (!expand(invalid).isEmpty()) return 3;
    invalid = packed;
    invalid.setSize(invalid.getSize() / 2);
    if (!expand(invalid).isEmpty()) return 4;
    invalid = packed;
    std::fill_n(static_cast<std::uint8_t*>(invalid.getData()), 4, std::uint8_t(0xff));
    if (!expand(invalid).isEmpty()) return 5;
    return 0;
}
