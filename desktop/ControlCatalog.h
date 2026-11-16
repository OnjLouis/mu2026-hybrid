#pragma once

#include "ui/xg_state.h"
#include "xg/sysfx.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace mu_pilot {
enum class Target { standard, reverb, chorus, variation, insert1, insert2, insert3, insert4, drumNote, drum };

struct Control {
    const char* id;
    const char* label;
    const char* page;
    const char* key;
    Target target = Target::standard;
    int slot = 0;
    int min = 0, max = 127, initial = 0;
    xg::view display = xg::view::raw;
};

#define FX(tag, group, page, n) Control{#tag "Fx" #n, "Parameter " #n, page, nullptr, Target::group, n}
#define FX_1_8(tag, page) \
    FX(tag, tag, page, 1), FX(tag, tag, page, 2), FX(tag, tag, page, 3), FX(tag, tag, page, 4), \
    FX(tag, tag, page, 5), FX(tag, tag, page, 6), FX(tag, tag, page, 7), FX(tag, tag, page, 8)
#define FX_11_16(tag, page) \
    FX(tag, tag, page, 11), FX(tag, tag, page, 12), FX(tag, tag, page, 13), \
    FX(tag, tag, page, 14), FX(tag, tag, page, 15), FX(tag, tag, page, 16)
#define FX_1_16(tag, page) FX_1_8(tag, page " A"), FX(tag, tag, page " B", 9), \
    FX(tag, tag, page " B", 10), FX_11_16(tag, page " B")
#define DRUM(id, label, page, addr, low, high, def, display) \
    Control{id, label, "Drum " page, nullptr, Target::drum, addr, low, high, def, xg::view::display}

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
    Control{"variationPart", "Target", "Variation", "variation.part"},
    Control{"insertion1Part", "Target", "Insertion 1", "insertion1.part"},
    Control{"insertion2Part", "Target", "Insertion 2", "insertion2.part"},
    Control{"insertion3Part", "Target", "Insertion 3", "insertion3.part"},
    Control{"insertion4Part", "Target", "Insertion 4", "insertion4.part"},
    FX_1_8(reverb, "Reverb A"), FX(reverb, reverb, "Reverb B", 9), FX_11_16(reverb, "Reverb B"),
    FX_1_8(chorus, "Chorus A"), FX(chorus, chorus, "Chorus B", 9),
    FX_1_16(variation, "Variation FX"),
    FX_1_16(insert1, "Insertion 1 FX"), FX_1_16(insert2, "Insertion 2 FX"),
    FX_1_16(insert3, "Insertion 3 FX"), FX_1_16(insert4, "Insertion 4 FX"),
    Control{"drumNote", "Note", "Drum Mixer", nullptr, Target::drumNote, 0,
        ui::XG_DRUM_KEY0, ui::XG_DRUM_KEY0 + ui::XG_DRUM_KEYS - 1, 36},
    DRUM("drumLevel", "Level", "Mixer", 0x02, 0, 127, 64, raw),
    DRUM("drumPan", "Pan", "Mixer", 0x04, 0, 127, 64, pan),
    DRUM("drumReverb", "Reverb Send", "Mixer", 0x05, 0, 127, 64, raw),
    DRUM("drumChorus", "Chorus Send", "Mixer", 0x06, 0, 127, 64, raw),
    DRUM("drumVariation", "Variation Send", "Mixer", 0x07, 0, 127, 64, raw),
    DRUM("drumPitch", "Coarse Tune", "Shape", 0x00, 0, 127, 64, center),
    DRUM("drumFine", "Fine Tune", "Shape", 0x01, 0, 127, 64, center),
    DRUM("drumAttack", "Attack", "Shape", 0x0d, 0, 127, 64, center),
    DRUM("drumDecay1", "Decay 1", "Shape", 0x0e, 0, 127, 64, center),
    DRUM("drumDecay2", "Decay 2", "Shape", 0x0f, 0, 127, 64, center),
    DRUM("drumVelPitch", "Velocity to Pitch", "Shape", 0x60, 0x30, 0x50, 64, center),
    DRUM("drumCutoff", "Cutoff", "Filter", 0x0b, 0, 127, 64, center),
    DRUM("drumResonance", "Resonance", "Filter", 0x0c, 0, 127, 64, center),
    DRUM("drumHighpass", "High-pass", "Filter", 0x50, 0, 127, 64, center),
    DRUM("drumVelCutoff", "Velocity to Cutoff", "Filter", 0x61, 0x30, 0x50, 64, center),
    DRUM("drumLowGain", "Low Gain", "EQ", 0x20, 0x34, 0x4c, 64, center),
    DRUM("drumHighGain", "High Gain", "EQ", 0x21, 0x34, 0x4c, 64, center),
    DRUM("drumLowFrequency", "Low Frequency", "EQ", 0x24, 4, 40, 12, raw),
    DRUM("drumHighFrequency", "High Frequency", "EQ", 0x25, 28, 58, 54, raw),
    DRUM("drumAlternate", "Alternate Group", "Trigger", 0x03, 0, 127, 0, raw),
    DRUM("drumAssign", "Voice Assignment", "Trigger", 0x08, 0, 1, 0, choice),
    DRUM("drumNoteOff", "Receive Note Off", "Trigger", 0x09, 0, 1, 0, choice),
    DRUM("drumNoteOn", "Receive Note On", "Trigger", 0x0a, 0, 1, 1, choice),
};
#undef DRUM
#undef FX_1_16
#undef FX_11_16
#undef FX_1_8
#undef FX

inline constexpr std::size_t controlIndex(std::string_view id) {
    for (std::size_t i = 0; i < controls.size(); ++i)
        if (id == controls[i].id) return i;
    return controls.size();
}
inline constexpr auto drumNoteIndex = controlIndex("drumNote");
inline bool effectSlot(const Control& c) { return c.target >= Target::reverb && c.target <= Target::insert4; }
inline bool drumControl(const Control& c) { return c.target == Target::drum || c.target == Target::drumNote; }
inline constexpr int routingOff = 66;
inline constexpr auto drumModeOffset = 0x07;
inline constexpr auto firstEditableDrumMode = 2;

inline bool effectType(const xg::param& p) {
    return p.where == xg::area::effect && p.size == 2;
}

inline const auto& definitions() {
    static const auto result = [] {
        constexpr const char* assign[]{"Single", "Multi"};
        constexpr const char* toggle[]{"Off", "On"};
        // Choice strings need to outlive the catalogue.
        static constexpr auto drumAssign = std::to_array(assign);
        static constexpr auto drumToggle = std::to_array(toggle);
        std::array<xg::param, controls.size()> values{};
        std::unordered_set<std::string> ids;
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto& c = controls[i];
            if (!ids.insert(c.id).second) throw std::logic_error("Duplicate MU2000 control");
            if (!c.key) {
                const bool wide = effectSlot(c) && c.target >= Target::variation && c.slot <= 10;
                values[i] = {c.id, c.label, xg::area::system, 0, 0, 0, 1, xg::coding::byte7,
                    c.min, wide ? 0x3fff : c.max, -1, c.initial, c.display, 64,
                    c.slot == 0x08 ? drumAssign.data() : drumToggle.data()};
                continue;
            }
            const auto* source = xg::find(c.key);
            if (!source || source->size > 4) throw std::logic_error("Invalid MU2000 control catalogue");
            values[i] = *source;
            if (source->how == xg::view::part_off) {
                values[i].max = routingOff;
                values[i].def = routingOff;
                values[i].special = -1;
            }
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

inline int fxSlotNumber(const xg::fx_param& p) {
    if (p.addr >= 0x02 && p.addr <= 0x0b) return p.addr - 0x02 + 1;
    if (p.addr >= 0x30 && p.addr <= 0x43) return (p.addr - 0x30) / 2 + 1;
    if (p.addr >= 0x20 && p.addr <= 0x25) return p.addr - 0x20 + 11;
    return 0;
}

struct EffectBinding {
    const xg::fx_param* parameter = nullptr;
    u8 hi = 0, mid = 0, lo = 0, size = 0;
};

inline int encodeEffectValue(const xg::fx_param& effect, int hostMax, int value) {
    const int span = effect.hi - effect.lo;
    return span ? ((std::clamp(value, int(effect.lo), int(effect.hi)) - effect.lo) * hostMax + span / 2) / span : 0;
}

inline int decodeEffectValue(const xg::fx_param& effect, int hostMax, int value) {
    return effect.lo + (value * (effect.hi - effect.lo) + hostMax / 2) / hostMax;
}

inline EffectBinding effectBinding(const Control& c, const ui::xg_snapshot& snapshot, int typeOverride = -1) {
    const auto group = int(c.target) - int(Target::reverb);
    const bool system = group < 3;
    const u8 hi = system ? 2 : 3, mid = system ? 1 : u8(group - 3);
    const u8 lo = system ? xg::sysfx_type_lo(xg::sysfx(group)) : 0;
    const auto* type = ui::locate_byte(snapshot, hi, mid, lo);
    const auto* def = xg::fx_find(typeOverride >= 0 ? typeOverride : (int(type[0]) << 7) | type[1]);
    if (!def) return {};
    for (int i = 0; i < def->count; ++i) {
        const auto& parameter = def->params[i];
        if (fxSlotNumber(parameter) != c.slot) continue;
        int size = parameter.size;
        const auto address = system ? xg::sysfx_addr(xg::sysfx(group), parameter, size) : parameter.addr;
        if (address >= 0) return {&parameter, hi, mid, u8(address), u8(size)};
    }
    return {};
}

inline int drumSetup(const ui::xg_snapshot& snapshot, int part) {
    const int set = int(snapshot.parts[part][drumModeOffset]) - firstEditableDrumMode;
    return set >= 0 && set < ui::XG_DRUM_SETS ? set : -1;
}

inline bool validDrumNote(int note) {
    return note >= ui::XG_DRUM_KEY0 && note < ui::XG_DRUM_KEY0 + ui::XG_DRUM_KEYS;
}

inline bool readControl(const Control& c, int part, int note, const ui::xg_snapshot& snapshot, int& result) {
    const auto& p = definitions()[std::size_t(&c - controls.data())];
    if (c.target == Target::drumNote) { result = note; return true; }
    if (c.target == Target::drum) {
        const int set = drumSetup(snapshot, part);
        if (set < 0 || !validDrumNote(note)) return false;
        result = snapshot.drum[set][note - ui::XG_DRUM_KEY0][xg::ram::drum_setup_index(c.slot)];
        return xg::valid(p, result);
    }
    if (effectSlot(c)) {
        const auto binding = effectBinding(c, snapshot);
        if (!binding.parameter) return false;
        if (binding.size == 2) {
            u8 bytes[20];
            if (binding.hi == 2) ui::var_wide_bytes(snapshot, bytes);
            else ui::ins_wide_bytes(snapshot, binding.mid, bytes);
            const int offset = 2 * (c.slot - 1);
            result = (int(bytes[offset]) << 7) | bytes[offset + 1];
        } else {
            const auto* raw = ui::locate_byte(snapshot, binding.hi, binding.mid, binding.lo);
            if (!raw) return false;
            result = *raw;
        }
        return result >= binding.parameter->lo && result <= binding.parameter->hi;
    }
    if (!ui::read_value(p, part, snapshot, result)) return false;
    if (p.how == xg::view::part_off && result == 127) result = routingOff;
    return xg::valid(p, result);
}

inline Message writeControl(const Control& c, int part, int note, const ui::xg_snapshot& snapshot,
                            int value, int typeOverride = -1) {
    auto p = definitions()[std::size_t(&c - controls.data())];
    if (!xg::valid(p, value) || c.target == Target::drumNote) return {};
    if (c.target == Target::drum) {
        const int set = drumSetup(snapshot, part);
        if (set < 0 || !validDrumNote(note)) return {};
        p.hi = u8(0x30 + set); p.mid = u8(note); p.lo = u8(c.slot);
    } else if (effectSlot(c)) {
        const auto binding = effectBinding(c, snapshot, typeOverride);
        if (!binding.parameter) return {};
        p.hi = binding.hi; p.mid = binding.mid; p.lo = binding.lo; p.size = binding.size;
        value = std::clamp(value, int(binding.parameter->lo), int(binding.parameter->hi));
    } else if (p.how == xg::view::part_off && value == routingOff) {
        value = 127;
    }
    return controlMessage(p, part, value);
}
}
