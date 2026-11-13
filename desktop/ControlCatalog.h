#pragma once

#include "ui/xg_state.h"
#include <array>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace mu_pilot {
struct Control {
    const char* id;
    const char* label;
    const char* page;
    const char* key;
};

// Keep the first fourteen IDs and their order for existing AU and VST3 projects.
inline constexpr std::array controls{
    Control{"volume", "Volume", "Mixer", "part.volume"},
    Control{"pan", "Pan", "Mixer", "part.pan"},
    Control{"reverb", "Reverb Send", "Mixer", "part.reverb_send"},
    Control{"chorus", "Chorus Send", "Mixer", "part.chorus_send"},
    Control{"variation", "Variation Send", "Mixer", "part.variation_send"},
    Control{"dry", "Dry Level", "Mixer", "part.dry_level"},
    Control{"cutoff", "Cutoff", "Filter", "part.cutoff"},
    Control{"resonance", "Resonance", "Filter", "part.resonance"},
    Control{"attack", "Attack", "Envelope", "part.attack"},
    Control{"decay", "Decay", "Envelope", "part.decay"},
    Control{"release", "Release", "Envelope", "part.release"},
    Control{"vibRate", "Rate", "Vibrato", "part.vib_rate"},
    Control{"vibDepth", "Depth", "Vibrato", "part.vib_depth"},
    Control{"vibDelay", "Delay", "Vibrato", "part.vib_delay"},
    Control{"hpf", "High-pass Cutoff", "Filter", "part.hpf_cutoff"},
    Control{"noteShift", "Note Shift", "Tuning", "part.note_shift"},
    Control{"detune", "Detune", "Tuning", "part.detune"},
    Control{"bendPitch", "Bend Range", "Tuning", "part.bend_pitch"},
    Control{"monoPoly", "Voice Mode", "Tuning", "part.mono_poly"},
    Control{"portaSwitch", "Portamento", "Tuning", "part.porta_switch"},
    Control{"portaTime", "Glide Time", "Tuning", "part.porta_time"},
    Control{"noteLow", "Low Note", "Key and Velocity", "part.note_low"},
    Control{"noteHigh", "High Note", "Key and Velocity", "part.note_high"},
    Control{"velDepth", "Velocity Depth", "Key and Velocity", "part.vel_depth"},
    Control{"velOffset", "Velocity Offset", "Key and Velocity", "part.vel_offset"},
    Control{"velLow", "Low Velocity", "Key and Velocity", "part.vel_limit_low"},
    Control{"velHigh", "High Velocity", "Key and Velocity", "part.vel_limit_high"},
    Control{"pegInit", "Initial Level", "Pitch Envelope", "part.peg_init_level"},
    Control{"pegAttack", "Attack Time", "Pitch Envelope", "part.peg_attack_time"},
    Control{"pegRelLevel", "Release Level", "Pitch Envelope", "part.peg_rel_level"},
    Control{"pegRelTime", "Release Time", "Pitch Envelope", "part.peg_rel_time"},
    Control{"partEqBassGain", "Bass Gain", "Part EQ", "part.eq_bass_gain"},
    Control{"partEqBassFreq", "Bass Frequency", "Part EQ", "part.eq_bass_freq"},
    Control{"partEqTrebleGain", "Treble Gain", "Part EQ", "part.eq_treble_gain"},
    Control{"partEqTrebleFreq", "Treble Frequency", "Part EQ", "part.eq_treble_freq"},
    Control{"mwPitch", "Pitch", "Mod Wheel", "part.mw_pitch"},
    Control{"mwFilter", "Filter", "Mod Wheel", "part.mw_filter"},
    Control{"mwAmp", "Amplitude", "Mod Wheel", "part.mw_amp"},
    Control{"mwLfoPitch", "LFO Pitch", "Mod Wheel", "part.mw_lfo_pmod"},
    Control{"mwLfoFilter", "LFO Filter", "Mod Wheel", "part.mw_lfo_fmod"},
    Control{"mwLfoAmp", "LFO Amplitude", "Mod Wheel", "part.mw_lfo_amod"},
    Control{"catPitch", "Pitch", "Aftertouch", "part.cat_pitch"},
    Control{"catFilter", "Filter", "Aftertouch", "part.cat_filter"},
    Control{"catAmp", "Amplitude", "Aftertouch", "part.cat_amp"},
    Control{"catLfoPitch", "LFO Pitch", "Aftertouch", "part.cat_lfo_pmod"},
    Control{"catLfoFilter", "LFO Filter", "Aftertouch", "part.cat_lfo_fmod"},
    Control{"catLfoAmp", "LFO Amplitude", "Aftertouch", "part.cat_lfo_amod"},
    Control{"reverbReturn", "Return", "Reverb", "reverb.return"},
    Control{"reverbPan", "Pan", "Reverb", "reverb.pan"},
    Control{"chorusReturn", "Return", "Chorus", "chorus.return"},
    Control{"chorusPan", "Pan", "Chorus", "chorus.pan"},
    Control{"chorusToReverb", "To Reverb", "Chorus", "chorus.to_reverb"},
    Control{"varReturn", "Return", "Variation", "variation.return"},
    Control{"varPan", "Pan", "Variation", "variation.pan"},
    Control{"varToReverb", "To Reverb", "Variation", "variation.to_reverb"},
    Control{"varToChorus", "To Chorus", "Variation", "variation.to_chorus"},
    Control{"varConnection", "Connection", "Variation", "variation.connect"},
    Control{"masterVolume", "Master Volume", "Master", "system.master_volume"},
    Control{"masterTranspose", "Transpose", "Master", "system.transpose"},
    Control{"masterEqType", "Type", "Master EQ Low", "master_eq.type"},
    Control{"masterEqGain1", "Band 1 Gain", "Master EQ Low", "master_eq.gain1"},
    Control{"masterEqFreq1", "Band 1 Frequency", "Master EQ Low", "master_eq.freq1"},
    Control{"masterEqQ1", "Band 1 Q", "Master EQ Low", "master_eq.q1"},
    Control{"masterEqShape1", "Band 1 Shape", "Master EQ Low", "master_eq.shape1"},
    Control{"masterEqGain2", "Band 2 Gain", "Master EQ Low", "master_eq.gain2"},
    Control{"masterEqFreq2", "Band 2 Frequency", "Master EQ Low", "master_eq.freq2"},
    Control{"masterEqQ2", "Band 2 Q", "Master EQ Low", "master_eq.q2"},
    Control{"masterEqGain3", "Band 3 Gain", "Master EQ High", "master_eq.gain3"},
    Control{"masterEqFreq3", "Band 3 Frequency", "Master EQ High", "master_eq.freq3"},
    Control{"masterEqQ3", "Band 3 Q", "Master EQ High", "master_eq.q3"},
    Control{"masterEqGain4", "Band 4 Gain", "Master EQ High", "master_eq.gain4"},
    Control{"masterEqFreq4", "Band 4 Frequency", "Master EQ High", "master_eq.freq4"},
    Control{"masterEqQ4", "Band 4 Q", "Master EQ High", "master_eq.q4"},
    Control{"masterEqGain5", "Band 5 Gain", "Master EQ Top", "master_eq.gain5"},
    Control{"masterEqFreq5", "Band 5 Frequency", "Master EQ Top", "master_eq.freq5"},
    Control{"masterEqQ5", "Band 5 Q", "Master EQ Top", "master_eq.q5"},
    Control{"masterEqShape5", "Band 5 Shape", "Master EQ Top", "master_eq.shape5"},
    Control{"partMode", "Part Mode", "Routing", "part.mode"},
    Control{"keyAssign", "Key Assign", "Routing", "part.key_assign"},
    Control{"reserve", "Element Reserve", "Routing", "part.element_reserve"},
    Control{"patPitch", "Pitch", "Poly Aftertouch", "part.pat_pitch"},
    Control{"patFilter", "Filter", "Poly Aftertouch", "part.pat_filter"},
    Control{"patAmp", "Amplitude", "Poly Aftertouch", "part.pat_amp"},
    Control{"patLfoPitch", "LFO Pitch", "Poly Aftertouch", "part.pat_lfo_pmod"},
    Control{"patLfoFilter", "LFO Filter", "Poly Aftertouch", "part.pat_lfo_fmod"},
    Control{"patLfoAmp", "LFO Amplitude", "Poly Aftertouch", "part.pat_lfo_amod"},
    Control{"ac1Cc", "MIDI CC", "Controller 1", "part.ac1_cc"},
    Control{"ac1Pitch", "Pitch", "Controller 1", "part.ac1_pitch"},
    Control{"ac1Filter", "Filter", "Controller 1", "part.ac1_filter"},
    Control{"ac1Amp", "Amplitude", "Controller 1", "part.ac1_amp"},
    Control{"ac1LfoPitch", "LFO Pitch", "Controller 1", "part.ac1_lfo_pmod"},
    Control{"ac1LfoFilter", "LFO Filter", "Controller 1", "part.ac1_lfo_fmod"},
    Control{"ac1LfoAmp", "LFO Amplitude", "Controller 1", "part.ac1_lfo_amod"},
    Control{"ac2Cc", "MIDI CC", "Controller 2", "part.ac2_cc"},
    Control{"ac2Pitch", "Pitch", "Controller 2", "part.ac2_pitch"},
    Control{"ac2Filter", "Filter", "Controller 2", "part.ac2_filter"},
    Control{"ac2Amp", "Amplitude", "Controller 2", "part.ac2_amp"},
    Control{"ac2LfoPitch", "LFO Pitch", "Controller 2", "part.ac2_lfo_pmod"},
    Control{"ac2LfoFilter", "LFO Filter", "Controller 2", "part.ac2_lfo_fmod"},
    Control{"ac2LfoAmp", "LFO Amplitude", "Controller 2", "part.ac2_lfo_amod"},
    Control{"reverbType", "Type", "Reverb", "reverb.type"},
    Control{"chorusType", "Type", "Chorus", "chorus.type"},
    Control{"variationType", "Type", "Variation", "variation.type"},
    Control{"insertion1Type", "Type", "Insertion 1", "insertion1.type"},
    Control{"insertion2Type", "Type", "Insertion 2", "insertion2.type"},
    Control{"insertion3Type", "Type", "Insertion 3", "insertion3.type"},
    Control{"insertion4Type", "Type", "Insertion 4", "insertion4.type"},
};

inline bool effectType(const xg::param& p) { return p.where == xg::area::effect && p.size == 2; }

inline const auto& definitions() {
    static const auto result = [] {
        std::array<const xg::param*, controls.size()> values{};
        std::unordered_set<std::string> ids;
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i] = xg::find(controls[i].key);
            if (!values[i] || values[i]->special >= 0 || values[i]->size > 4 || !ids.insert(controls[i].id).second)
                throw std::logic_error("Invalid MU2000 control catalogue");
        }
        return values;
    }();
    return result;
}

struct Message { std::array<u8, 12> data{}; std::size_t size = 0; };

inline Message controlMessage(const xg::param& p, int part, int value) {
    const auto address = xg::address(p, part);
    Message msg{{0xf0, 0x43, 0x10, 0x4c, u8(address >> 14), u8((address >> 7) & 127), u8(address & 127)},
                std::size_t(8 + p.size)};
    const int bits = p.enc == xg::coding::nibble ? 4 : 7;
    for (int i = 0; i < p.size; ++i)
        msg.data[7 + i] = u8((value >> (bits * (p.size - i - 1))) & ((1 << bits) - 1));
    msg.data[msg.size - 1] = 0xf7;
    return msg;
}
}
