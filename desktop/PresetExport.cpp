#include "PresetExport.h"
#include "ControlCatalog.h"
#include "PresetAddress.h"
#include "vst3/engine.h"
#include "xg/voices.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#if JUCE_WINDOWS
#include <windows.h>
#elif JUCE_MAC
#include <stdio.h>
#endif

namespace mu_pilot {
std::shared_ptr<const std::vector<u8>> presetRom();
namespace {
using namespace juce;
constexpr auto classId = "ABCDEF019182FAEB4F6E6A4D4D75326B";
constexpr auto receiptName = ".onjmusic-receipt.json";
constexpr int rate = 44100, block = 256;
using Entry = std::pair<PresetAddress, String>;

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

bool moveWithoutReplacing(const File& from, const File& to) {
#if JUCE_WINDOWS
    return MoveFileExW(from.getFullPathName().toWideCharPointer(), to.getFullPathName().toWideCharPointer(), 0) != 0;
#elif JUCE_MAC
    return renamex_np(from.getFullPathName().toRawUTF8(), to.getFullPathName().toRawUTF8(), RENAME_EXCL) == 0;
#else
    if (to.exists() || to.isSymbolicLink()) return false;
    return from.moveFileTo(to);
#endif
}

var object(std::initializer_list<std::pair<const char*, var>> values) {
    auto result = std::make_unique<DynamicObject>();
    for (const auto& [key, value] : values) result->setProperty(key, value);
    return var(result.release());
}
var list(std::initializer_list<var> values) {
    Array<var> result;
    for (const auto& value : values) result.add(value);
    return var(result);
}

var presetTypes(PresetAddress a) {
    auto type = [](const char* main, const char* sub) {
        return list({sub[0] ? list({main, sub}) : list({main})});
    };
#define MU_SPECIAL(bank, variation, pc, main, sub) \
    if (a.msb == (bank) && ((variation) < 0 || int(a.lsb) == (variation)) && ((pc) < 0 || int(a.program) == (pc))) return type(main, sub);
#include "PresetSpecialTypes.inc"
#undef MU_SPECIAL
    // Expansion banks need their own catalogue; their programs are not GM numbers.
    if (a.msb != 0 && a.msb != 121) return type("Synth", "Misc");
#define MU_TYPE(first, last, main, sub) \
    if (a.program >= (first) && a.program <= (last)) return type(main, sub);
#include "PresetTypes.inc"
#undef MU_TYPE
    return type("Synth", "Misc");
}

// MessagePack subset used by the HS2 NKS format: map, array, string, bool and unsigned ID.
void writeValue(MemoryOutputStream& out, const var& v) {
    if (const auto* a = v.getArray()) {
        out.writeByte(char(0xdc)); out.writeShortBigEndian(short(a->size()));
        for (const auto& item : *a) writeValue(out, item);
    } else if (const auto* o = v.getDynamicObject()) {
        out.writeByte(char(0xde)); out.writeShortBigEndian(short(o->getProperties().size()));
        for (const auto& property : o->getProperties()) {
            writeValue(out, property.name.toString()); writeValue(out, property.value);
        }
    } else if (v.isBool()) {
        out.writeByte(char(bool(v) ? 0xc3 : 0xc2));
    } else if (v.isInt() || v.isInt64()) {
        out.writeByte(char(0xce)); out.writeIntBigEndian(int(int64(v)));
    } else {
        const auto text = v.toString();
        out.writeByte(char(0xda)); out.writeShortBigEndian(short(text.getNumBytesAsUTF8()));
        out.write(text.toRawUTF8(), text.getNumBytesAsUTF8());
    }
}

void chunk(MemoryOutputStream& out, const char* tag, const MemoryBlock& data) {
    out.write(tag, 4); out.writeInt(int(data.getSize())); out.write(data.getData(), data.getSize());
    if (data.getSize() & 1) out.writeByte(0);
}
MemoryBlock packed(const var& v) {
    MemoryOutputStream out;
    out.writeInt(1); writeValue(out, v);
    return out.getMemoryBlock();
}

MemoryBlock vst(PresetAddress address) {
    const auto component = address.encode();
    constexpr int header = 48;
    MemoryOutputStream out;
    out.write("VST3", 4); out.writeInt(1); out.write(classId, 32); out.writeInt64(header + component.size());
    out.write(component.data(), component.size());
    out.write("List", 4); out.writeInt(2);
    out.write("Comp", 4); out.writeInt64(header); out.writeInt64(component.size());
    out.write("Cont", 4); out.writeInt64(header + component.size()); out.writeInt64(0);
    return out.getMemoryBlock();
}

MemoryBlock makeNks(const Entry& entry, String bank) {
    const auto [address, name] = entry;
    const auto drums = address.msb >= 126;
    const auto key = String("OnjMusic/MU2000/") + String(address.msb) + "/" + String(address.lsb) + "/" + String(address.program);
    auto raw = SHA256(key.toUTF8()).getRawData();
    auto* bytes = static_cast<uint8*>(raw.getData());
    bytes[6] = uint8((bytes[6] & 15) | 80);
    bytes[8] = uint8((bytes[8] & 63) | 128);
    const auto uuid = Uuid(bytes).toDashedString();
    Array<var> pages;
    StringArray names;
    for (const auto& c : controls) if (!names.contains(c.page)) names.add(c.page);
    for (const auto& page : names) {
        if (drums && page != "Mixer" && page != "Reverb" && page != "Chorus" && page != "Variation"
            && !page.startsWith("Master")) continue;
        Array<var> knobs;
        for (const auto& c : controls) {
            if (page != c.page) continue;
            auto knob = object({{"id", int(String(c.id).hashCode() & 0x7fffffff)}, {"name", c.label},
                                {"autoname", false}, {"vflag", false}});
            if (knobs.isEmpty()) knob.getDynamicObject()->setProperty("section", page);
            knobs.add(knob);
        }
        pages.add(var(knobs));
    }
    MemoryOutputStream riff;
    riff.write("NIKS", 4);
    chunk(riff, "NISI", packed(object({{"name", name}, {"vendor", "Yamaha"}, {"author", "OnjMusic"},
        {"deviceType", "INST"}, {"uuid", uuid}, {"bankchain", list({"MU2000", drums ? "Kits" : "Programs", bank})},
        {"types", presetTypes(address)}, {"characters", var(Array<var>{})}})));
    chunk(riff, "NICA", packed(object({{"ni8", var(pages)}})));
    chunk(riff, "PLID", packed(object({{"VST.magic", 0}, {"VST3.uid", list({int64(0xabcdef01), int64(0x9182faeb),
        int64(0x4f6e6a4d), int64(0x4d75326b)})}, {"pluginName", "MU2000"}, {"pluginVendor", "OnjMusic"}})));
    MemoryOutputStream state;
    state.writeInt(1);
    const auto native = vst(address);
    state.write(native.getData(), native.getSize());
    chunk(riff, "PCHK", state.getMemoryBlock());
    MemoryOutputStream result;
    result.write("RIFF", 4); result.writeInt(int(riff.getDataSize())); result.write(riff.getData(), riff.getDataSize());
    return result.getMemoryBlock();
}

std::vector<Entry> catalogue(const xg::voice_rom& rom, const ui::xg_snapshot& mode) {
    std::vector<Entry> entries;
    std::set<std::pair<int, u32>> used;
    // Record aliases do not make a new sound. Keep the first supported address per program and record.
    for (int a = 0; a < 128; ++a) {
        for (int b = 0; b < (a >= 126 ? 1 : 128); ++b) {
            if (a < 126 && !rom.lsb_ok(mode.voice_mode, mode.voice_set, a, b)) continue;
            for (int pc = 0; pc < 128; ++pc) {
                std::string name;
                if (a >= 126) name = rom.kit_name(a, pc);
                else {
                    const auto record = rom.lookup(mode.voice_mode, mode.voice_set, a, b, pc);
                    if (!record || !used.emplace(pc, record).second) continue;
                    name = rom.record_name(record);
                }
                if (!name.empty() && name != "Silence")
                    entries.push_back({{u8(a), u8(b), u8(pc)}, String::fromUTF8(name.c_str())});
            }
        }
    }
    return entries;
}

bool preview(smu2000::vst3::engine& engine, const Entry& entry, const File& target) {
    AudioBuffer<float> audio(2, block * 240);
    auto render = [&](int n) {
        std::array<float, block> l{}, r{};
        while (n--) engine.fill(l.data(), r.data(), block);
    };
    const auto setup = entry.first.setup();
    const auto encoded = entry.first.encode();
    if (!engine.load_state(encoded.data(), 0, setup.data(), setup.size())) return false;
    render(96);
    ui::xg_snapshot state{};
    engine.panel().read_xg(state);
    if (!state.serial || !entry.first.matches(state.parts[0])) return false;
    const bool kit = entry.first.msb >= 126;
    for (int i = 0; i < 240; ++i) {
        if (i == 0 || i == 48 || i == 96) {
            const u8 note[]{0x90, u8(kit ? (i == 0 ? 36 : i == 48 ? 38 : 42) : (i == 0 ? 60 : i == 48 ? 64 : 67)), 96};
            engine.midi(note, sizeof note);
        }
        if (i == 145) { const u8 off[]{0xb0, 123, 0}; engine.midi(off, sizeof off); }
        engine.fill(audio.getWritePointer(0, i * block), audio.getWritePointer(1, i * block), block);
    }
    int onset = -1;
    for (int i = 0; i < audio.getNumSamples(); ++i) {
        const auto l = audio.getSample(0, i), r = audio.getSample(1, i);
        if (!std::isfinite(l) || !std::isfinite(r)) return false;
        if (onset < 0 && std::max(std::abs(l), std::abs(r)) > 0.001f) onset = i;
    }
    if (onset < 0) return false;
    if (target == File{}) return true;
    onset = std::max(0, onset - rate / 200);
    std::unique_ptr<OutputStream> stream = target.createOutputStream();
    if (!stream) return false;
    auto writer = OggVorbisAudioFormat().createWriterFor(stream,
        AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(2).withBitsPerSample(16).withQualityOptionIndex(3));
    return writer && writer->writeFromAudioSampleBuffer(audio, onset, audio.getNumSamples() - onset);
}

var inventory(const File& directory) {
    auto result = std::make_unique<DynamicObject>();
    for (const auto& file : directory.findChildFiles(File::findFilesAndDirectories, false)) {
        if (file.isSymbolicLink()) throw std::runtime_error("Preset folder contains a link");
        if (file.isDirectory()) result->setProperty(file.getFileName(), inventory(file));
        else if (file.existsAsFile() && file.getFileName() != receiptName)
            result->setProperty(file.getFileName(), SHA256(file).toHexString());
    }
    return var(result.release());
}

bool unchanged(const File& directory) {
    const auto receipt = directory.getChildFile(receiptName);
    if (directory.isSymbolicLink() || !receipt.existsAsFile() || receipt.isSymbolicLink()) return false;
    const auto expected = JSON::parse(receipt);
    const auto compare = [&](auto&& self, const var& want, const var& have) -> bool {
        if (!want.isObject() || !have.isObject()) return want.isString() && have.isString() && want == have;
        const auto& fields = have.getDynamicObject()->getProperties();
        if (want.getDynamicObject()->getProperties().size() != fields.size()) return false;
        for (const auto& field : fields)
            if (!self(self, want.getProperty(field.name, {}), field.value)) return false;
        return true;
    };
    return compare(compare, expected, inventory(directory));
}
}

String exportPresets(const File& destination, PresetFormat format, const std::atomic<bool>& cancel,
                     const Progress& progress) try {
    const bool nksFormat = format == PresetFormat::nks;
    if (cancel.load()) return "Cancelled. Your existing presets were not changed.";
    require(nksFormat ? destination.isDirectory() : destination.createDirectory().wasOk(),
        nksFormat ? "Choose an existing Komplete Kontrol User Content folder." : "Cannot create the VST3 preset folder.");
    const auto live = destination.getChildFile(nksFormat ? "MU2000 Generated" : "MU2000 VST3 Generated");
    require(!live.isSymbolicLink() && (!live.exists() || unchanged(live)), "Generated folder has changed; choose another destination or move it aside.");
    if (cancel.load()) return "Cancelled. Your existing presets were not changed.";
    const auto stage = destination.getChildFile(".mu2000-staging-" + Uuid().toString());
    require(stage.createDirectory().wasOk(), "Cannot create staging folder.");
    struct CleanStage { File folder; ~CleanStage() { folder.deleteRecursively(); } } cleanup{stage};
    const auto owner = std::make_unique<smu2000::vst3::engine>();
    auto& engine = *owner;
    require(engine.wait_ready(30000), "The MU2000 ROMs could not be loaded.");
    engine.set_output_rate(rate);
    engine.set_processing(true);
    const auto romBytes = presetRom();
    const xg::voice_rom rom(romBytes);
    require(rom.ok(), "This ROM does not expose a recognised preset catalogue.");
    std::array<float, block> l{}, r{};
    for (int i = 0; i < 64; ++i) engine.fill(l.data(), r.data(), block);
    ui::xg_snapshot state{};
    engine.panel().read_xg(state);
    const auto entries = catalogue(rom, state);
    int done = 0, written = 0, skipped = 0;
    for (const auto& entry : entries) {
        if (cancel.load()) return "Cancelled. Your existing presets were not changed.";
        if (progress) progress(done, int(entries.size()));
        ++done;
        const auto a = entry.first;
        const auto bank = String("Bank ") + String(a.msb) + "-" + String(a.lsb);
        const auto directory = stage.getChildFile(a.msb >= 126 ? "Kits" : "Programs").getChildFile(bank);
        const auto previews = directory.getChildFile(".previews");
        require((nksFormat ? previews : directory).createDirectory().wasOk(), "Cannot create preset directory.");
        const auto stem = String(a.program + 1) + " " + entry.second.removeCharacters("\\/:*?\"<>|").trim();
        const auto ogg = nksFormat ? previews.getChildFile(stem + ".nksf.ogg") : File{};
        if (!preview(engine, entry, ogg)) { if (nksFormat) ogg.deleteFile(); ++skipped; continue; }
        const auto preset = nksFormat ? makeNks(entry, bank) : vst(a);
        require(directory.getChildFile(stem + (nksFormat ? ".nksf" : ".vstpreset"))
            .replaceWithData(preset.getData(), preset.getSize()), "Cannot write a preset.");
        ++written;
    }
    engine.set_processing(false);
    require(written > 0, "No verified sounds were found; the existing library was not changed.");
    require(stage.getChildFile(receiptName).replaceWithText(JSON::toString(inventory(stage))), "Cannot write preset receipt.");
    if (cancel.load()) return "Cancelled. Your existing presets were not changed.";
    const auto rollback = destination.getNonexistentChildFile(".mu2000-rollback", {}, false);
    const bool replacing = live.exists();
    require(!live.isSymbolicLink() && (!replacing || unchanged(live)), "The destination changed during export.");
    if (replacing) require(moveWithoutReplacing(live, rollback), "Cannot move the previous generated bank.");
    if (!moveWithoutReplacing(stage, live)) {
        if (replacing && !moveWithoutReplacing(rollback, live)) return "Could not restore the previous bank. It is at " + rollback.getFullPathName();
        return "Could not install the generated bank. No existing presets were changed.";
    }
    if (replacing && !rollback.deleteRecursively()) return "Presets generated. The previous bank is still at " + rollback.getFullPathName();
    return "Generated " + String(written) + (nksFormat ? " presets with previews. Skipped " : " VST3 presets. Skipped ")
        + String(skipped) + " addresses that did not verify."
        + (nksFormat ? " Rescan the Komplete Kontrol User library." : "");
} catch (const std::exception& error) {
    return String::fromUTF8(error.what());
}
}
