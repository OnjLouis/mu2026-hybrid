#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif

int main(int argc, char** argv) {
    using namespace juce;
    ScopedJuceInitialiser_GUI init;
    if (argc < 2 || argc > 5) return 2;
    AudioPluginFormatManager formats;
    formats.addFormat(std::make_unique<VST3PluginFormat>());
    OwnedArray<PluginDescription> found;
    formats.getFormat(0)->findAllTypesForFile(found, argv[1]);
    if (found.size() != 1) return 3;
    constexpr int rate = 48000, block = 256;
    String error;
    auto plugin = formats.createPluginInstance(*found[0], rate, block, error);
    if (!plugin) { std::cerr << error << '\n'; return 4; }
    plugin->prepareToPlay(rate, block);
    AudioBuffer<float> audio(2, block);
    MidiBuffer midi;
    const auto render = [&] { audio.clear(); plugin->processBlock(audio, midi); midi.clear(); };
    if (argc == 5 && String(argv[2]) == "--recall") {
        auto* vst = plugin->getVST3Client();
        if (!vst) return 31;
        if (plugin->getParameters().size() < 107) return 36;
        auto recall = [&](const File& file) {
            MemoryBlock nks;
            if (!file.loadFileAsData(nks) || nks.getSize() < 12) return false;
            auto* data = static_cast<const uint8_t*>(nks.getData());
            if (file.hasFileExtension(".vstpreset")) return vst->setPreset(nks);
            for (size_t pos = 12; pos + 8 <= nks.getSize();) {
                const auto size = ByteOrder::littleEndianInt(data + pos + 4);
                if (size > nks.getSize() - pos - 8) return false;
                if (std::memcmp(data + pos, "PCHK", 4) == 0 && size > 4)
                    return vst->setPreset(MemoryBlock(data + pos + 12, size - 4));
                pos += 8 + size + (size & 1);
            }
            return false;
        };
        const auto first = File(argv[3]), second = File(argv[4]);
        const bool drums = first.getParentDirectory().getFileName() == "Bank 127-0";
        // The first preset is applied before any audio callback, as on a cold host load.
        if (!recall(first)) return 32;
        auto fingerprint = [&] {
            midi.addEvent(MidiMessage::noteOn(1, drums ? 36 : 60, uint8_t(100)), 0);
            std::vector<float> samples;
            for (int i = 0; i < 80; ++i) {
                if (drums && i == 35) midi.addEvent(MidiMessage::noteOn(1, 38, uint8_t(100)), 0);
                render();
                samples.insert(samples.end(), audio.getReadPointer(0), audio.getReadPointer(0) + block);
            }
            midi.addEvent(MidiMessage::allSoundOff(1), 0);
            render();
            return samples;
        };
        auto warm = [&] { for (int i = 0; i < 1000; ++i) { render(); if (i % 8 == 0) Thread::sleep(10); } };
        warm();
        const auto a = fingerprint();
        if (!recall(second)) return 33;
        warm();
        const auto b = fingerprint();
        if (!recall(first)) return 34;
        warm();
        const auto c = fingerprint();
        auto rms = [](const auto& samples) {
            double sum = 0;
            for (auto sample : samples) sum += double(sample) * sample;
            return std::sqrt(sum / samples.size());
        };
        auto difference = [](const auto& x, const auto& y) {
            double sum = 0;
            for (size_t i = 0; i < x.size(); ++i) sum += double(x[i] - y[i]) * (x[i] - y[i]);
            return std::sqrt(sum / x.size());
        };
        const double aa = rms(a), bb = rms(b), cc = rms(c), ab = difference(a,b);
        std::cout << "Recall RMS " << aa << ' ' << bb << ' ' << cc << "; changed delta " << ab << '\n';
        if (!(aa > .001 && bb > .001 && cc > .001 && ab > .002 && std::abs(aa - cc) < aa * .25)) return 35;
        if (drums) { plugin->releaseResources(); return 0; }
        auto* cutoff = [&]() -> AudioProcessorParameter* {
            for (auto* parameter : plugin->getParameters())
                if (parameter->getName(40) == "Filter Cutoff") return parameter;
            return nullptr;
        }();
        if (!cutoff) return 37;
        cutoff->setValueNotifyingHost(0.0f);
        warm();
        const auto filtered = fingerprint();
        if (!recall(first)) return 38;
        warm();
        const auto reset = fingerprint();
        std::cout << "Cutoff automation delta " << difference(filtered, reset) << '\n';
        if (difference(filtered, reset) < .003) return 39;
        plugin->releaseResources();
        return 0;
    }
    for (int i = 0; i < 1400; ++i) {
        render();
        if (i % 10 == 0) Thread::sleep(10);
    }
    constexpr uint8_t xg[]{0x43, 0x10, 0x4c, 0, 0, 0x7e, 0};
    midi.addEvent(MidiMessage::createSysExMessage(xg, sizeof xg), 0);
    render();
    for (int i = 0; i < 160; ++i) render();
    // VST3 MIDI program-change transport is host-specific; test native XG part selection through SysEx.
    constexpr uint8_t voice1[]{0x43, 0x10, 0x4c, 0x08, 0, 3, 80};
    constexpr uint8_t voice2[]{0x43, 0x10, 0x4c, 0x08, 1, 3, 81};
    midi.addEvent(MidiMessage::createSysExMessage(voice1, sizeof voice1), 0);
    midi.addEvent(MidiMessage::createSysExMessage(voice2, sizeof voice2), 1);
    for (int i = 0; i < 100; ++i) render();
    std::vector<float> rms;
    int late = 0;
    midi.addEvent(MidiMessage::noteOn(1, 60, uint8_t(90)), 0);
    midi.addEvent(MidiMessage::noteOn(2, 67, uint8_t(88)), 70);
    for (int i = 0; i < (20 * rate / block); ++i) {
        const auto start = Time::getMillisecondCounterHiRes();
        render();
        if (i > 20 && Time::getMillisecondCounterHiRes() - start > 1000.0 * block / rate) ++late;
        if (i > 20) rms.push_back(audio.getRMSLevel(0, 0, block));
    }
    if (!std::all_of(rms.begin(), rms.end(), [](float v) { return std::isfinite(v) && v > 1e-5f; })) return 5;
    midi.addEvent(MidiMessage::allSoundOff(1), 0);
    midi.addEvent(MidiMessage::allSoundOff(2), 1);
    render();
    for (int i = 0; i < 200; ++i) render();
    MemoryBlock state;
    plugin->getStateInformation(state);
    if (state.getSize() < 1000) return 6;
    plugin->setStateInformation(state.getData(), int(state.getSize()));
    for (int i = 0; i < 100; ++i) render();
    midi.addEvent(MidiMessage::noteOn(2, 67, uint8_t(88)), 31);
    for (int i = 0; i < 15; ++i) render();
    if (audio.getMagnitude(0, block) < 1e-4f) return 7;
    if (argc == 4 && String(argv[2]) == "--write-legacy-state") {
        if (state.getSize() < 5 * 1024 * 1024 || !File(argv[3]).replaceWithData(state.getData(), state.getSize())) return 44;
        plugin->releaseResources();
        return 0;
    }
    if (argc == 4 && String(argv[2]) == "--compact-state") {
        if (state.getSize() >= 1024 * 1024) return 40;
        MemoryBlock legacy;
        if (!File(argv[3]).loadFileAsData(legacy) || legacy.getSize() < 5 * 1024 * 1024) return 41;
        auto sample = [&](const MemoryBlock& preset) {
            plugin->setStateInformation(preset.getData(), int(preset.getSize()));
            for (int i = 0; i < 120; ++i) render();
            midi.addEvent(MidiMessage::noteOn(1, 60, uint8_t(100)), 0);
            double energy = 0;
            for (int i = 0; i < 80; ++i) { render(); const auto rms = audio.getRMSLevel(0, 0, block); energy += double(rms) * rms; }
            return energy;
        };
        plugin->setStateInformation(legacy.getData(), int(legacy.getSize()));
        for (int i = 0; i < 120; ++i) render();
        MemoryBlock updated;
        plugin->getStateInformation(updated);
        if (updated.isEmpty() || updated.getSize() >= 1024 * 1024) return 42;
        const auto first = sample(legacy), second = sample(updated);
        std::cout << "VST3 legacy recall: " << legacy.getSize() << " -> " << updated.getSize()
                  << " bytes; audio energy: " << first << ", " << second
                  << "; 20-second render late blocks: " << late << '\n';
        if (first < 1e-6 || std::abs(first - second) > 0.1 * first) return 43;
        plugin->releaseResources();
        return 0;
    }
    auto editor = std::unique_ptr<AudioProcessorEditor>(plugin->createEditorAndMakeActive());
    if (!editor || editor->getWidth() <= 0) return 8;
#if JUCE_MAC
    const auto pump = [&] {
        for (int i = 0; i < 50; ++i) {
            render();
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
        }
    };
    pump();
    if (argc >= 3) {
        const File output(argv[2]);
        if (output.exists() || !output.createDirectory()) return 16;
        auto* vst = plugin->getVST3Client();
        if (!vst) return 17;
        auto setPatch = [&](int msb, int pc) {
            const uint8_t setup[]{0x43, 0x10, 0x4c, 0x08, 0, 1, uint8_t(msb), 0, uint8_t(pc)};
            midi.addEvent(MidiMessage::createSysExMessage(setup, sizeof setup), 0);
            pump();
        };
        struct Sound { const char* name; int bank, pc; };
        const Sound sounds[]{{"Grand Piano", 0, 0}, {"Electric Piano 1", 0, 4},
                             {"Square Lead", 0, 80}, {"Standard Kit", 127, 0}, {"Room Kit", 127, 8}};
        auto* volume = [&]() -> AudioProcessorParameter* {
            for (auto* parameter : plugin->getParameters())
                if (parameter->getName(40) == "Volume") return parameter;
            return nullptr;
        }();
        if (!volume) return 18;
        for (int channel = 1; channel <= 16; ++channel)
            midi.addEvent(MidiMessage::allSoundOff(channel), 0);
        pump();
        setPatch(0, 19);
        midi.addEvent(MidiMessage::controllerEvent(1, 91, 0), 0);
        pump();
        for (int i = 0; i < 700; ++i) render();
        const auto loudness = [&] {
            midi.addEvent(MidiMessage::allSoundOff(1), 0);
            render();
            midi.addEvent(MidiMessage::noteOn(1, 60, uint8_t(100)), 0);
            float peak = 0;
            for (int i = 0; i < 60; ++i) { render(); if (i > 12) peak = jmax(peak, audio.getMagnitude(0, block)); }
            midi.addEvent(MidiMessage::allSoundOff(1), 0);
            render();
            return peak;
        };
        volume->setValueNotifyingHost(1.0f);
        pump();
        const auto loud = loudness();
        volume->setValueNotifyingHost(0.0f);
        pump();
        const auto muted = loudness();
        std::cout << "Volume test peaks: " << loud << ", " << muted << '\n';
        if (loud < 0.001f || muted > loud * 0.1f) return 24;
        volume->setValueNotifyingHost(100.0f / 127.0f);
        pump();
        std::cout << "VST3: Volume automation changes the rendered sound\n";
        for (const auto& patch : sounds) {
            setPatch(patch.bank, patch.pc);
            const auto bytes = vst->getPreset();
            if (bytes.getSize() < 1000 || !output.getChildFile(String(patch.name) + ".vstpreset")
                    .replaceWithData(bytes.getData(), bytes.getSize())) return 19;
            setPatch(0, 24);
            if (!vst->setPreset(bytes)) return 20;
            pump();
            midi.addEvent(MidiMessage::noteOn(1, patch.bank == 127 ? 36 : 60, uint8_t(100)), 0);
            std::vector<float> samples;
            for (int i = 0; i < 240; ++i) {
                if (i == 140) midi.addEvent(MidiMessage::allNotesOff(1), 0);
                if (patch.bank == 127 && i == 48) midi.addEvent(MidiMessage::noteOn(1, 38, uint8_t(100)), 0);
                render();
                for (int f = 0; f < block; ++f)
                    for (int c = 0; c < 2; ++c) samples.push_back(audio.getSample(c, f));
            }
            if (std::none_of(samples.begin(), samples.end(), [](float v) { return std::abs(v) > 1e-4f; })) return 22;
            if (!output.getChildFile(String(patch.name) + ".f32").replaceWithData(samples.data(), samples.size() * sizeof(float))) return 23;
        }
        std::cout << "VST3: five SDK preset containers accepted and rendered audio\n";
    }
    if (argc == 4) {
        OwnedArray<PluginDescription> priorTypes;
        formats.getFormat(0)->findAllTypesForFile(priorTypes, argv[3]);
        if (priorTypes.size() != 1 || priorTypes[0]->uniqueId != found[0]->uniqueId) return 25;
        auto prior = formats.createPluginInstance(*priorTypes[0], rate, block, error);
        if (!prior) return 26;
        prior->prepareToPlay(rate, block);
        MidiBuffer oldMidi;
        for (int i = 0; i < 1200; ++i) {
            prior->processBlock(audio, oldMidi);
            if (i % 10 == 0) Thread::sleep(10);
        }
        oldMidi.addEvent(MidiMessage::createSysExMessage(voice1, sizeof voice1), 0);
        for (int i = 0; i < 160; ++i) { prior->processBlock(audio, oldMidi); oldMidi.clear(); }
        auto* oldVst = prior->getVST3Client();
        auto* newVst = plugin->getVST3Client();
        if (!oldVst || !newVst) return 27;
        const auto priorState = oldVst->getPreset();
        if (priorState.getSize() < 1000 || !newVst->setPreset(priorState)) return 28;
        pump();
        midi.addEvent(MidiMessage::noteOn(1, 60, uint8_t(100)), 0);
        float peak = 0;
        for (int i = 0; i < 50; ++i) { render(); peak = jmax(peak, audio.getMagnitude(0, block)); }
        if (peak < 0.001f) return 29;
        prior->releaseResources();
        std::cout << "VST3: old installed component ID and preset state accepted by renamed build\n";
    }
#endif
    editor.reset();
    plugin->releaseResources();
    std::cout << "VST3: 20 seconds sustained on two channels; XG reset and state recall; " << late
              << " late render blocks out of " << rms.size() << '\n';
}
