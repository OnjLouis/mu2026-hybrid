#include "DxEngine.h"
#include "MidiSystemReset.h"
#include "MuInsertionRouting.h"
#include "VlPluginVoiceBulk.h"
#include "XgVariationRouting.h"
#include "synth.h"
#include "controllers.h"
#include "dx7note.h"
#include "exp2.h"
#include "freqlut.h"
#include "lfo.h"
#include "pitchenv.h"
#include "patch.h"
#include "sin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

namespace hybrid {
namespace {
constexpr unsigned nativeRate = 44100;
constexpr unsigned fmBlock = 64;
constexpr unsigned voiceLimit = 128;
constexpr unsigned eventLimit = 8192;
constexpr unsigned sysexByteLimit = 2 * 1024 * 1024;
constexpr unsigned ccSostenuto = 66;
// Leave mix headroom for layered DX chords before MU's input compensation.
constexpr float outputScale = 0.018f / 16777216.0f;

struct Element {
    std::array<char, 156> patch {};
    unsigned level {};
    int shift {}, detune {};
};
struct Preset { std::array<Element, 4> elements; unsigned count {}; };
unsigned key(unsigned msb, unsigned lsb, unsigned pc) {
    return (msb << 14) | (lsb << 7) | pc;
}
bool dxBank(unsigned msb) { return msb == 35 || msb == 83 || msb == 99 || msb == 67; }
struct ParameterData {
    unsigned model {}, high {}, mid {}, low {};
    std::span<const std::uint8_t> values;
};
std::optional<ParameterData> parameters(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 9 || bytes.front() != 0xf0 || bytes.back() != 0xf7
        || bytes[1] != 0x43 || (bytes[3] != 0x4c && bytes[3] != 0x62)) return {};
    if (std::any_of(bytes.begin() + 1, bytes.end() - 1,
        [](auto value) { return value > 127; })) return {};
    if ((bytes[2] & 0xf0) == 0x10)
        return ParameterData {bytes[3],bytes[4],bytes[5],bytes[6],bytes.subspan(7,bytes.size()-8)};
    if (bytes[2] >= 16 || bytes.size() < 12) return {};
    const unsigned count = bytes[4] * 128 + bytes[5];
    if (count != bytes.size() - 11) return {};
    unsigned checksum = 0;
    for (unsigned i = 4; i + 1 < bytes.size(); ++i) checksum += bytes[i];
    if (checksum & 127) return {};
    return ParameterData {bytes[3],bytes[6],bytes[7],bytes[8],bytes.subspan(9,count)};
}
std::optional<VlPluginVoice> pluginVoice(VlPluginVoiceBulk& parser, std::span<const std::uint8_t> bytes) {
    if (!VlPluginVoiceBulk::isModel64Bulk(bytes)) return {};
    unsigned sum = 0;
    for (unsigned i = 4; i + 1 < bytes.size(); ++i) sum += bytes[i];
    if (bytes.size() != static_cast<unsigned>(bytes[4]) * 128 + bytes[5] + 11
        || (sum & 127) || std::any_of(bytes.begin() + 1, bytes.end() - 1,
            [](auto value) { return value > 127; })) { parser = {}; return {}; }
    const auto voice = parser.observe(bytes);
    return voice && dxBank(voice->bankMsb) ? voice : std::nullopt;
}
void validate(const Element& element) {
    constexpr std::array<unsigned, 21> limits {
        99,99,99,99,99,99,99,99,99,99,99,3,3,7,3,7,99,1,31,99,14
    };
    constexpr std::array<unsigned, 19> globals {
        99,99,99,99,99,99,99,99,31,7,1,99,99,99,99,1,5,7,48
    };
    for (unsigned op = 0; op < 6; ++op)
        for (unsigned i = 0; i < limits.size(); ++i)
            if (static_cast<unsigned char>(element.patch[op * 21 + i]) > limits[i])
                throw std::runtime_error("DX bank has an invalid operator parameter");
    for (unsigned i = 0; i < globals.size(); ++i)
        if (static_cast<unsigned char>(element.patch[126 + i]) > globals[i])
            throw std::runtime_error("DX bank has an invalid global parameter");
}
std::vector<std::uint8_t> readBank(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (size > 8 * 1024 * 1024) throw std::runtime_error("DX bank is too large");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open DX bank");
    return {std::istreambuf_iterator<char>(stream), {}};
}
}

struct DxEngine::Impl {
    struct Channel {
        unsigned pendingMsb {}, pendingLsb {}, msb {}, lsb {}, pc {};
        unsigned volume {100}, expression {127}, pan {64};
        unsigned reverb {40}, chorus {}, variation {}, dry {127}, modulation {};
        unsigned rpnMsb {127}, rpnLsb {127}, bendCents {}, fineTune {8192};
        unsigned pressure {}, ac1Controller {16}, ac2Controller {17};
        std::array<unsigned,128> notePressure {}, controllerValues {};
        std::array<int,12> scaleTune {};
        int bend {8192}, bendSemitones {2}, transpose {}, coarseTune {};
        int mwPitch {}, catPitch {}, patPitch {}, ac1Pitch {}, ac2Pitch {};
        unsigned noteLow {}, noteHigh {127}, velocityLow {1}, velocityHigh {127};
        bool sustain {}, sostenuto {};
        std::array<int,6> carrierOffset {}, modulatorOffset {};
        int feedbackOffset {};
    };
    struct Voice {
        Dx7Note note;
        Lfo lfo;
        std::array<std::int32_t, fmBlock> buffer {};
        unsigned cursor {fmBlock}, channel {}, midiNote {}, level {}, quietBlocks {};
        unsigned releaseAge {};
        int detune {};
        unsigned algorithm {};
        std::uint64_t serial {};
        float left {}, right {};
        double phase {};
        bool active {}, keyHeld {}, released {}, primed {}, sostenutoHeld {};
    };
    struct Event {
        std::uint64_t frame {};
        std::uint32_t packed {};
        unsigned offset {}, size {};
    };
    std::map<unsigned, Preset> presets;
    Preset editVoice;
    bool editVoiceLoaded {};
    std::array<Preset,64> userVoices {};
    unsigned userBankBlock {};
    VlPluginVoiceBulk renderPluginVoice, shadowPluginVoice;
    std::array<Channel, 16> channels {}, shadow {};
    std::array<Voice, voiceLimit> voices {};
    std::array<Event, eventLimit> events {};
    std::array<std::uint8_t, sysexByteLimit> sysexBytes {};
    unsigned sysexUsed {};
    unsigned eventCount {};
    std::uint64_t timeline {}, serial {};
    double step {1.0};
    XgVariationRouting variation;
    MuVariationInsertionMirror variationMirror;
    struct Insertion {
        unsigned typeMsb {}, typeLsb {}, part {127};
        bool nativeAssignment {};
    };
    std::array<Insertion, 4> insertions {};

    explicit Impl(std::span<const std::uint8_t> bank) {
        if (bank.size() < 8 || bank[0] != 'D' || bank[1] != 'X'
            || bank[2] != 'P' || bank[3] != '1')
            throw std::runtime_error("Not a DXP1 voice bank");
        const unsigned count = bank[4] | (bank[5] << 8) | (bank[6] << 16)
            | (bank[7] << 24);
        if (!count || count > 8192) throw std::runtime_error("Invalid DX bank count");
        std::size_t pos = 8;
        for (unsigned i = 0; i < count; ++i) {
            if (pos + 4 > bank.size()) throw std::runtime_error("Truncated DX bank");
            const unsigned msb = bank[pos++], lsb = bank[pos++], pc = bank[pos++];
            Preset preset;
            preset.count = bank[pos++];
            if (msb > 127 || lsb > 127 || pc > 127 || !preset.count || preset.count > 4)
                throw std::runtime_error("Invalid DX selection");
            for (unsigned n = 0; n < preset.count; ++n) {
                if (pos + 159 > bank.size()) throw std::runtime_error("Truncated DX voice");
                auto& e = preset.elements[n];
                e.level = bank[pos++];
                e.shift = static_cast<int>(bank[pos++]) - 64;
                e.detune = static_cast<int>(bank[pos++]) - 64;
                if (e.level > 127 || e.shift < -64 || e.shift > 63
                    || e.detune < -64 || e.detune > 63 || bank[pos++] != 0)
                    throw std::runtime_error("Invalid DX layer metadata");
                for (unsigned j = 0; j < 155; ++j) e.patch[j] = static_cast<char>(bank[pos++]);
                e.patch[155] = 0x3f;
                validate(e);
            }
            if (!presets.emplace(key(msb, lsb, pc), preset).second)
                throw std::runtime_error("Duplicate DX selection");
        }
        if (pos != bank.size()) throw std::runtime_error("Trailing DX bank bytes");
        static std::once_flag initialized;
        std::call_once(initialized, [] {
            Sin::init(); Exp2::init(); Freqlut::init(nativeRate);
            PitchEnv::init(nativeRate); Lfo::init(nativeRate);
        });
    }

    void reset() {
        for (auto& v : voices) v.active = false;
        channels = {};
        variation.reset(); variationMirror.reset(); insertions = {};
        renderPluginVoice = {};
    }
    void add(Event event) {
        if (eventCount == events.size()) throw std::runtime_error("DX MIDI queue overflow");
        unsigned at = eventCount++;
        while (at && events[at - 1].frame > event.frame) {
            events[at] = events[at - 1]; --at;
        }
        events[at] = event;
    }
    void compactSysex() {
        // Event time order need not match byte allocation order. Move surviving
        // payloads in allocation order so compaction cannot overwrite a later source.
        std::array<unsigned,eventLimit> order {};
        unsigned count = 0;
        for (unsigned i = 0; i < eventCount; ++i)
            if (events[i].size) order[count++] = i;
        std::sort(order.begin(),order.begin()+count,[&](unsigned a,unsigned b) {
            return events[a].offset < events[b].offset;
        });
        unsigned used = 0;
        for (unsigned i = 0; i < count; ++i) {
            auto& e = events[order[i]];
            std::memmove(sysexBytes.data()+used,sysexBytes.data()+e.offset,e.size);
            e.offset = used; used += e.size;
        }
        sysexUsed = used;
    }
    static void select(Channel& c, unsigned operation, unsigned a, unsigned b) {
        if (operation == 0xb0 && a == 0) c.pendingMsb = b;
        if (operation == 0xb0 && a == 32) c.pendingLsb = b;
        if (operation == 0xc0) { c.msb = c.pendingMsb; c.lsb = c.pendingLsb; c.pc = a; }
    }
    static void partParameter(Channel& c, unsigned address, unsigned value) {
        switch (address) {
        case 1: c.pendingMsb = value; break;
        case 2: c.pendingLsb = value; break;
        case 3: select(c, 0xc0, value, 0); break;
        case 8: c.transpose = static_cast<int>(value) - 64; break;
        case 0xb: c.volume = value; break;
        case 0xe: c.pan = value; break;
        case 0xf: c.noteLow = value; break;
        case 0x10: c.noteHigh = value; break;
        case 0x11: c.dry = value; break;
        case 0x12: c.chorus = value; break;
        case 0x13: c.reverb = value; break;
        case 0x14: c.variation = value; break;
        case 0x23:
            if (value >= 40 && value <= 88) { c.bendSemitones = static_cast<int>(value) - 64; c.bendCents = 0; }
            break;
        case 0x1d: if (value >= 40 && value <= 88) c.mwPitch = static_cast<int>(value)-64; break;
        case 0x4d: if (value >= 40 && value <= 88) c.catPitch = static_cast<int>(value)-64; break;
        case 0x53: if (value >= 40 && value <= 88) c.patPitch = static_cast<int>(value)-64; break;
        case 0x59: if (value <= 95) c.ac1Controller = value; break;
        case 0x5a: if (value >= 40 && value <= 88) c.ac1Pitch = static_cast<int>(value)-64; break;
        case 0x60: if (value <= 95) c.ac2Controller = value; break;
        case 0x61: if (value >= 40 && value <= 88) c.ac2Pitch = static_cast<int>(value)-64; break;
        case 0x6d: if (value) c.velocityLow = value; break;
        case 0x6e: if (value) c.velocityHigh = value; break;
        default: break;
        }
        if (address >= 0x41 && address <= 0x4c) c.scaleTune[address-0x41] = static_cast<int>(value)-64;
    }
    static void applyPlugin(Channel& c, const VlPluginVoice& voice) {
        c.pendingMsb = c.msb = voice.bankMsb; c.pendingLsb = c.lsb = voice.bankLsb; c.pc = voice.program;
        c.volume = voice.volume; c.bendSemitones = voice.pitchBendRange; c.bendCents = 0;
        c.reverb = voice.reverbSend; c.chorus = voice.chorusSend;
    }
    void observeParameters(const ParameterData& p, bool translated = false) {
        if (p.model == 0x62 && p.high == 0 && p.mid == 0)
            for (unsigned i = 0; i < p.values.size(); ++i)
                if (p.low + i == 0xe && p.values[i] <= 1) userBankBlock = p.values[i];
        if (p.model == 0x62 && p.high == 0x60 && p.mid < channels.size()) {
            auto& c = channels[p.mid];
            for (unsigned i = 0; i < p.values.size(); ++i) {
                const auto address = p.low + i;
                const int offset = static_cast<int>(p.values[i]) - 64;
                if (address >= 0xb && address <= 0x10) c.carrierOffset[0x10-address] = offset;
                if (address >= 0x13 && address <= 0x18) c.modulatorOffset[0x18-address] = offset;
                if (address == 0x1b && offset >= -7 && offset <= 7) c.feedbackOffset = offset;
            }
            return;
        }
        if (p.model != 0x4c) return;
        if (p.high == 8 && p.mid < channels.size()) {
            auto& c = channels[p.mid];
            for (unsigned i = 0; i < p.values.size() && p.low + i < 128; ++i)
                partParameter(c, p.low + i, p.values[i]);
        }
        if (p.high == 3 && p.mid < insertions.size()) {
            auto& insertion = insertions[p.mid];
            for (unsigned i = 0; i < p.values.size(); ++i) {
                switch (p.low + i) {
                case 0: insertion.typeMsb = p.values[i]; break;
                case 1: insertion.typeLsb = p.values[i]; break;
                case 0xc: insertion.part = p.values[i]; insertion.nativeAssignment = !translated; break;
                default: break;
                }
            }
        }
        if (p.high == 2 && p.mid == 1) {
            for (unsigned i = 0; i < p.values.size() && p.low + i < 128; ++i) {
                const auto address = p.low + i;
                const std::array<std::uint8_t,9> message {0xf0,0x43,0x10,0x4c,2,1,
                    static_cast<std::uint8_t>(address),p.values[i],0xf7};
                variation.observe(message);
            }
        }
    }
    std::optional<unsigned> insertionFor(unsigned channel) const {
        for (unsigned i = 0; i < insertions.size(); ++i)
            if ((insertions[i].typeMsb || insertions[i].typeLsb) && insertions[i].part == channel
                && (i != 0 || insertions[i].nativeAssignment || variation.isInsertionPart(channel)))
                return i;
        return {};
    }
    void release(Voice& v) {
        if (!v.released) { v.note.keyup(); v.released = true; }
    }
    void releaseUnheld(unsigned channel) {
        const auto& c = channels[channel];
        for (auto& v : voices)
            if (v.active && v.channel == channel && !v.keyHeld && !c.sustain
                && !(c.sostenuto && v.sostenutoHeld)) release(v);
    }
    void shortMessage(std::uint32_t packed) {
        const unsigned ch = packed & 15, op = packed & 0xf0;
        const unsigned a = (packed >> 8) & 127, b = (packed >> 16) & 127;
        auto& c = channels[ch];
        select(c, op, a, b);
        // Selection affects new notes; existing voices still receive their note-offs.
        if (op == 0xe0) c.bend = a | (b << 7);
        if (op == 0xd0) c.pressure = a;
        if (op == 0xa0) c.notePressure[a] = b;
        if (op == 0xb0) {
            c.controllerValues[a] = b;
            switch (a) {
            case 1: c.modulation = b; break;
            case 7: c.volume = b; break;
            case 10: c.pan = b; break;
            case 11: c.expression = b; break;
            case 91: c.reverb = b; break;
            case 93: c.chorus = b; break;
            case 94: c.variation = b; break;
            case 101: c.rpnMsb = b; break;
            case 100: c.rpnLsb = b; break;
            case 6:
                if (c.rpnMsb == 0) {
                    if (c.rpnLsb == 0) c.bendSemitones = b;
                    if (c.rpnLsb == 1) c.fineTune = (b << 7) | (c.fineTune & 127);
                    if (c.rpnLsb == 2) c.coarseTune = static_cast<int>(b)-64;
                }
                break;
            case 38:
                if (c.rpnMsb == 0 && c.rpnLsb == 0) c.bendCents = b;
                if (c.rpnMsb == 0 && c.rpnLsb == 1) c.fineTune = (c.fineTune & 0x3f80) | b;
                break;
            case 99: case 98: c.rpnMsb = c.rpnLsb = 127; break;
            case 64:
                c.sustain = b >= 64;
                releaseUnheld(ch);
                break;
            case ccSostenuto:
                if (b >= 64 && !c.sostenuto)
                    for (auto& v : voices)
                        if (v.active && v.channel == ch && !v.released) v.sostenutoHeld = true;
                c.sostenuto = b >= 64;
                if (!c.sostenuto) for (auto& v : voices)
                    if (v.channel == ch) v.sostenutoHeld = false;
                releaseUnheld(ch);
                break;
            case 120: for (auto& v : voices) if (v.channel == ch) v.active = false; break;
            case 123: case 124: case 125: for (auto& v : voices) if (v.active && v.channel == ch) {
                v.keyHeld = false;
            } releaseUnheld(ch); break;
            case 121:
                c.expression = 127; c.modulation = 0; c.bend = 8192;
                c.sustain = c.sostenuto = false;
                c.pressure = 0; c.notePressure = {}; c.controllerValues = {};
                c.rpnMsb = c.rpnLsb = 127;
                for (auto& v : voices) if (v.channel == ch) v.sostenutoHeld = false;
                releaseUnheld(ch);
                break;
            default: break;
            }
        }
        if (op == 0x80 || (op == 0x90 && !b)) {
            auto oldest = std::uint64_t(-1);
            for (const auto& v : voices)
                if (v.active && v.channel == ch && v.midiNote == a && v.keyHeld)
                    oldest = std::min(oldest, v.serial);
            for (auto& v : voices) if (v.active && v.serial == oldest) {
                v.keyHeld = false;
            }
            releaseUnheld(ch);
        }
        if (op != 0x90 || !b || !dxBank(c.msb)) return;
        if (a < c.noteLow || a > c.noteHigh || b < c.velocityLow || b > c.velocityHigh) return;
        const auto found = presets.find(key(c.msb, c.lsb, c.pc));
        const Preset* preset = c.msb == 83 && c.lsb == 2 && c.pc == 0 && editVoiceLoaded
            ? &editVoice : c.msb == 35 && c.lsb == 0 && c.pc < userVoices.size() && userVoices[c.pc].count
            ? &userVoices[c.pc] : found != presets.end() ? &found->second : nullptr;
        if (!preset) return;
        const auto group = ++serial;
        for (unsigned layer = 0; layer < preset->count; ++layer) {
            const auto& e = preset->elements[layer];
            auto v = std::find_if(voices.begin(), voices.end(), [](const Voice& voice) { return !voice.active; });
            if (v == voices.end()) v = std::min_element(voices.begin(), voices.end(),
                [](const Voice& x, const Voice& y) {
                    if (x.released != y.released) return x.released;
                    return x.serial < y.serial;
                });
            *v = Voice {};
            v->active = v->keyHeld = true; v->channel = ch; v->midiNote = a;
            v->serial = group; v->level = e.level; v->detune = e.detune;
            v->algorithm = static_cast<unsigned char>(e.patch[134]);
            const int pitch = std::clamp(static_cast<int>(a) + e.shift + c.transpose
                + static_cast<unsigned char>(e.patch[144]) - 24, 0, 127);
            v->note.init(e.patch.data(), pitch, b);
            v->lfo.reset(e.patch.data() + 137); v->lfo.keydown();
        }
    }
    void sysex(std::span<const std::uint8_t> bytes) {
        if (const auto voice = pluginVoice(renderPluginVoice, bytes)) applyPlugin(channels[0], *voice);
        // Packed DX7 banks are checked and staged as a complete transaction.
        if (bytes.size() == 4104 && bytes[0] == 0xf0 && bytes[1] == 0x43
            && bytes[2] < 16 && bytes[3] == 9 && bytes[4] == 32 && bytes[5] == 0 && bytes.back() == 0xf7) {
            unsigned sum = 0;
            for (unsigned i = 6; i < 4103; ++i) { if (bytes[i] > 127) return; sum += bytes[i]; }
            if (sum & 127) return;
            std::array<Preset,32> candidate {};
            for (unsigned i = 0; i < candidate.size(); ++i) {
                candidate[i].count = 1; auto& e = candidate[i].elements[0]; e.level = 127;
                UnpackPatch(reinterpret_cast<const char*>(bytes.data() + 6 + i * 128), e.patch.data());
                try { validate(e); } catch (...) { return; }
            }
            std::copy(candidate.begin(), candidate.end(), userVoices.begin() + userBankBlock * 32);
            return;
        }
        // A complete DX7 edit-buffer dump has 155 data bytes and a Yamaha checksum.
        if (bytes.size() == 163 && bytes[0] == 0xf0 && bytes[1] == 0x43
            && bytes[2] < 16 && bytes[3] == 0 && bytes[4] == 1
            && bytes[5] == 27 && bytes.back() == 0xf7) {
            unsigned checksum = 0;
            for (unsigned i = 6; i < 162; ++i) {
                if (bytes[i] > 127) return;
                checksum += bytes[i];
            }
            if (checksum & 127) return;
            Preset candidate;
            candidate.count = 1;
            auto& e = candidate.elements[0]; e.level = 127;
            std::copy_n(bytes.begin() + 6, 155, e.patch.begin()); e.patch[155] = 0x3f;
            try { validate(e); } catch (...) { return; }
            editVoice = candidate; editVoiceLoaded = true;
            return;
        }
        if (classifySystemReset(bytes) != MidiSystemReset::none) { reset(); return; }
        if (bytes.size() == 7 && bytes[0] == 0xf0 && bytes[1] == 0x43
            && (bytes[2] & 0xf0) == 0x10 && bytes[3] <= 1 && bytes[4] < 128 && bytes[5] < 128
            && bytes.back() == 0xf7 && editVoiceLoaded) {
            const auto address = bytes[3] * 128 + bytes[4];
            if (address >= 156) return;
            auto candidate = editVoice;
            candidate.elements[0].patch[address] = static_cast<char>(bytes[5]);
            if (address == 155 && bytes[5] > 63) return;
            try { validate(candidate.elements[0]); } catch (...) { return; }
            editVoice = candidate;
            return;
        }
        if (const auto translated = variationMirror.observe(bytes))
            if (const auto p = parameters({translated->bytes.data(), translated->size})) observeParameters(*p, true);
        if (const auto p = parameters(bytes)) observeParameters(*p);
    }
    float next(Voice& v, const Channel& c) {
        if (v.cursor == fmBlock) {
            v.buffer.fill(0);
            Controllers controls {};
            controls.values_[kControllerPitch] = c.bend;
            controls.pitchOffsetQ24 = static_cast<std::int32_t>(
                ((c.bend - 8192) / 8192.0 * (c.bendSemitones + c.bendCents / 100.0)
                 + v.detune / 100.0 + c.coarseTune
                 + (static_cast<int>(c.fineTune)-8192)/8192.0
                 + c.scaleTune[v.midiNote%12]/100.0
                 + (static_cast<int>(c.modulation)*c.mwPitch + static_cast<int>(c.pressure)*c.catPitch
                     + static_cast<int>(c.notePressure[v.midiNote])*c.patPitch
                     + static_cast<int>(c.controllerValues[c.ac1Controller])*c.ac1Pitch
                     + static_cast<int>(c.controllerValues[c.ac2Controller])*c.ac2Pitch)/127.0) * (16777216.0 / 12));
            const auto lfo = v.lfo.getsample();
            controls.pitchOffsetQ24 += static_cast<std::int32_t>(
                (lfo - 8388608) * (c.modulation / 127.0) / 12);
            for (unsigned op = 0; op < 6; ++op)
                controls.operatorLevelOffset[op] = FmCore::isCarrier(v.algorithm, op)
                    ? c.carrierOffset[op] : c.modulatorOffset[op];
            controls.feedbackOffset = c.feedbackOffset;
            v.note.compute(v.buffer.data(), lfo, v.lfo.getdelay(), &controls);
            v.cursor = 0;
            std::int64_t peak = 0;
            for (auto sample : v.buffer) peak = std::max(peak, std::abs(static_cast<std::int64_t>(sample)));
            if (v.released) {
                v.releaseAge += fmBlock;
                v.quietBlocks = peak < 256 ? v.quietBlocks + 1 : 0;
                if (v.quietBlocks > 128 || v.releaseAge > nativeRate * 120) v.active = false;
            }
        }
        return v.buffer[v.cursor++] * outputScale * (v.level / 127.0f);
    }
    float sample(Voice& v, const Channel& c) {
        if (!v.primed) { v.left = next(v, c); v.right = next(v, c); v.primed = true; }
        const float value = v.left + static_cast<float>(v.phase) * (v.right - v.left);
        v.phase += step;
        while (v.phase >= 1.0) { v.phase -= 1.0; v.left = v.right; v.right = next(v, c); }
        return value;
    }
};

DxEngine::DxEngine(const std::filesystem::path& path) : DxEngine(readBank(path)) {}
DxEngine::DxEngine(std::span<const std::uint8_t> bank) : impl(std::make_unique<Impl>(bank)) {}
DxEngine::~DxEngine() = default;
void DxEngine::setSampleRate(float rate) {
    if (!std::isfinite(rate) || rate < 8000 || rate > 192000) throw std::runtime_error("Invalid DX sample rate");
    impl->step = nativeRate / static_cast<double>(rate);
}
bool DxEngine::queueShort(std::uint32_t packed, std::uint64_t frame) {
    auto& c = impl->shadow[packed & 15];
    const auto op = packed & 0xf0;
    Impl::select(c, op, (packed >> 8) & 127, (packed >> 16) & 127);
    Impl::Event e; e.frame = frame; e.packed = packed; impl->add(e);
    // Release events must also reach voices started in another engine before
    // the bank changed. Only a nonzero DX note-on is exclusively owned here.
    return dxBank(c.msb) && op == 0x90 && ((packed >> 16) & 127) != 0;
}
void DxEngine::queueSysex(std::span<const std::uint8_t> bytes, std::uint64_t frame) {
    if (bytes.empty()) return;
    if (bytes.size() > sysexByteLimit - impl->sysexUsed) impl->compactSysex();
    if (bytes.size() > sysexByteLimit - impl->sysexUsed)
        throw std::runtime_error("DX SysEx queue overflow");
    Impl::Event e; e.frame = frame; e.size = static_cast<unsigned>(bytes.size()); e.offset = impl->sysexUsed;
    impl->add(e);
    std::copy(bytes.begin(), bytes.end(), impl->sysexBytes.begin() + impl->sysexUsed);
    impl->sysexUsed += e.size;
    if (classifySystemReset(bytes) != MidiSystemReset::none) { impl->shadow = {}; impl->shadowPluginVoice = {}; }
    if (const auto voice = pluginVoice(impl->shadowPluginVoice, bytes)) Impl::applyPlugin(impl->shadow[0], *voice);
    if (const auto p = parameters(bytes); p && p->model == 0x4c && p->high == 8 && p->mid < 16)
        for (unsigned i = 0; i < p->values.size() && p->low + i <= 3; ++i)
            Impl::partParameter(impl->shadow[p->mid], p->low + i, p->values[i]);
}
void DxEngine::render(float* output, unsigned stride, unsigned frames) {
    for (unsigned bus = 0; bus < busCount; ++bus) std::fill_n(output + bus * stride, frames, 0.0f);
    unsigned consumed = 0;
    for (unsigned frame = 0; frame < frames; ++frame) {
        while (consumed < impl->eventCount && impl->events[consumed].frame <= impl->timeline) {
            const auto& e = impl->events[consumed++];
            if (e.size) impl->sysex({impl->sysexBytes.data() + e.offset, e.size}); else impl->shortMessage(e.packed);
        }
        for (auto& v : impl->voices) {
            if (!v.active) continue;
            const auto& c = impl->channels[v.channel];
            const auto value = impl->sample(v, c) * (c.volume / 127.0f) * (c.expression / 127.0f);
            const auto pan = c.pan / 127.0f;
            const float left = value * std::sqrt(1.0f - pan), right = value * std::sqrt(pan);
            auto add = [&](unsigned bus, float gain) {
                output[bus * stride + frame] += left * gain;
                output[(bus + 1) * stride + frame] += right * gain;
            };
            if (const auto insertion = impl->insertionFor(v.channel))
                add(8 + *insertion * 2, 1.0f);
            else {
                add(0, c.dry / 127.0f);
                if (impl->variation.connection() == XgVariationConnection::system) add(6, c.variation / 127.0f);
            }
            add(2, c.reverb / 127.0f); add(4, c.chorus / 127.0f);
        }
        ++impl->timeline;
    }
    if (consumed) {
        std::move(impl->events.begin() + consumed, impl->events.begin() + impl->eventCount, impl->events.begin());
        impl->eventCount -= consumed;
        if (!impl->eventCount) impl->sysexUsed = 0;
    }
}
unsigned DxEngine::selections() const noexcept { return static_cast<unsigned>(impl->presets.size()); }
unsigned DxEngine::activeVoices() const noexcept {
    return static_cast<unsigned>(std::count_if(impl->voices.begin(), impl->voices.end(), [](const auto& v) { return v.active; }));
}
bool DxEngine::selected(unsigned channel) const noexcept { return channel < 16 && dxBank(impl->shadow[channel].msb); }
} // namespace hybrid
