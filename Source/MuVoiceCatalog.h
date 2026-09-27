#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace hybrid {

class MuVoiceCatalog {
public:
    explicit MuVoiceCatalog(const std::filesystem::path& programRom);

    bool valid() const noexcept { return !rom.empty(); }
    bool hasDistinctVoice(std::uint8_t msb, std::uint8_t lsb,
                          std::uint8_t program) const noexcept;

private:
    std::uint32_t record(std::uint8_t msb, std::uint8_t lsb,
                         std::uint8_t program) const noexcept;

    std::vector<std::uint8_t> rom;
};

} // namespace hybrid
