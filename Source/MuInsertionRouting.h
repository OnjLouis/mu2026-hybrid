#pragma once

#include <algorithm>
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

class MuInsertionRouting {
public:
    void reset() noexcept
    {
        assignments = {};
    }

    void observe(std::span<const std::uint8_t> sysex) noexcept
    {
        if ((sysex.size() != 9 && sysex.size() != 10)
            || sysex.front() != 0xf0 || sysex.back() != 0xf7
            || sysex[1] != 0x43 || (sysex[2] & 0xf0) != 0x10
            || sysex[3] != 0x4c || sysex[4] != 0x03
            || sysex[5] < 0x01 || sysex[5] > 0x03)
            return;

        auto& assignment = assignments[sysex[5] - 1];
        if (sysex[6] == 0x00 && sysex.size() == 10)
            assignment.typeEnabled = sysex[7] != 0 || sysex[8] != 0;
        else if (sysex[6] == 0x0c && sysex.size() == 9)
            assignment.part = sysex[7] < 16
                ? std::optional<std::uint8_t>(sysex[7])
                : std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint8_t> targetFor(
        std::uint8_t channel) const noexcept
    {
        for (std::size_t index = 0; index < assignments.size(); ++index)
            if (assignments[index].typeEnabled
                && assignments[index].part == channel)
                return static_cast<std::uint8_t>(index + 2);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint8_t> targetForSoleRoute(
        std::uint32_t routeMask) const noexcept
    {
        for (std::size_t index = 0; index < assignments.size(); ++index)
            if (assignments[index].typeEnabled && assignments[index].part
                && routeMask == (std::uint32_t {1}
                    << *assignments[index].part))
                return static_cast<std::uint8_t>(index + 2);
        return std::nullopt;
    }

private:
    struct Assignment {
        bool typeEnabled {};
        std::optional<std::uint8_t> part;
    };
    std::array<Assignment, 3> assignments {};
};

// Legacy XG insertion variation uses MU insertion 1 as a bridge, but an
// explicitly configured MU insertion must remain independent of variation.
class MuVariationInsertionMirror {
public:
    void reset() noexcept { nativeInsertionOne = false; }

    std::optional<MuInsertionMessage> observe(
        std::span<const std::uint8_t> sysex) noexcept
    {
        if (sysex.size() >= 9 && sysex.front() == 0xf0
            && sysex.back() == 0xf7 && sysex[1] == 0x43
            && (sysex[2] & 0xf0) == 0x10 && sysex[3] == 0x4c
            && sysex[4] == 3 && sysex[5] == 0
            && std::none_of(sysex.begin()+1, sysex.end()-1,
                [](auto b) { return b > 127; }))
            nativeInsertionOne = true;
        return nativeInsertionOne ? std::nullopt : muInsertionForVariation(sysex);
    }

private:
    bool nativeInsertionOne {};
};

} // namespace hybrid
