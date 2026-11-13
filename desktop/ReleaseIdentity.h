#pragma once
#include <array>
#include <charconv>
#include <optional>
#include <string_view>

namespace mu_pilot {
inline constexpr std::string_view releaseRepository = "OnjLouis/mu2026-hybrid";
inline constexpr std::string_view releasePrefix = "mu2000-v";
using ReleaseVersion = std::array<unsigned, 3>;

inline std::optional<ReleaseVersion> parseReleaseTag(std::string_view tag) {
    if (!tag.starts_with(releasePrefix)) return {};
    tag.remove_prefix(releasePrefix.size());
    ReleaseVersion version{};
    for (size_t i = 0; i < version.size(); ++i) {
        const auto dot = tag.find('.');
        if ((dot == tag.npos) != (i == version.size() - 1)) return {};
        const auto part = tag.substr(0, dot);
        if (part.empty() || (part.size() > 1 && part.front() == '0')) return {};
        const auto result = std::from_chars(part.data(), part.data() + part.size(), version[i]);
        if (result.ec != std::errc{} || result.ptr != part.data() + part.size()) return {};
        if (dot != tag.npos) tag.remove_prefix(dot + 1);
    }
    return version;
}
}
