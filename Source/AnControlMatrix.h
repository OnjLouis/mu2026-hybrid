#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace hybrid {
inline void anDirectControl(std::array<std::uint8_t,122>& scene, unsigned target, unsigned value) {
    struct Parameter { unsigned index, width, minimum, maximum; };
    // Direct (encoded depth 64) edits the parameter in its native range,
    // rather than multiplying a controller by a zero modulation depth.
    // Pitch (target 1) has no scene field and is not changed here.
    constexpr std::array<Parameter,47> targets {{
        {0,0,0,0},{0,0,0,0},{3,1,0,127},{4,1,0,127},{5,1,0,3},
        {7,1,0,127},{9,1,0,20},{10,2,0,255},{12,1,0,127},{13,2,0,255},
        {16,1,0,127},{17,1,0,127},{20,1,0,127},{24,1,0,127},{25,1,14,114},
        {26,1,0,127},{27,1,0,127},{28,1,0,127},{30,2,1,255},{33,1,0,127},
        {34,1,14,114},{35,1,0,127},{36,1,0,127},{37,1,0,127},{39,2,1,255},
        {41,1,0,127},{42,1,0,127},{43,1,0,127},{44,1,0,127},{45,1,0,127},
        {46,1,0,127},{47,1,0,127},{48,1,0,127},{49,1,0,127},{51,1,0,127},
        {52,1,13,127},{53,2,0,255},{57,1,0,127},{58,1,0,127},{59,1,0,127},
        {60,1,0,127},{61,1,0,127},{62,1,0,127},{63,1,0,127},{65,1,0,127},
        {66,1,1,127},{118,1,0,127}
    }};
    if (target >= targets.size()) return;
    const auto p = targets[target];
    if (!p.width) return;
    const unsigned native = p.minimum+((p.maximum-p.minimum)*std::min(value,127u)+63)/127;
    if (p.width == 2) { scene[p.index]=native/128; scene[p.index+1]=native%128; }
    else scene[p.index]=native;
}
} // namespace hybrid
