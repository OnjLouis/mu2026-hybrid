#pragma once
#include <algorithm>
#include <cmath>

namespace hybrid {
// Yamaha specifies the directions/ranges, but not the DSP transfer curves.
// These are explicit candidate calibrations, not recovered AN coefficients.
inline constexpr float anFilterEgTimeScale = 4.0f;
inline constexpr float anFilterEgSemitonesPerUnit = 1.0f;
inline constexpr float anFilterResonanceUnitsPerOctave = 24.0f;
inline float anFilterDamping(float resonance) {
    // Preserve the full documented -12..102 range. The previous damping
    // floor made roughly the upper quarter of positive resonance identical.
    return 1.41421356f*std::exp2(-std::clamp(resonance,-12.0f,102.0f)/anFilterResonanceUnitsPerOctave);
}
inline float anFilterVelocity(float sensitivity, unsigned velocity) {
    return std::clamp(1+sensitivity/64*(velocity/127.0f-1),0.0f,2.0f);
}
struct AnOnePole {
    float state {};
    float lowPass(float input, float g) {
        const float v = (input-state)*g/(1+g);
        const float output = state+v;
        state = output+v;
        return output;
    }
    float highPass(float input, float g) { return input-lowPass(input,g); }
};
}
