#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace hybrid {

struct MuInsertionMessage {
    std::array<std::uint8_t, 10> bytes {};
    std::size_t size {};
};

inline std::optional<MuInsertionMessage> muInsertionForVariation(
    std::span<const std::uint8_t> sysex) noexcept
{
    if (sysex.size() < 9 || sysex.size() > 10
        || sysex.front() != 0xf0 || sysex.back() != 0xf7
        || sysex[1] != 0x43 || (sysex[2] & 0xf0) != 0x10
        || sysex[3] != 0x4c || sysex[4] != 0x02
        || sysex[5] != 0x01)
        return std::nullopt;

    const auto address = sysex[6];
    std::uint8_t destination {};
    if (address == 0x5b && sysex.size() == 9)
        destination = 0x0c;
    else if (address >= 0x40 && address <= 0x57
             && address + sysex.size() - 9 <= 0x57)
        destination = address - 0x40;
    else
        return std::nullopt;

    MuInsertionMessage result;
    result.size = sysex.size();
    for (std::size_t i = 0; i < sysex.size(); ++i)
        result.bytes[i] = sysex[i];
    result.bytes[4] = 0x03;
    result.bytes[5] = 0x00;
    result.bytes[6] = destination;
    return result;
}

} // namespace hybrid
