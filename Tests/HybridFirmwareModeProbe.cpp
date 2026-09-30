#include "../Source/Vst2Abi.h"

#include <windows.h>

#include <algorithm>
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
