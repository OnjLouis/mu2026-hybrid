#include "../Source/MuVoiceCatalog.h"
#include "../Source/XglVoiceMap.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {
constexpr std::size_t romSize = 0x400000;
constexpr std::size_t voiceStart = 0x200ee0;
constexpr std::size_t drumMap = 0x299fcc;
constexpr std::size_t drumNames = 0x299dc0;
constexpr std::size_t sfxMap = 0x29be58;
constexpr std::size_t sfxNames = 0x29bdec;
constexpr std::size_t nameStride = 12;

struct Fixture {
    std::filesystem::path path = std::filesystem::temp_directory_path()
        / ("MuDrumFallbackTests-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    ~Fixture() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    void write(const std::vector<std::uint8_t>& bytes) const {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        assert(output.good());
    }
};
}

int main()
{
    // Synthetic table markers only; no Yamaha ROM or preset data is required.
    std::vector<std::uint8_t> rom(romSize);
    std::memcpy(rom.data() + voiceStart + 2, "GrandPno", 8);
    std::memcpy(rom.data() + drumNames, "StandKit", 8);
    std::memcpy(rom.data() + drumNames + nameStride, "SilenKit", 8);
    std::memcpy(rom.data() + sfxNames, "SFXKit 1", 8);
    std::memcpy(rom.data() + sfxNames + nameStride, "SilenKit", 8);
    rom[drumMap + 112] = 1;
    rom[sfxMap + 112] = 1;
    Fixture fixture;
    fixture.write(rom);
    const hybrid::MuVoiceCatalog mu(fixture.path);
    assert(mu.valid());
    assert(!mu.hasDistinctVoice(127, 0, 112));
    assert(!mu.hasDistinctVoice(126, 0, 112));
    for (unsigned lsb = 0; lsb < 128; ++lsb) {
        for (unsigned program = 0; program < 128; ++program) {
            for (const auto bank : {126, 127}) {
                assert(mu.hasDistinctVoice(bank, lsb, program) == (program != 112));
            }
        }
    }
    std::vector<std::uint8_t> table(hybrid::XglVoiceMap::requiredTableSize, 0xff);
    table[hybrid::XglVoiceMap::bankMapOffset + 127 * 128] = 0;
    const auto voice = hybrid::XglVoiceMap::programMapOffset + 112 * 2;
    table[voice] = 1;
    table[voice + 1] = 0;
    const hybrid::XglVoiceMap le(table);
    assert(le.shouldUse2006Engine(127, 0, 112));
    assert(!le.shouldUse2006Engine(127, 0, 113));
    // Unknown drum table layouts must retain MU rather than guess a fallback.
    rom[drumNames] = 0;
    fixture.write(rom);
    assert(hybrid::MuVoiceCatalog(fixture.path).hasDistinctVoice(127, 0, 112));
    fixture.write({});
    const hybrid::MuVoiceCatalog invalid(fixture.path);
    assert(!invalid.valid());
    assert(invalid.hasDistinctVoice(127, 0, 112));
}
