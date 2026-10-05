#include "../Source/Vst2Abi.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
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

float render(vst2::EntryPoint entry, bool controlsFirst,
             std::uint8_t controller, std::uint8_t send, int insertion = 0)
{
    auto* effect = entry(host);
    if (!effect) return -1;
    effect->dispatcher(effect, vst2::open, 0, 0, nullptr, 0);
    effect->dispatcher(effect, vst2::setBlockSize, 0, 512, nullptr, 0);
    effect->dispatcher(effect, vst2::mainsChanged, 0, 1, nullptr, 0);
    std::array<unsigned char, 10> type {0xf0, 0x43, 0x10, 0x4c, 3, 0, 0, 0x49, 0, 0xf7};
    std::array<unsigned char, 9> part {0xf0, 0x43, 0x10, 0x4c, 3, 0, 0x0c, 0, 0xf7};
    std::array<vst2::SysexEvent, 2> sysex {};
    if (insertion) {
        type[5] = part[5] = static_cast<unsigned char>(insertion - 1);
        sysex[0].dumpBytes = type.size();
        sysex[0].sysexDump = reinterpret_cast<char*>(type.data());
        sysex[1].dumpBytes = part.size();
        sysex[1].sysexDump = reinterpret_cast<char*>(part.data());
        vst2::Events setup {2};
        setup.events[0] = reinterpret_cast<vst2::Event*>(&sysex[0]);
        setup.events[1] = reinterpret_cast<vst2::Event*>(&sysex[1]);
        effect->dispatcher(effect, vst2::processEvents, 0, 0, &setup, 0);
    }
    std::array<std::uint32_t, 5> messages {
        0xb0u | (std::uint32_t(controller) << 8) | (std::uint32_t(send) << 16),
        0x002100b0u, 0x000020b0u, 0x000040c0u, 0x00643c90u,
    };
    if (!controlsFirst) std::swap(messages[0], messages[1]);
    std::array<vst2::MidiEvent, 5> midi {};
    struct { std::int32_t count; vst2::IntPtr reserved; vst2::Event* items[5]; } batch {};
    batch.count = 5;
    for (std::size_t i = 0; i < midi.size(); ++i) {
        midi[i].type = 1;
        midi[i].byteSize = 24;
        for (int byte = 0; byte < 3; ++byte)
            midi[i].midiData[byte] = static_cast<char>(messages[i] >> (byte * 8));
        batch.items[i] = reinterpret_cast<vst2::Event*>(&midi[i]);
    }
    effect->dispatcher(effect, vst2::processEvents, 0, 0, &batch, 0);
    std::array<float, 512> left {}, right {};
    float* outputs[] {left.data(), right.data()};
    effect->processReplacing(effect, nullptr, outputs, 512);
    const auto value = left[256];
    effect->dispatcher(effect, vst2::mainsChanged, 0, 0, nullptr, 0);
    effect->dispatcher(effect, vst2::close, 0, 0, nullptr, 0);
    return value;
}
}

int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    const auto stage = std::filesystem::absolute(argv[1]).parent_path()
        / ("controller-setup-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(stage / "roms");
    std::filesystem::copy_file(argv[1], stage / "mu2026-hybrid.dll");
    std::filesystem::copy_file(argv[2], stage / "mu2000-engine.bin");
    std::filesystem::copy_file(argv[3], stage / "mu2026-vl-worker.exe");
    std::ofstream(stage / "Sxgpvknl.vxd").put('\0');
    std::vector<char> catalog(0x400000, 0);
    std::copy_n("GrandPno", 8, catalog.begin() + 0x200ee2);
    std::ofstream rom(stage / "roms" / "mu2000_flash.bin", std::ios::binary);
    rom.write(catalog.data(), catalog.size());
    rom.close();
    SetEnvironmentVariableW(L"MU2026_TEST_CONTROLLER_STUB", L"1");
    const auto engine = LoadLibraryW((stage / "mu2000-engine.bin").c_str());
    const auto module = LoadLibraryW((stage / "mu2026-hybrid.dll").c_str());
    const auto select = reinterpret_cast<void(*)(int)>(GetProcAddress(engine, "Mu2026TestInputBus"));
    const auto entry = reinterpret_cast<vst2::EntryPoint>(GetProcAddress(module, "VSTPluginMain"));
    bool passed = select && entry;
    if (passed) {
        constexpr std::array<std::uint8_t,4> controllers {7,91,93,94};
        for (std::size_t plane = 0; plane < controllers.size(); ++plane) {
            select(static_cast<int>(plane * 2));
            const float before = render(entry, true, controllers[plane], 77);
            const float after = render(entry, false, controllers[plane], 77);
            const float off = render(entry, true, controllers[plane], 0);
            const bool correct = before > 0 && before == after && off == 0;
            passed = passed && correct;
            std::printf("CC%d before/after VL bank: %.8g / %.8g; off %.8g: %s\n",
                        controllers[plane], before, after, off, correct ? "PASS" : "FAIL");
        }
        for (int insertion = 2; insertion <= 4; ++insertion) {
            select(10 + (insertion - 2) * 2);
            const float dry = render(entry, false, 7, 77, insertion);
            bool correct = dry > 0;
            for (std::size_t plane = 1; plane < controllers.size(); ++plane) {
                select(static_cast<int>(plane * 2));
                correct &= render(entry, false, controllers[plane], 77, insertion) == 0;
            }
            passed &= correct;
            std::printf("Insertion %d receives dry without duplicate pre-insertion sends: %s\n",
                        insertion, correct ? "PASS" : "FAIL");
        }
    }
    if (module) FreeLibrary(module);
    if (engine) FreeLibrary(engine);
    SetEnvironmentVariableW(L"MU2026_TEST_CONTROLLER_STUB", nullptr);
    std::filesystem::remove_all(stage);
    return passed ? 0 : 1;
}
