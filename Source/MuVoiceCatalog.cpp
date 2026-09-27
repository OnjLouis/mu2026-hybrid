#include "MuVoiceCatalog.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace hybrid {
namespace {

// MU2000 EX program-ROM tables, identified and checked by S-MU2000.
constexpr std::size_t romSize = 0x400000;
constexpr std::size_t voiceStart = 0x200ee0;
constexpr std::size_t voiceEnd = 0x23cece;
constexpr std::size_t xgGroups = 0x283950;
constexpr std::size_t lsbGroupNative = 0x2839d0;
constexpr std::size_t lsbGroup77 = 0x283ad0;
constexpr std::size_t lsbGroupC9Native = 0x292640;
constexpr std::size_t voiceTable = 0x267f50;

} // namespace

MuVoiceCatalog::MuVoiceCatalog(const std::filesystem::path& programRom)
{
    std::ifstream input(programRom, std::ios::binary);
    if (!input)
        return;
    rom.assign(std::istreambuf_iterator<char>(input), {});
    if (input.bad() || rom.size() != romSize
        || std::memcmp(rom.data() + voiceStart + 2, "GrandPno", 8) != 0) {
        rom.clear();
    }
}

std::uint32_t MuVoiceCatalog::record(std::uint8_t msb,
                                     std::uint8_t lsb,
                                     std::uint8_t program) const noexcept
{
    if (!valid())
        return 0;
    const auto kind = rom[xgGroups + msb];
    std::uint8_t group = kind;
    if (kind == 0)
        group = rom[lsbGroupNative + lsb];
    else if (kind == 77)
        group = rom[lsbGroup77 + lsb];
    else if (kind == 0xc9)
        group = rom[lsbGroupC9Native + lsb];
    const auto slot = voiceTable + static_cast<std::size_t>(group) * 512
        + static_cast<std::size_t>(program) * 4;
    if (slot + 4 > rom.size())
        return 0;
    const auto offset = (static_cast<std::uint32_t>(rom[slot]) << 24)
        | (static_cast<std::uint32_t>(rom[slot + 1]) << 16)
        | (static_cast<std::uint32_t>(rom[slot + 2]) << 8)
        | rom[slot + 3];
    const auto result = static_cast<std::uint64_t>(voiceStart)
        + static_cast<std::uint64_t>(offset) * 2;
    return result >= voiceStart && result + 16 <= voiceEnd ? result : 0;
}

bool MuVoiceCatalog::hasDistinctVoice(std::uint8_t msb,
                                      std::uint8_t lsb,
                                      std::uint8_t program) const noexcept
{
    if (!valid() || msb == 126 || msb == 127)
        return true;
    const auto selected = record(msb, lsb, program);
    if (msb == 0 && lsb == 0)
        return true;
    return selected != 0 && selected != record(0, 0, program);
}

} // namespace hybrid
