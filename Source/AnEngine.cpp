#include "AnEngine.h"
#include "AnFreeEg.h"
#include "AnFilter.h"
#include "AnControlMatrix.h"
#include "MidiSystemReset.h"
#include "XgVariationRouting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace hybrid {
namespace {
constexpr unsigned partCount = 16;
constexpr unsigned notesPerPart = 5;
constexpr unsigned voiceLimit = partCount * notesPerPart;
constexpr unsigned eventLimit = 8192;
constexpr unsigned sysexLimit = 2 * 1024 * 1024;
constexpr float pi = 3.14159265358979323846f;
constexpr float outputScale = 0.075f;
constexpr unsigned matrixAfterTouch = 96;
constexpr unsigned matrixPitchBend = 97;
constexpr unsigned matrixKeyTrack = 98;
constexpr unsigned matrixVelocity = 99;
constexpr unsigned distortionLpfFirst = 34;
constexpr unsigned distortionLpfThru = 60;
constexpr unsigned sequenceBaseNote = 48; // C2 in Yamaha's note-name convention.
constexpr std::array<double,10> sequenceBeats {1.5,1,2.0/3,0.75,0.5,1.0/3,0.375,0.25,1.0/6,0.125};
// Yamaha effect frequency table; amplifier/filter topology remains approximate.
constexpr std::array<float,26> distortionLpfHz {
    1000,1100,1200,1400,1600,1800,2000,2200,2500,2800,3200,3600,4000,
    4500,5000,5600,6300,7000,8000,9000,10000,11000,12000,14000,16000,18000
};
unsigned key(unsigned msb, unsigned lsb, unsigned pc) { return (msb << 14) | (lsb << 7) | pc; }
bool anBank(unsigned msb) { return msb == 36 || msb == 84 || msb == 100; }
struct Preset {
    std::array<std::uint8_t,104> common {};
    std::array<std::uint8_t,122> scene {};
    std::array<std::uint8_t,70> sequence {};
    std::array<std::uint8_t,768> freeEg {};
    bool hasFreeEg {};
};
std::vector<std::uint8_t> readBank(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > sysexLimit) throw std::runtime_error("AN bank is too large");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open AN bank");
    return {std::istreambuf_iterator<char>(input), {}};
}
float blep(float phase, float step) {
    if (phase < step) { const auto t = phase / step; return t + t - t * t - 1; }
    if (phase > 1 - step) { const auto t = (phase - 1) / step; return t * t + t + t + 1; }
    return 0;
}
float oscillator(unsigned wave, float phase, float step, float width, float noise) {
    const auto saw = 2 * phase - 1 - blep(phase,step);
    const auto shifted = phase >= width ? phase - width : phase + 1 - width;
    const auto pulse = (phase < width ? 1.0f : -1.0f) + blep(phase,step) - blep(shifted,step);
    // Retain the reference-tested AN saw pitch law. A neutral-ramp model
    // inferred from the AN1x diagram regressed the recovered Wonder bass
    // by an octave; this PW transfer curve still needs hardware calibration.
    const auto pwmSaw = saw+0.5f*pulse;
    switch (wave) {
    case 0: return pwmSaw;
    case 1: return pulse;
    // Saw2 retains the fundamental while PW changes its even harmonics.
    // The precise chip transfer curve is unknown; retain a full saw component.
    case 2: return 0.5f*(saw+pwmSaw);
    case 3: return 0.5f * (saw + pulse);
    case 4: return 1 - 4 * std::abs(phase - 0.5f);
    case 5: return std::sin(2 * pi * phase);
    default: return noise;
    }
}
float oscillatorWithEdge(unsigned wave, float phase, float step, float width, float noise, float edge) {
    const auto sharp = oscillator(wave,phase,step,width,noise);
    if (wave >= 5 || edge >= 127) return sharp;
    // Both documented endpoints are fixed. Intermediate Edge uses a spectral
    // crossfade, not a claim to reproduce Yamaha's unpublished oscillator DSP.
    const auto sine = -std::sin(2*pi*phase);
    return sine+(sharp-sine)*std::clamp(edge/127,0.0f,1.0f);
}
float seconds(unsigned value) { return 0.001f * std::exp2(value * (13.0f / 127)); }
float lfoWave(unsigned wave, float phase, float held) {
    const float sine = std::sin(2*pi*phase);
    const float triangle = 1-4*std::abs(std::fmod(phase+0.25f,1.0f)-0.5f);
    switch (wave) {
    case 0: return sine;
    case 1: return std::max(0.0f,sine);
    case 2: return (sine+1)*0.5f;
    case 3: return std::max(0.0f,-sine);
    case 4: return (1-sine)*0.5f;
    case 5: return triangle;
    case 6: return std::max(0.0f,triangle);
    case 7: return (triangle+1)*0.5f;
    case 8: return std::max(0.0f,-triangle);
    case 9: return (1-triangle)*0.5f;
    case 10: return phase<0.5f ? 1 : -1;
    case 11: return phase<0.5f ? 1 : 0;
    case 12: return phase<0.5f ? 0 : 1;
    case 13: return 1-2*phase;
    case 14: return 1-phase;
    case 15: return 2*phase-1;
    case 16: return phase;
    case 17: return held;
    case 18: return (held+1)*0.5f;
    // Yamaha describes S/H2 as positively biased, without publishing its
    // distribution. This remains an approximation, not their DSP algorithm.
    case 19: return std::clamp(held+0.35f,-1.0f,1.0f);
    case 20: return (std::clamp(held+0.35f,-1.0f,1.0f)+1)*0.5f;
    default: return 0;
    }
}
float advanceLfo(float& phase, std::uint32_t& random, float& held, unsigned wave, float step) {
    phase += step;
    if (phase >= 1) {
        phase -= std::floor(phase);
        random ^= random<<13; random ^= random>>17; random ^= random<<5;
        held = int(random)/2147483648.0f;
    }
    return lfoWave(wave,phase,held);
}
struct Envelope {
    unsigned stage {};
    float value {}, releaseStep {};
    void release(float time, float rate) { if (stage < 3) { stage = 3; releaseStep = value / (time * rate); } }
    float next(unsigned attack, unsigned decay, unsigned sustain, float rate, float timeScale = 1) {
        const float target = sustain / 127.0f;
        if (stage == 0) { value += 1 / (seconds(attack) * timeScale * rate); if (value >= 1) { value = 1; stage = 1; } }
        else if (stage == 1) { value -= (1 - target) / (seconds(decay) * timeScale * rate); if (value <= target) { value = target; stage = 2; } }
        else if (stage == 2) value = target;
        else { value = std::max(0.0f,value-releaseStep); }
        return value;
    }
};
struct Filter {
    float ic1 {}, ic2 {};
    float next(float input, float g, float damping, unsigned type) {
        const float a1 = 1 / (1 + g * (g + damping));
        const float v1 = a1 * (ic1 + g * (input - ic2));
        const float v2 = ic2 + g * v1;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        const auto high = input - damping * v1 - v2;
        return type == 3 ? v1 : type == 4 ? high : type == 5 ? high + v2 : v2;
    }
};
struct Equalizer {
    float b0 {1}, b1 {}, b2 {}, a1 {}, a2 {}, z1 {}, z2 {};
    float previousFrequency {-1}, previousGain {}, previousQ {}, previousRate {};
    float next(float input, unsigned type, float frequency, float gain, float q, float rate) {
        frequency = std::clamp(frequency,20.0f,rate*0.4f);
        if (frequency != previousFrequency || gain != previousGain || q != previousQ || rate != previousRate) {
            previousFrequency = frequency; previousGain = gain; previousQ = q; previousRate = rate;
            const float a = std::pow(10.0f,gain/40), w = 2*pi*frequency/rate;
            const float cosine = std::cos(w), sine = std::sin(w);
            float denominator;
            if (type == 1) {
                const float alpha = sine/(2*q);
                denominator = 1+alpha/a;
                b0 = 1+alpha*a; b1 = -2*cosine; b2 = 1-alpha*a;
                a1 = -2*cosine; a2 = 1-alpha/a;
            } else {
                const float beta = std::sqrt(2*a)*sine;
                if (!type) {
                    denominator = (a+1)+(a-1)*cosine+beta;
                    b0 = a*((a+1)-(a-1)*cosine+beta);
                    b1 = 2*a*((a-1)-(a+1)*cosine);
                    b2 = a*((a+1)-(a-1)*cosine-beta);
                    a1 = -2*((a-1)+(a+1)*cosine); a2 = (a+1)+(a-1)*cosine-beta;
                } else {
                    denominator = (a+1)-(a-1)*cosine+beta;
                    b0 = a*((a+1)+(a-1)*cosine+beta);
                    b1 = -2*a*((a-1)+(a+1)*cosine);
                    b2 = a*((a+1)+(a-1)*cosine-beta);
                    a1 = 2*((a-1)-(a+1)*cosine); a2 = (a+1)-(a-1)*cosine-beta;
                }
            }
            b0 /= denominator; b1 /= denominator; b2 /= denominator; a1 /= denominator; a2 /= denominator;
        }
        const float output = b0*input+z1;
        z1 = b1*input-a1*output+z2; z2 = b2*input-a2*output;
        return output;
    }
};
float eqFrequency(unsigned value) { return 20*std::exp2(value/6.0f); }
float eqGain(unsigned value) { return std::clamp(int(value)-64,-12,12); }
struct Parameters {
    unsigned model {}, high {}, mid {}, low {};
    std::span<const std::uint8_t> data;
};
std::optional<Parameters> parameters(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 9 || bytes.front() != 0xf0 || bytes.back() != 0xf7 || bytes[1] != 0x43
        || std::any_of(bytes.begin()+1,bytes.end()-1,[](auto b) { return b > 127; })) return {};
    if ((bytes[2] & 0xf0) == 0x10)
        return Parameters {bytes[3],bytes[4],bytes[5],bytes[6],bytes.subspan(7,bytes.size()-8)};
    if (bytes[2] >= 16 || bytes.size() < 12) return {};
    const unsigned count = bytes[4] * 128 + bytes[5];
    unsigned sum = 0;
    for (unsigned i = 4; i + 1 < bytes.size(); ++i) sum += bytes[i];
    if (count != bytes.size()-11 || (sum & 127)) return {};
    return Parameters {bytes[3],bytes[6],bytes[7],bytes[8],bytes.subspan(9,count)};
}
}

struct AnEngine::Impl {
    struct Channel {
        unsigned pendingMsb {}, pendingLsb {}, msb {}, lsb {}, program {};
        unsigned volume {100}, expression {127}, pan {64}, reverb {40}, chorus {}, variation {}, dry {127};
        unsigned modulation {}, pressure {}, rpnMsb {127}, rpnLsb {127}, bendCents {}, fineTune {8192};
        unsigned cutoff {64}, resonance {64}, attack {64}, release {64};
        unsigned decay {64}, vibratoRate {64}, vibratoDepth {64}, vibratoDelay {64};
        unsigned nrpnMsb {127}, nrpnLsb {127};
        unsigned bassGain {64}, trebleGain {64}, bassFrequency {12}, trebleFrequency {54};
        std::array<unsigned,128> controllers {}, held {}, velocity {};
        std::array<bool,128> controllerReceived {};
        std::array<unsigned,128> notePressure {};
        std::array<std::uint64_t,128> order {};
        std::array<int,34> nativeOffsets {};
        std::array<int,12> scaleTune {};
        int bend {8192}, bendRange {-1}, coarseTune {}, transpose {};
        bool sustain {}, portamento {}, pressureReceived {};
        std::uint64_t egStart {};
        std::uint64_t lfoFrame {};
        float lfoPhase {}, lfo2Phase {}, lfoHeld {0.15f}, lfo2Held {-0.25f};
        std::uint32_t lfoRandom {0x13579bdf}, lfo2Random {0x2468ace1};
    };
    struct Voice {
        Preset preset;
        Envelope amp, filter;
        Filter low, low2, distortionLow;
        AnOnePole high, low18;
        std::array<Equalizer,5> eq;
        unsigned channel {}, note {}, velocity {}, noise {0x12345678}, releaseAge {}, age {}, presetIndex {};
        std::uint64_t serial {};
        std::uint64_t egStart {};
        std::uint64_t sequenceGateEnd {};
        float phase1 {}, phase2 {}, phaseSync {}, phaseSub {}, lfoPhase {}, lfo2Phase {}, pitch {}, last {};
        std::uint32_t lfoRandom {0x13579bdf}, lfo2Random {0x2468ace1};
        float lfoHeld {0.15f}, lfo2Held {-0.25f};
        bool active {}, held {}, released {}, sequenceGenerated {};
    };
    struct Sequence {
        bool active {};
        unsigned presetIndex {}, triggerNote {}, velocity {}, step {};
        std::uint64_t start {};
        double nextFrame {};
    };
    struct Event { std::uint64_t frame {}; std::uint32_t packed {}; unsigned offset {}, size {}; };
    struct Insertion { unsigned msb {}, lsb {}, part {127}; };
    std::vector<Preset> presets;
    std::map<unsigned,unsigned> entries;
    std::array<Channel,partCount> channels {}, shadow {};
    std::array<Voice,voiceLimit> voices {};
    std::array<Sequence,partCount> sequences {};
    std::array<Event,eventLimit> events {};
    std::array<std::uint8_t,sysexLimit> bytes {};
    std::array<Insertion,4> insertions {};
    XgVariationRouting variation;
    std::array<unsigned,8> assignableControllers {41,42,43,44,45,46,47,48};
    unsigned count {}, bytesUsed {};
    std::uint64_t timeline {}, serial {};
    float rate {44100};

    explicit Impl(std::span<const std::uint8_t> bank) {
        if (bank.size() > sysexLimit || bank.size() < 12 || std::memcmp(bank.data(),"ANP1",4))
            throw std::runtime_error("Not an ANP1 voice bank");
        auto word = [&](unsigned p) { return unsigned(bank[p]) | (unsigned(bank[p+1])<<8)
            | (unsigned(bank[p+2])<<16) | (unsigned(bank[p+3])<<24); };
        const auto presetCount = word(4), entryCount = word(8);
        if (!presetCount || presetCount > 256 || entryCount > 4096) throw std::runtime_error("Invalid AN bank counts");
        unsigned cursor = 12;
        presets.resize(presetCount);
        for (auto& p : presets) {
            if (cursor + 300 > bank.size()) throw std::runtime_error("Truncated AN preset");
            std::copy_n(bank.data()+cursor,104,p.common.begin()); cursor += 104;
            std::copy_n(bank.data()+cursor,122,p.scene.begin()); cursor += 122;
            std::copy_n(bank.data()+cursor,70,p.sequence.begin()); cursor += 70;
            const auto egSize = word(cursor); cursor += 4;
            if ((egSize != 0 && egSize != 768) || cursor + egSize > bank.size()) throw std::runtime_error("Invalid AN Free EG length");
            p.hasFreeEg = egSize != 0;
            if (egSize) std::copy_n(bank.data()+cursor,egSize,p.freeEg.begin());
            cursor += egSize;
            if (std::any_of(p.common.begin(),p.common.begin()+10,[](auto b) { return b < 32 || b > 126; })
                || std::any_of(p.common.begin()+10,p.common.end(),[](auto b) { return b > 127; })
                || std::any_of(p.scene.begin(),p.scene.end(),[](auto b) { return b > 127; })
                || std::any_of(p.sequence.begin(),p.sequence.end(),[](auto b) { return b > 127; }))
                throw std::runtime_error("Invalid AN preset parameters");
        }
        if (cursor + entryCount * 5 != bank.size()) throw std::runtime_error("Invalid AN selection table size");
        for (unsigned i = 0; i < entryCount; ++i) {
            unsigned msb = bank[cursor], lsb = bank[cursor+1], pc = bank[cursor+2];
            unsigned index = bank[cursor+3] | (bank[cursor+4]<<8); cursor += 5;
            if (!anBank(msb) || lsb > 127 || pc > 127 || index >= presets.size()
                || !entries.emplace(key(msb,lsb,pc),index).second) throw std::runtime_error("Invalid AN bank selection");
        }
    }
    const Preset* preset(const Channel& c) const {
        const auto found = entries.find(key(c.msb,c.lsb,c.program));
        return found == entries.end() ? nullptr : &presets[found->second];
    }
    bool owns(const Channel& c) const { return c.msb == 36 || c.msb == 84 || (c.msb == 100 && preset(c)); }
    static void select(Channel& c, unsigned op, unsigned a, unsigned b) {
        if (op == 0xb0 && a == 0) c.pendingMsb = b;
        if (op == 0xb0 && a == 32) c.pendingLsb = b;
        if (op == 0xc0) { c.msb = c.pendingMsb; c.lsb = c.pendingLsb; c.program = a; }
    }
    static void part(Channel& c, unsigned address, unsigned value) {
        switch (address) {
        case 1: c.pendingMsb = value; break;
        case 2: c.pendingLsb = value; break;
        case 3: select(c,0xc0,value,0); break;
        case 8: c.transpose = int(value)-64; break;
        case 0xb: c.volume = value; break;
        case 0xe: c.pan = value; break;
        case 0x11: c.dry = value; break;
        case 0x12: c.chorus = value; break;
        case 0x13: c.reverb = value; break;
        case 0x14: c.variation = value; break;
        case 0x15: c.vibratoRate = value; break;
        case 0x16: c.vibratoDepth = value; break;
        case 0x17: c.vibratoDelay = value; break;
        case 0x18: c.cutoff = value; break;
        case 0x19: c.resonance = value; break;
        case 0x1a: c.attack = value; break;
        case 0x1b: c.decay = value; break;
        case 0x1c: c.release = value; break;
        case 0x72: c.bassGain = value; break;
        case 0x73: c.trebleGain = value; break;
        case 0x76: if (value >= 4 && value <= 40) c.bassFrequency = value; break;
        case 0x77: if (value >= 28 && value <= 58) c.trebleFrequency = value; break;
        case 0x23: if (value >= 40 && value <= 88) c.bendRange = int(value)-64; break;
        default: break;
        }
        if (address >= 0x41 && address <= 0x4c) c.scaleTune[address-0x41] = int(value)-64;
    }
    void add(Event e) {
        if (count == eventLimit) throw std::runtime_error("AN MIDI queue overflow");
        auto at = count++;
        while (at && events[at-1].frame > e.frame) { events[at] = events[at-1]; --at; }
        events[at] = e;
    }
    void compact() {
        std::array<unsigned,eventLimit> order {};
        unsigned n = 0, used = 0;
        for (unsigned i = 0; i < count; ++i) if (events[i].size) order[n++] = i;
        std::sort(order.begin(),order.begin()+n,[&](unsigned a,unsigned b) { return events[a].offset < events[b].offset; });
        for (unsigned i = 0; i < n; ++i) { auto& e = events[order[i]];
            std::memmove(bytes.data()+used,bytes.data()+e.offset,e.size); e.offset = used; used += e.size; }
        bytesUsed = used;
    }
    void release(Voice& v) {
        if (v.released) return;
        v.released = true;
        const auto& c = channels[v.channel];
        v.amp.release(seconds(std::clamp(int(v.preset.scene[61])+int(c.release)-64+c.nativeOffsets[0x1d],0,127)),rate);
        v.filter.release(seconds(v.preset.scene[48])*anFilterEgTimeScale,rate);
    }
    void stopSequence(unsigned ch) {
        sequences[ch].active = false;
        for (auto& v : voices) if (v.active && v.channel == ch && v.sequenceGenerated) {
            v.held = false; release(v);
        }
    }
    void noteOn(unsigned ch, unsigned note, unsigned velocity, bool generated = false, std::uint64_t gateEnd = 0) {
        auto& c = channels[ch];
        const auto* p = preset(c);
        if (!p) return;
        ++serial;
        if (!generated) {
            c.held[note]++; c.velocity[note] = velocity; c.order[note] = serial;
            // Single stored-pattern playback only. User-pattern selection and
            // alternate-loop modes are not guessed from incomplete data.
            if (p->common[80] && p->common[81] == 1 && p->common[83] <= 1
                && p->sequence[2] <= 1 && p->sequence[1] >= 1 && p->sequence[1] <= 16
                && (!p->common[83] || note < p->common[18])) {
                auto& seq = sequences[ch];
                if (!seq.active || !p->common[84]) {
                    stopSequence(ch);
                    seq = {true,unsigned(p-presets.data()),note,velocity,0,timeline,double(timeline)};
                } else { seq.triggerNote=note; seq.velocity=velocity; }
                if (p->common[83] == 1) return;
            }
        }
        const bool mono = p->scene[0] != 0;
        const auto presetIndex = unsigned(p-presets.data());
        if (p->hasFreeEg && p->common[92] && (!generated || p->common[92] == 2))
            for (auto& voice : voices)
                if (voice.active && voice.channel == ch && voice.presetIndex == presetIndex)
                    voice.egStart = timeline;
        if (mono) for (auto& v : voices) if (v.active && v.channel == ch && !v.released
            && v.presetIndex == presetIndex && v.sequenceGenerated == generated) {
            v.note = note; v.velocity = velocity; v.held = true;
            v.sequenceGateEnd = gateEnd;
            if (p->scene[0] == 1) { v.amp = {}; v.filter = {}; v.age = 0; }
            return;
        }
        auto slot = voices.end();
        unsigned partVoices = 0;
        for (auto v = voices.begin(); v != voices.end(); ++v)
            if (v->active && v->channel == ch) {
                ++partVoices;
                if (slot == voices.end() || (v->released != slot->released ? v->released : v->serial < slot->serial)) slot = v;
            }
        if (partVoices < notesPerPart) slot = std::find_if(voices.begin(),voices.end(),[](const Voice& v) { return !v.active; });
        if (slot == voices.end()) return;
        *slot = Voice {}; slot->preset = *p; slot->channel = ch; slot->note = note;
        slot->velocity = velocity; slot->serial = serial; slot->active = slot->held = true;
        slot->presetIndex = presetIndex;
        slot->sequenceGenerated = generated; slot->sequenceGateEnd = gateEnd;
        slot->egStart = generated && p->common[92] == 1 ? sequences[ch].start : p->common[92] ? timeline : c.egStart;
        slot->pitch = float(note); slot->noise ^= unsigned(serial * 747796405u);
    }
    bool advanceSequences() {
        bool generated = false;
        for (unsigned ch=0; ch<partCount; ++ch) {
            auto& seq = sequences[ch];
            if (!seq.active) continue;
            const auto* p = preset(channels[ch]);
            if (!p || unsigned(p-presets.data()) != seq.presetIndex) { stopSequence(ch); continue; }
            if (timeline < std::uint64_t(std::ceil(seq.nextFrame))) continue;
            const auto& common = p->common; const auto& data = p->sequence;
            const unsigned length = std::clamp(unsigned(data[1]),1u,16u);
            const unsigned step = data[2] == 1 ? length-1-seq.step%length : seq.step%length;
            const auto tempo = common[16]*128+common[17];
            const double beats = sequenceBeats[std::min(unsigned(data[0]),9u)];
            const double period = rate*60*beats/(tempo == 39 ? 120 : std::clamp(tempo,40,240));
            const double swing = std::clamp(unsigned(common[87]),50u,83u)/50.0;
            const double duration = period*(seq.step%2 ? 2-swing : swing);
            seq.nextFrame += duration;
            ++seq.step;
            if (data[3] && data[3] <= 96)
                shortMessage((data[3] == 96 ? 0xd0 : 0xb0) | ch
                    | (data[3] == 96 ? unsigned(data[54+step])<<8
                        : (unsigned(data[3])<<8) | (unsigned(data[54+step])<<16)));
            const unsigned velocityScale = common[88]*128+common[89];
            const unsigned velocity = !data[22+step] ? 0 : velocityScale
                ? std::min(127u,unsigned(data[22+step])*velocityScale/100) : seq.velocity;
            if (!velocity) continue;
            const int shift = common[83] == 1 ? int(seq.triggerNote)-int(sequenceBaseNote) : 0;
            const auto note = unsigned(std::clamp(int(data[6+step])+shift,0,127));
            const auto gate = data[38+step];
            const double gatePercent = gate <= 64 ? std::max(1.0,gate*100.0/64) : 100+(gate-64)*100.0/63;
            const auto gateScale = std::clamp(common[90]*128+common[91],1,200);
            const auto gateEnd = timeline+std::uint64_t(std::max(1.0,std::round(duration*gatePercent*gateScale/10000)));
            noteOn(ch,note,velocity,true,gateEnd);
            generated = true;
        }
        return generated;
    }
    void shortMessage(std::uint32_t packed) {
        unsigned ch = packed & 15, op = packed & 0xf0, a = (packed>>8)&127, b = (packed>>16)&127;
        auto& c = channels[ch]; select(c,op,a,b);
        if (op == 0xc0) c.egStart = timeline;
        if (op == 0xe0) c.bend = a | (b<<7);
        if (op == 0xd0) { c.pressure = a; c.pressureReceived = true; }
        if (op == 0xa0) c.notePressure[a] = b;
        if (op == 0xb0) {
            c.controllers[a] = b;
            c.controllerReceived[a] = true;
            switch (a) {
            case 1: c.modulation = b; break;
            case 7: c.volume = b; break;
            case 10: c.pan = b; break;
            case 11: c.expression = b; break;
            case 91: c.reverb = b; break;
            case 93: c.chorus = b; break;
            case 94: c.variation = b; break;
            case 71: c.resonance = b; break;
            case 72: c.release = b; break;
            case 73: c.attack = b; break;
            case 74: c.cutoff = b; break;
            case 75: c.decay = b; break;
            case 65: c.portamento = b >= 64; break;
            case 101: c.rpnMsb = b; c.nrpnMsb = c.nrpnLsb = 127; break;
            case 100: c.rpnLsb = b; c.nrpnMsb = c.nrpnLsb = 127; break;
            case 99: c.nrpnMsb = b; c.rpnMsb = c.rpnLsb = 127; break;
            case 98: c.nrpnLsb = b; c.rpnMsb = c.rpnLsb = 127; break;
            case 6:
                if (!c.rpnMsb && !c.rpnLsb) c.bendRange = int(b);
                if (!c.rpnMsb && c.rpnLsb == 1) c.fineTune = (b<<7) | (c.fineTune&127);
                if (!c.rpnMsb && c.rpnLsb == 2) c.coarseTune = int(b)-64;
                if (c.nrpnMsb == 1) switch (c.nrpnLsb) {
                case 8: c.vibratoRate = b; break;
                case 9: c.vibratoDepth = b; break;
                case 10: c.vibratoDelay = b; break;
                case 0x20: c.cutoff = b; break;
                case 0x30: c.bassGain = b; break;
                case 0x31: c.trebleGain = b; break;
                case 0x34: if (b >= 4 && b <= 40) c.bassFrequency = b; break;
                case 0x35: if (b >= 28 && b <= 58) c.trebleFrequency = b; break;
                case 0x63: c.attack = b; break;
                case 0x64: c.decay = b; break;
                case 0x66: c.release = b; break;
                default: break;
                }
                break;
            case 38:
                if (!c.rpnMsb && !c.rpnLsb) c.bendCents = b;
                if (!c.rpnMsb && c.rpnLsb == 1) c.fineTune = (c.fineTune&0x3f80) | b;
                break;
            case 64:
                c.sustain = b >= 64;
                if (!c.sustain) for (auto& v : voices) if (v.active && v.channel == ch && !v.held) release(v);
                break;
            case 120: stopSequence(ch); for (auto& v : voices) if (v.channel == ch) v.active = false; c.held = {}; break;
            case 123:
                stopSequence(ch);
                c.held = {};
                for (auto& v : voices) if (v.active && v.channel == ch) { v.held = false; if (!c.sustain) release(v); }
                break;
            case 121:
                c.expression = 127; c.modulation = c.pressure = 0; c.bend = 8192;
                c.sustain = c.portamento = false;
                c.controllers = {}; c.notePressure = {}; c.rpnMsb = c.rpnLsb = c.nrpnMsb = c.nrpnLsb = 127;
                c.controllerReceived = {}; c.pressureReceived = false;
                for (auto& v : voices) if (v.active && v.channel == ch && !v.held) release(v);
                break;
            default: break;
            }
        }
        if (op == 0x80 || (op == 0x90 && !b)) {
            if (c.held[a]) --c.held[a];
            auto& seq = sequences[ch];
            if (seq.active && !c.held[seq.triggerNote] && !presets[seq.presetIndex].common[84]) {
                unsigned next = 0; std::uint64_t order = 0;
                const unsigned limit = presets[seq.presetIndex].common[83] == 1 ? presets[seq.presetIndex].common[18] : 128;
                for (unsigned n=0;n<limit;++n) if (c.held[n] && c.order[n]>order) { order=c.order[n]; next=n; }
                if (order) { seq.triggerNote=next; seq.velocity=c.velocity[next]; }
                else stopSequence(ch);
            }
            auto oldest = std::uint64_t(-1);
            for (const auto& v : voices) if (v.active && !v.sequenceGenerated && v.channel == ch && v.note == a && v.held) oldest = std::min(oldest,v.serial);
            for (auto& v : voices) if (v.active && v.serial == oldest) {
                if (v.preset.scene[0] && std::any_of(c.held.begin(),c.held.end(),[](auto n) { return n != 0; })) {
                    unsigned next = 0; std::uint64_t order = 0;
                    for (unsigned n = 0; n < 128; ++n) if (c.held[n] && c.order[n] >= order) { order = c.order[n]; next = n; }
                    v.note = next; v.velocity = c.velocity[next];
                } else { v.held = false; if (!c.sustain) release(v); }
                break;
            }
        }
        if (op == 0x90 && b && owns(c)) noteOn(ch,a,b);
    }
    void observe(const Parameters& p) {
        if (p.model == 0x4c && p.high == 8 && p.mid < partCount)
            for (unsigned i = 0; i < p.data.size() && p.low+i < 128; ++i) part(channels[p.mid],p.low+i,p.data[i]);
        if (p.model == 0x5c && p.high == 9 && p.mid < partCount)
            for (unsigned i = 0; i < p.data.size() && p.low+i < 34; ++i)
                channels[p.mid].nativeOffsets[p.low+i] = int(p.data[i])-64;
        if (p.model == 0x5c && p.high == 0 && p.mid == 0)
            for (unsigned i = 0; i < p.data.size(); ++i)
                if (p.low+i >= 0x13 && p.low+i <= 0x1a && p.data[i] <= 96)
                    assignableControllers[p.low+i-0x13] = p.data[i];
        if (p.model == 0x5c && p.high == 0 && p.mid == 8)
            for (unsigned i = 0; i < p.data.size(); ++i)
                if (p.low+i >= 8 && p.low+i <= 11 && p.data[i] <= 96)
                    assignableControllers[p.low+i-8] = p.data[i];
        if (p.model == 0x4c && p.high == 3 && p.mid < 4) {
            auto& ins = insertions[p.mid];
            for (unsigned i = 0; i < p.data.size(); ++i) switch (p.low+i) {
            case 0: ins.msb = p.data[i]; break;
            case 1: ins.lsb = p.data[i]; break;
            case 12: ins.part = p.data[i]; break;
            default: break;
            }
        }
        if (p.model == 0x4c && p.high == 2 && p.mid == 1)
            for (unsigned i = 0; i < p.data.size() && p.low+i < 128; ++i) {
                const std::array<std::uint8_t,9> msg {0xf0,0x43,0x10,0x4c,2,1,std::uint8_t(p.low+i),p.data[i],0xf7};
                variation.observe(msg);
            }
    }
    void sysex(std::span<const std::uint8_t> data) {
        if (classifySystemReset(data) != MidiSystemReset::none) {
            channels = {}; voices = {}; sequences = {}; insertions = {}; variation.reset();
            assignableControllers = {41,42,43,44,45,46,47,48}; return;
        }
        if (const auto p = parameters(data)) observe(*p);
    }
    std::optional<unsigned> insertion(unsigned ch) const {
        for (unsigned i = 0; i < 4; ++i) if ((insertions[i].msb || insertions[i].lsb)
            && insertions[i].part == ch) return i;
        return {};
    }
    float sample(Voice& v, Channel& c) {
        auto modulated = v.preset.hasFreeEg
            ? anEgScene(v.preset.scene,v.preset.common,v.preset.freeEg,double(timeline-v.egStart)/rate)
            : v.preset.scene;
        // Preserve factory values until an actual direct-control message
        // arrives. Untouched knobs must not behave as implicit CC value zero.
        for (unsigned slot = 68; slot + 2 < 113; slot += 3) {
            const auto source = v.preset.scene[slot], target = v.preset.scene[slot+1];
            if (!source || v.preset.scene[slot+2] != 64) continue;
            const unsigned cc = source >= 107 && source <= 114 ? assignableControllers[source-107] : source;
            if (cc == matrixAfterTouch && c.pressureReceived)
                anDirectControl(modulated,target,c.pressure);
            else if (cc < 96 && c.controllerReceived[cc])
                anDirectControl(modulated,target,c.controllers[cc]);
        }
        const auto& s = modulated;
        unsigned wave1 = s[23];
        if (s[15] && wave1 == 5) wave1 = 1;
        if ((!s[15] && wave1 == 4) || (s[15] && wave1 == 2)) wave1 = 0;
        // Inner1 remains an explicit saw approximation, not the distinct
        // unsynchronised saw2. Yamaha does not publish the inner-wave shapes.
        std::array<float,47> matrix {};
        for (unsigned slot = 68; slot + 2 < 113; slot += 3) {
            const auto source = s[slot], target = s[slot+1];
            if (!source || target >= matrix.size()) continue;
            const float value = source <= 95 ? c.controllers[source]/127.0f
                : source == matrixAfterTouch ? c.pressure/127.0f
                : source == matrixPitchBend ? (c.bend-8192)/8192.0f
                : source == matrixKeyTrack ? (int(v.note)-60)/64.0f
                : source == matrixVelocity ? v.velocity/127.0f
                : source >= 107 && source <= 114
                    ? (assignableControllers[source-107] == 96 ? c.pressure
                        : assignableControllers[source-107] ? c.controllers[assignableControllers[source-107]] : 0)/127.0f
                    : 0.0f;
            matrix[target] += (int(s[slot+2])-64)*value;
        }
        const auto offset = [&](unsigned index, int delta = 0) { return unsigned(std::clamp(int(s[index])+delta,0,127)); };
        const float amp = v.amp.next(offset(58,int(c.attack)-64+c.nativeOffsets[0x1a]-int(matrix[38])),offset(59,int(c.decay)-64+c.nativeOffsets[0x1b]-int(matrix[39])),offset(60,c.nativeOffsets[0x1c]+int(matrix[40])),rate);
        const float feg = v.filter.next(offset(45,c.nativeOffsets[0x15]-int(matrix[29])),offset(46,c.nativeOffsets[0x16]-int(matrix[30])),offset(47,c.nativeOffsets[0x17]+int(matrix[31])),rate,anFilterEgTimeScale);
        if (v.released && (++v.releaseAge > unsigned(rate * 30) || amp <= 0)) { v.active = false; return 0; }
        const float lfoSpeed = 0.05f * std::exp2((s[10]*128+s[11]+matrix[7]+int(c.vibratoRate)-64) / 32.0f);
        const float lfo2Speed = 0.05f * std::exp2((s[13]*128+s[14]+matrix[9]+c.nativeOffsets[4]) / 32.0f);
        float lfo, lfo2;
        if (s[8]) {
            lfo = advanceLfo(v.lfoPhase,v.lfoRandom,v.lfoHeld,s[9],std::min(100.0f,lfoSpeed)/rate);
            lfo2 = advanceLfo(v.lfo2Phase,v.lfo2Random,v.lfo2Held,s[119],std::min(100.0f,lfo2Speed)/rate);
        } else {
            // Free-running LFOs belong to the part, not each new note. Update
            // once per frame, including elapsed silent time on the next note.
            if (c.lfoFrame <= timeline) {
                const auto elapsed = float(timeline+1-c.lfoFrame);
                advanceLfo(c.lfoPhase,c.lfoRandom,c.lfoHeld,s[9],std::min(100.0f,lfoSpeed)*elapsed/rate);
                advanceLfo(c.lfo2Phase,c.lfo2Random,c.lfo2Held,s[119],std::min(100.0f,lfo2Speed)*elapsed/rate);
                c.lfoFrame = timeline+1;
            }
            v.lfoPhase=c.lfoPhase; v.lfo2Phase=c.lfo2Phase;
            v.lfoHeld=c.lfoHeld; v.lfo2Held=c.lfo2Held;
            lfo = lfoWave(s[9],v.lfoPhase,v.lfoHeld);
            lfo2 = lfoWave(s[119],v.lfo2Phase,v.lfo2Held);
        }
        const float pitchLfo1 = s[120]&8 ? lfo2 : lfo, pitchLfo2 = s[120]&4 ? lfo2 : lfo;
        const int range = c.bendRange >= 0 ? c.bendRange : (c.bend >= 8192 ? int(s[1])-64 : 64-int(s[2]));
        const float bend = (c.bend-8192) / 8192.0f * (range+c.bendCents/100.0f);
        if (c.portamento || v.preset.common[19]) v.pitch += (v.note-v.pitch) * std::min(1.0f,1/(seconds(s[7])*rate)); else v.pitch = float(v.note);
        const float peg = (int(s[4])-64)*std::exp(-float(v.age++)/(seconds(s[3])*rate));
        const float commonPitch = v.pitch + c.transpose + c.coarseTune + matrix[1] + (int(s[117])-64)*12
            + (int(c.fineTune)-8192)/8192.0f + c.scaleTune[v.note%12]/100.0f + bend;
        const float pitchMod1 = pitchLfo1*((int(s[30])*128+s[31]-128)/32.0f)*(1+matrix[18]/64.0f);
        const float pitch1 = commonPitch+int(s[24])-64+matrix[13]+(int(s[25])-64+matrix[14])/100.0f
            +(s[5]&1 ? peg : 0);
        const float pitch2 = commonPitch+int(s[33])-64+matrix[19]+(int(s[34])-64+c.nativeOffsets[7]+matrix[20])/100.0f
            +(s[5]&2 ? peg : 0)+pitchLfo2*(int(s[39])*128+s[40]-128)/32.0f*(1+matrix[24]/64.0f);
        const auto pitchStep = [&](float pitch) { return std::clamp(440*std::exp2((pitch-69)/12)/rate,0.000001f,0.45f); };
        float step1 = pitchStep(pitch1+(s[15] == 1 && s[19] == 1 ? 0 : pitchMod1));
        const float step2 = std::clamp(440*std::exp2((pitch2-69)/12)/rate,0.000001f,0.45f);
        v.noise ^= v.noise<<13; v.noise ^= v.noise>>17; v.noise ^= v.noise<<5;
        const float noise = int(v.noise) / 2147483648.0f;
        const float edge1 = std::clamp(s[26]+matrix[15],0.0f,127.0f);
        const float edge2 = s[32] >= 4 ? 127 : std::clamp(s[35]+matrix[21],0.0f,127.0f);
        const auto modulationSource = [&](unsigned source) {
            switch (source) {
            case 0: return 1.0f;
            case 1: return peg/64;
            case 2: return feg;
            case 3: return lfo;
            case 4: return lfo2;
            default: return 0.0f;
            }
        };
        const auto pwmSource = [&](unsigned source, unsigned otherWave, float otherPhase, float otherStep, float edge) {
            if (source <= 4) return modulationSource(source);
            if (source == 5) return lfoWave(s[119],std::fmod(v.lfo2Phase+1.0f/6,1.0f),v.lfo2Held);
            if (source == 7) return oscillatorWithEdge(otherWave,otherPhase,otherStep,0.5f,noise,edge);
            return lfo2; // Fast LFO's hardware rate multiplier remains unverified.
        };
        const float pw1 = std::clamp((s[27]+c.nativeOffsets[9]+matrix[16])/128.0f
            + (int(s[28])-64+c.nativeOffsets[10])*(1+matrix[17]/64.0f)*pwmSource(s[29],s[32],v.phase2,step2,edge2)/256.0f,0.02f,0.98f);
        const float pw2 = std::clamp((s[36]+c.nativeOffsets[12]+matrix[22])/128.0f
            + (int(s[37])-64+c.nativeOffsets[13])*(1+matrix[23]/64.0f)*pwmSource(s[38],wave1,v.phase1,step1,edge1)/256.0f,0.02f,0.98f);
        float phase2 = v.phase2;
        if (s[32] >= 4) {
            // PLG150-AN reuses PWM depth/source for VCO1-to-VCO2 X-MOD
            // on triangle and sine voices. Phase modulation preserves the
            // carrier tuning; its hardware depth law remains approximate.
            const float depth = (int(s[37])-64+c.nativeOffsets[13])/64.0f
                *(1+matrix[23]/64.0f)*modulationSource(s[38]);
            phase2 += depth*oscillatorWithEdge(wave1,v.phase1,step1,pw1,noise,edge1)*0.5f;
            phase2 -= std::floor(phase2);
        }
        const float osc2 = oscillatorWithEdge(s[32],phase2,step2,pw2,noise,edge2);
        const float fm = (int(s[20])-64+c.nativeOffsets[6])/64.0f*(1+matrix[12]/64.0f)*modulationSource(s[21]);
        // The two FM selectors are independent: an envelope/LFO controls
        // depth, while the second selector supplies the modulation waveform.
        // Frequency-derived VCO2 currently uses a sinusoidal carrier; the
        // original chip's FM transfer curve is still not reproduced exactly.
        const float fmWave = s[22] == 0 ? std::sin(2*pi*v.phase2)
            : s[22] == 1 ? oscillatorWithEdge(wave1,v.phase1,step1,pw1,noise,edge1)
            : s[22] == 2 ? oscillatorWithEdge(wave1,v.phaseSub,step1*0.5f,pw1,noise,edge1)
            : s[22] == 3 ? peg/64 : s[22] == 4 ? feg
            : s[22] == 5 ? lfo : s[22] == 6 ? lfo2 : osc2;
        // Sync Pmod (scene19) is independent of the FM algorithm (scene121).
        float masterStep = pitchStep(pitch1+(s[15] == 1 && s[19] == 2 ? 0 : pitchMod1));
        if (s[15] == 1) {
            const float source = modulationSource(s[18]);
            const float syncPitch = int(s[16])-64+c.nativeOffsets[5]+matrix[10]
                +(int(s[17])-64)*(1+matrix[11]/64.0f)*source;
            // Sync Pitch tunes the slave. The master's cycle still sets the
            // reset period, otherwise a timbre sweep incorrectly retunes notes.
            step1 = std::clamp(step1*std::exp2(syncPitch/12),0.000001f,0.45f);
            if (s[121] != 3) masterStep = std::clamp(masterStep*(1+fm*fmWave),0.000001f,0.45f);
        }
        if (s[15] != 1 || s[121] != 2)
            step1 = std::clamp(step1*(1+fm*fmWave),0.000001f,0.45f);
        float osc1 = oscillatorWithEdge(wave1,v.phase1,step1,pw1,noise,edge1);
        if (!s[15] && s[23] == 4) osc1 = (osc1+oscillatorWithEdge(0,v.phaseSync,step1*1.003f,pw1,noise,edge1))*0.5f;
        const auto mixGain = [&](unsigned index, unsigned native, unsigned target) {
            return offset(index,c.nativeOffsets[native])/127.0f*std::max(0.0f,1+matrix[target]/64.0f);
        };
        const float signal = osc1*mixGain(41,14,25) + osc2*mixGain(42,15,26)
            + osc1*osc2*mixGain(43,16,27) + noise*mixGain(44,17,28);
        v.phase1 += step1; v.phase2 += step2;
        v.phaseSub += step1*0.5f; v.phaseSub -= std::floor(v.phaseSub);
        const bool wrap2 = v.phase2 >= 1;
        v.phase1 -= std::floor(v.phase1); v.phase2 -= std::floor(v.phase2);
        v.phaseSync += s[15] ? masterStep : step1*1.003f; const bool wrapSync = v.phaseSync >= 1;
        v.phaseSync -= std::floor(v.phaseSync);
        // A master wraps between samples. Preserve the slave's fraction of
        // that sample instead of quantising every reset to the sample boundary.
        if (s[15] == 2 && wrap2) v.phase1 = v.phase2/step2*step1;
        else if (s[15] == 1 && wrapSync) v.phase1 = v.phaseSync/masterStep*step1;
        const float egDepth = (int(s[53])*128+s[54]-128+c.nativeOffsets[20])*(1+matrix[36]/64.0f);
        const float filterVelocity = anFilterVelocity(int(s[55])-64,v.velocity);
        const float filterLfo = s[120]&1 ? lfo2 : lfo;
        const float cutoff = std::clamp(20*std::exp2((s[51]+int(c.cutoff)-64+matrix[34]+egDepth*feg*filterVelocity*anFilterEgSemitonesPerUnit
            +(int(s[56])-64)*(int(v.note)-60)/64.0f
            +(int(s[57])-64+c.nativeOffsets[19]+matrix[37])*filterLfo)/12.0f),20.0f,rate*0.4f);
        const float g = std::tan(pi*cutoff/rate);
        const float damping = anFilterDamping(int(s[52])-25+int(c.resonance)-64+matrix[35]);
        const float feedback = std::clamp(offset(62,c.nativeOffsets[18])*(1+matrix[42]/64.0f),0.0f,127.0f);
        float result = std::tanh(signal + v.last*feedback/254.0f);
        // PLG150-AN signal flow (pp32/47): separate 6dB HPF before VCF,
        // then VCA; only the pre-effect VCA output feeds back into the mixer.
        const auto highPass = offset(49,int(matrix[33]));
        if (highPass) result = v.high.highPass(result,std::tan(pi*std::min(rate*0.3f,20*std::exp2(highPass/14.0f))/rate));
        result = v.low.next(result,g,damping,s[50]);
        if (s[50] == 0) result = v.low2.next(result,g,1.414f,2);
        else if (s[50] == 1) result = v.low18.lowPass(result,g);
        const float sensitivity = (int(s[64])-64)/64.0f;
        const float velocity = std::clamp(1+sensitivity*(v.velocity/127.0f-1),0.0f,2.0f);
        const float amplitudeLfo = s[120]&2 ? lfo2 : lfo;
        const float amplitudeDepth = std::clamp((int(s[65])-64+c.nativeOffsets[0x19]+matrix[44])/64.0f,-1.0f,1.0f);
        const float tremolo = 1-std::abs(amplitudeDepth)*(1-(amplitudeDepth>=0 ? amplitudeLfo : -amplitudeLfo))/2;
        result *= amp * velocity * tremolo * (s[63]/127.0f) * std::max(0.0f,1+matrix[43]/64.0f);
        v.last = result;
        const auto& common = v.preset.common;
        const float wet = (std::clamp(int(s[66])+int(matrix[45]),1,127)-1)/126.0f;
        // Amp Type Off cancels cabinet colouring, not drive or the wet-path LPF.
        if (wet > 0) {
            const float drive = 1+(common[28]*128+common[29])/8.0f;
            float distorted = std::tanh(result*drive);
            const unsigned cutoff = common[32]*128+common[33];
            if (cutoff >= distortionLpfFirst && cutoff < distortionLpfThru)
                distorted = v.distortionLow.next(distorted,std::tan(pi*
                    std::min(rate*0.3f,distortionLpfHz[cutoff-distortionLpfFirst])/rate),1.414f,2);
            distorted *= (common[34]*128+common[35])/100.0f;
            result = result*(1-wet)+distorted*wet;
        }
        // Voice EQ precedes the XG part EQ and the external MU effects buses.
        for (unsigned band = 0; band < 3; ++band) {
            const unsigned gainIndex = band == 0 ? 41 : band == 1 ? 43 : 46;
            if (common[gainIndex] < 52 || common[gainIndex] > 76) continue;
            const unsigned frequencyIndex = band == 0 ? 40 : band == 1 ? 42 : 45;
            result = v.eq[band].next(result,band,eqFrequency(common[frequencyIndex]),eqGain(common[gainIndex]),
                band == 1 ? std::max(1.0f,common[44]/10.0f) : 1,rate);
        }
        result = v.eq[3].next(result,0,eqFrequency(c.bassFrequency),eqGain(c.bassGain),1,rate);
        result = v.eq[4].next(result,2,eqFrequency(c.trebleFrequency),eqGain(c.trebleGain),1,rate);
        return result * outputScale * std::exp2(std::min(unsigned(common[27]),2u));
    }
};

AnEngine::AnEngine(const std::filesystem::path& path) : AnEngine(readBank(path)) {}
AnEngine::AnEngine(std::span<const std::uint8_t> bank) : impl(std::make_unique<Impl>(bank)) {}
AnEngine::~AnEngine() = default;
void AnEngine::setSampleRate(float rate) {
    if (!std::isfinite(rate) || rate < 8000 || rate > 192000) throw std::runtime_error("Invalid AN sample rate");
    impl->rate = rate;
}
bool AnEngine::queueShort(std::uint32_t packed, std::uint64_t frame) {
    auto& c = impl->shadow[packed&15]; const auto op = packed&0xf0;
    Impl::select(c,op,(packed>>8)&127,(packed>>16)&127);
    impl->add({frame,packed,0,0});
    const unsigned note = (packed>>8)&127;
    if (impl->owns(c) && op == 0x90 && ((packed>>16)&127)) ++c.held[note];
    if ((op == 0x80 || (op == 0x90 && !((packed>>16)&127))) && c.held[note]) --c.held[note];
    if (op == 0xb0 && (note == 120 || note == 123)) c.held = {};
    return impl->owns(c) && op == 0x90 && ((packed>>16)&127);
}
void AnEngine::queueSysex(std::span<const std::uint8_t> bytes, std::uint64_t frame) {
    if (bytes.empty()) return;
    if (bytes.size() > sysexLimit-impl->bytesUsed) impl->compact();
    if (bytes.size() > sysexLimit-impl->bytesUsed) throw std::runtime_error("AN SysEx queue overflow");
    impl->add({frame,0,impl->bytesUsed,unsigned(bytes.size())});
    std::copy(bytes.begin(),bytes.end(),impl->bytes.begin()+impl->bytesUsed); impl->bytesUsed += unsigned(bytes.size());
    if (classifySystemReset(bytes) != MidiSystemReset::none) impl->shadow = {};
    if (const auto p = parameters(bytes); p && p->model == 0x4c && p->high == 8 && p->mid < partCount)
        for (unsigned i = 0; i < p->data.size() && p->low+i < 128; ++i) Impl::part(impl->shadow[p->mid],p->low+i,p->data[i]);
}
void AnEngine::render(float* output, unsigned stride, unsigned frames) {
    if (!output || frames > stride) throw std::runtime_error("Invalid AN output buffer");
    for (unsigned bus = 0; bus < busCount; ++bus) std::fill_n(output+bus*stride,frames,0.0f);
    unsigned consumed = 0;
    bool haveVoices = activeVoices() != 0;
    bool haveSequences = std::any_of(impl->sequences.begin(),impl->sequences.end(),[](const auto& s) { return s.active; });
    for (unsigned frame = 0; frame < frames;) {
        bool processed = false;
        while (consumed < impl->count && impl->events[consumed].frame <= impl->timeline) {
            const auto& e = impl->events[consumed++];
            if (e.size) impl->sysex({impl->bytes.data()+e.offset,e.size}); else impl->shortMessage(e.packed);
            processed = true;
        }
        if (processed) {
            haveVoices = activeVoices() != 0;
            haveSequences = std::any_of(impl->sequences.begin(),impl->sequences.end(),[](const auto& s) { return s.active; });
        }
        if (haveSequences) haveVoices = impl->advanceSequences() || haveVoices;
        if (!haveVoices) {
            // Advance exactly to the next event instead of scanning 80 idle
            // voice slots for every sample in ordinary MU/DX/VL/SG songs.
            auto distance = consumed < impl->count ? impl->events[consumed].frame-impl->timeline : frames-frame;
            for (const auto& seq : impl->sequences) if (seq.active)
                distance = std::min(distance,std::uint64_t(std::ceil(seq.nextFrame))-impl->timeline);
            const auto skip = unsigned(std::min<std::uint64_t>(distance,frames-frame));
            impl->timeline += skip; frame += skip; continue;
        }
        haveVoices = false;
        for (auto& v : impl->voices) if (v.active) {
            if (v.sequenceGenerated && !v.released && impl->timeline >= v.sequenceGateEnd) {
                v.held = false; impl->release(v);
            }
            auto& c = impl->channels[v.channel];
            const float value = impl->sample(v,c)*(c.volume/127.0f)*(c.expression/127.0f);
            const float left = value*std::sqrt(1-c.pan/127.0f), right = value*std::sqrt(c.pan/127.0f);
            auto add = [&](unsigned bus,float gain) { output[bus*stride+frame] += left*gain; output[(bus+1)*stride+frame] += right*gain; };
            if (const auto insertion = impl->insertion(v.channel)) add(8+*insertion*2,1);
            // The variation processor has its own MU input; do not borrow
            // insertion 1, which may already process another part.
            else if (impl->variation.isInsertionPart(v.channel)) add(6,1);
            else { add(0,c.dry/127.0f); if (impl->variation.connection() == XgVariationConnection::system) add(6,c.variation/127.0f); }
            add(2,c.reverb/127.0f); add(4,c.chorus/127.0f);
            haveVoices = haveVoices || v.active;
        }
        ++impl->timeline;
        ++frame;
    }
    if (consumed) { std::move(impl->events.begin()+consumed,impl->events.begin()+impl->count,impl->events.begin()); impl->count -= consumed; if (!impl->count) impl->bytesUsed = 0; }
}
unsigned AnEngine::selections() const noexcept { return unsigned(impl->entries.size()); }
unsigned AnEngine::activeVoices() const noexcept { return unsigned(std::count_if(impl->voices.begin(),impl->voices.end(),[](const auto& v) { return v.active; })); }
bool AnEngine::selected(unsigned channel) const noexcept { return channel < partCount && impl->owns(impl->shadow[channel]); }
bool AnEngine::heldNote(unsigned channel, unsigned note) const noexcept { return channel < partCount && note < 128 && impl->shadow[channel].held[note] != 0; }
} // namespace hybrid
