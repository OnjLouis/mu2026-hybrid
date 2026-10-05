#include "../Source/Vst2Abi.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

vst2::IntPtr host(vst2::AEffect*, std::int32_t opcode, std::int32_t,
                  vst2::IntPtr, void*, float)
{
    if (opcode == vst2::hostVersion) return 2400;
    if (opcode == vst2::hostGetSampleRate) return 44100;
    if (opcode == vst2::hostGetBlockSize) return 512;
    return 0;
}

}

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    const auto wrapperPath = std::filesystem::absolute(argv[1]);
    const auto stage = wrapperPath.parent_path()
        / ("firmware-policy-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directory(stage);
    std::filesystem::copy_file(wrapperPath, stage / "mu2026-hybrid.dll");
    std::filesystem::copy_file(argv[2], stage / "mu2000-engine.bin");
    std::filesystem::create_directory(stage / "roms");
    std::vector<char> catalog(0x400000, 0);
    std::copy_n("GrandPno", 8, catalog.begin() + 0x200ee2);
    std::ofstream rom(stage / "roms" / "mu2000_flash.bin", std::ios::binary);
    rom.write(catalog.data(), catalog.size());
    rom.close();
    // An existing installation keeps its old INI when it updates.
    std::ofstream(stage / "mu2026.ini") << "[engine]\nnative=1\n";

    HMODULE engine = LoadLibraryW((stage / "mu2000-engine.bin").c_str());
    HMODULE wrapper = LoadLibraryW((stage / "mu2026-hybrid.dll").c_str());
    auto mode = reinterpret_cast<int (*)()>(GetProcAddress(engine, "Mu2026TestMode"));
    auto reject = reinterpret_cast<void (*)(bool)>(
        GetProcAddress(engine, "Mu2026TestRejectMode"));
    auto entry = reinterpret_cast<vst2::EntryPoint>(GetProcAddress(wrapper, "VSTPluginMain"));
    auto* effect = entry ? entry(host) : nullptr;
    bool passed = mode && effect && mode() == 0;
    std::printf("firmware mode with legacy native=1: %s (mode=%d)\n",
                passed ? "PASS" : "FAIL", mode ? mode() : -2);
    const auto sendCount = reinterpret_cast<int (*)()>(
        GetProcAddress(engine, "Mu2026TestEffectSendCount"));
    const auto sendValue = reinterpret_cast<int (*)(int)>(
        GetProcAddress(engine, "Mu2026TestEffectSendValue"));
    if (effect && sendCount && sendValue) {
        std::array<vst2::MidiEvent, 4> midi {};
        struct Batch {
            std::int32_t numEvents {4};
            vst2::IntPtr reserved {};
            std::array<vst2::Event*, 4> events {};
        } batch;
        constexpr std::array<unsigned char, 4> controllers {0, 91, 93, 94};
        constexpr std::array<unsigned char, 4> values {33, 30, 0, 40};
        for (std::size_t index = 0; index < midi.size(); ++index) {
            midi[index].type = 1;
            midi[index].byteSize = sizeof(vst2::MidiEvent);
            midi[index].midiData[0] = static_cast<char>(0xb0);
            midi[index].midiData[1] = static_cast<char>(controllers[index]);
            midi[index].midiData[2] = static_cast<char>(values[index]);
            batch.events[index] = reinterpret_cast<vst2::Event*>(&midi[index]);
        }
        effect->dispatcher(effect, vst2::processEvents, 0, 0, &batch, 0.0f);
        std::array<float, 512> left {}, right {};
        std::array<float*, 2> outputs {left.data(), right.data()};
        effect->processReplacing(effect, nullptr, outputs.data(), 512);
        const bool workerOnly = sendCount() == 0;
        passed &= workerOnly;
        std::printf("Non-inserted VL sends stay worker-owned: %s (count=%d)\n",
                    workerOnly ? "PASS" : "FAIL", sendCount());
        std::array<unsigned char, 10> type {0xf0, 0x43, 0x10, 0x4c, 3, 1, 0, 0x49, 0, 0xf7};
        std::array<unsigned char, 9> part {0xf0, 0x43, 0x10, 0x4c, 3, 1, 0x0c, 0, 0xf7};
        std::array<vst2::SysexEvent, 2> sysex {};
        sysex[0].dumpBytes = type.size();
        sysex[0].sysexDump = reinterpret_cast<char*>(type.data());
        sysex[1].dumpBytes = part.size();
        sysex[1].sysexDump = reinterpret_cast<char*>(part.data());
        vst2::Events setup {2};
        setup.events[0] = reinterpret_cast<vst2::Event*>(&sysex[0]);
        setup.events[1] = reinterpret_cast<vst2::Event*>(&sysex[1]);
        effect->dispatcher(effect, vst2::processEvents, 0, 0, &setup, 0);
        effect->processReplacing(effect, nullptr, outputs.data(), 512);
        const bool earlierSendsRestored = sendCount() == 3
            && sendValue(91) == 30 && sendValue(93) == 0 && sendValue(94) == 40;
        passed &= earlierSendsRestored;
        std::printf("Late insertion restores prior VL effect sends: %s (count=%d)\n",
                    earlierSendsRestored ? "PASS" : "FAIL", sendCount());
        batch.numEvents = 3;
        for (std::size_t index = 0; index < 3; ++index)
            batch.events[index] = reinterpret_cast<vst2::Event*>(&midi[index + 1]);
        effect->dispatcher(effect, vst2::processEvents, 0, 0, &batch, 0);
        effect->processReplacing(effect, nullptr, outputs.data(), 512);
        const bool liveSendsForwarded = sendCount() == 6
            && sendValue(91) == 30 && sendValue(93) == 0 && sendValue(94) == 40;
        passed &= liveSendsForwarded;
        std::printf("Inserted VL effect sends reach MU firmware: %s (count=%d)\n",
                    liveSendsForwarded ? "PASS" : "FAIL", sendCount());
    } else {
        passed = false;
    }
    if (effect) effect->dispatcher(effect, vst2::close, 0, 0, nullptr, 0.0f);
    if (reject && entry) {
        reject(true);
        effect = entry(host);
        passed = passed && effect == nullptr;
        std::printf("refused firmware mode: %s\n", effect ? "FAIL" : "PASS");
        if (effect) effect->dispatcher(effect, vst2::close, 0, 0, nullptr, 0.0f);
    } else {
        passed = false;
    }
    if (wrapper) FreeLibrary(wrapper);
    if (engine) FreeLibrary(engine);
    std::filesystem::remove_all(stage);
    return passed ? 0 : 1;
}
