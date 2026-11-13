#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace mu_pilot {
struct PresetAddress {
    static constexpr std::array<std::uint8_t, 4> magic{'M', 'U', 'P', '1'};
    static constexpr std::size_t size = 7;
    std::uint8_t msb, lsb, program;

    constexpr std::array<std::uint8_t, size> encode() const {
        return {magic[0], magic[1], magic[2], magic[3], msb, lsb, program};
    }
    constexpr bool valid() const { return msb < 128 && lsb < 128 && program < 128; }
    constexpr bool matches(const std::uint8_t* part) const {
        return part[1] == msb && part[2] == lsb && part[3] == program;
    }
    static constexpr std::array<std::uint8_t, 9> reset{0xf0, 0x43, 0x10, 0x4c, 0, 0, 0x7e, 0, 0xf7};
    constexpr std::array<std::uint8_t, 8> select() const {
        return {0xb0, 0, msb, 0xb0, 32, lsb, 0xc0, program};
    }
    constexpr std::array<std::uint8_t, 17> setup() const {
        std::array<std::uint8_t, 17> result{};
        const auto selection = select();
        for (std::size_t i = 0; i < reset.size(); ++i) result[i] = reset[i];
        for (std::size_t i = 0; i < selection.size(); ++i) result[reset.size() + i] = selection[i];
        return result;
    }
    static constexpr bool decode(std::span<const std::uint8_t> bytes, PresetAddress& out) {
        if (bytes.size() != size) return false;
        for (std::size_t i = 0; i < magic.size(); ++i)
            if (bytes[i] != magic[i]) return false;
        out = {bytes[4], bytes[5], bytes[6]};
        return out.valid();
    }
};
}
