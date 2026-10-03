#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace hybrid {
constexpr unsigned anEgTrackSamples = 192;
constexpr unsigned anEgTracks = 4;

inline double anEgDuration(const std::array<std::uint8_t,104>& common) {
    const unsigned length = common[94];
    if (length >= 10) return length <= 80 ? length/10.0 : 8+(length-80)*0.5;
    constexpr std::array<double,10> bars {0,0,0.5,1,1.5,2,3,4,6,8};
    const unsigned tempo = common[16]*128+common[17];
    return bars[length]*240/(tempo >= 40 ? tempo : 120);
}

inline double anEgPosition(double cycles, unsigned loop) {
    if (cycles < 1) return std::clamp(cycles,0.0,1.0);
    switch (loop) {
    case 1: return std::fmod(cycles,1.0);
    case 2: return 0.5+std::fmod(cycles-1,0.5);
    case 3: return 1-std::abs(std::fmod(cycles,2.0)-1);
    case 4: return 0.5+std::abs(std::fmod(cycles-1,1.0)-0.5);
    default: return 1;
    }
}

inline std::array<std::uint8_t,122> anEgScene(
    const std::array<std::uint8_t,122>& base,
    const std::array<std::uint8_t,104>& common,
    const std::array<std::uint8_t,768>& data, double elapsed) {
    struct Parameter { unsigned index, width, maximum; };
    // Free EG's target numbering differs from the control matrix (Yamaha
    // PLG150-AN data list, pages 49 and 60-62). Two-byte values are 7-bit pairs.
    constexpr std::array<Parameter,60> targets {{
        {0,0,0},{3,1,127},{4,1,127},{5,1,3},{7,1,127},{9,1,20},
        {10,2,255},{12,1,127},{13,2,255},{15,1,2},{16,1,127},
        {17,1,127},{18,1,4},{19,1,3},{20,1,127},{21,1,4},
        {22,1,7},{23,1,6},{24,1,127},{25,1,114},{26,1,127},
        {27,1,127},{28,1,127},{29,1,7},{30,2,255},{32,1,5},
        {33,1,127},{34,1,114},{35,1,127},{36,1,127},{37,1,127},
        {38,1,7},{39,2,255},{41,1,127},{42,1,127},{43,1,127},
        {44,1,127},{45,1,127},{46,1,127},{47,1,127},{48,1,127},
        {49,1,127},{50,1,5},{51,1,127},{52,1,127},{53,2,255},
        {55,1,127},{56,1,127},{57,1,127},{58,1,127},{59,1,127},
        {60,1,127},{61,1,127},{62,1,127},{63,1,127},{64,1,127},
        {65,1,127},{118,1,127},{119,1,20},{121,1,3}
    }};
    auto scene = base;
    const double duration = anEgDuration(common);
    if (duration <= 0) return scene;
    const double position = anEgPosition(elapsed/duration,common[93])*(anEgTrackSamples-1);
    const unsigned first = unsigned(position), second = std::min(first+1,anEgTrackSamples-1);
    for (unsigned track = 0; track < anEgTracks; ++track) {
        const unsigned target = common[96+track*2];
        if (!target || target >= targets.size() || !(common[97+track*2]&1)) continue;
        const auto p = targets[target];
        const unsigned start = track*anEgTrackSamples;
        // The ROM stores unsigned 8-bit knob displacement, centred on 128,
        // not absolute settings. Neutral data must leave the preset unchanged.
        const double delta = int(data[start+first])-128
            +(int(data[start+second])-int(data[start+first]))*(position-first);
        const unsigned original = p.width == 2 ? base[p.index]*128+base[p.index+1] : base[p.index];
        const unsigned value = unsigned(std::clamp(int(std::lround(original+delta)),0,int(p.maximum)));
        if (p.width == 2) { scene[p.index] = value/128; scene[p.index+1] = value%128; }
        else scene[p.index] = value;
    }
    return scene;
}
} // namespace hybrid
