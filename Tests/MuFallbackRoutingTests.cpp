#include "../Source/MuVoiceCatalog.h"
#include "../Source/XglEngine.h"

#include <cassert>
#include <cstdint>

namespace {

vst2::IntPtr host(vst2::AEffect*, std::int32_t opcode,
                   std::int32_t, vst2::IntPtr, void*, float)
{
    if (opcode == vst2::hostVersion)
        return 2400;
    if (opcode == vst2::hostGetSampleRate)
        return 44'100;
    if (opcode == vst2::hostGetBlockSize)
        return 512;
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 4)
        return 2;
    const hybrid::MuVoiceCatalog mu(argv[1]);
    if (!mu.valid())
        return 3;
    hybrid::XglEngine le(argv[2], argv[3], host, 44'100.0f, 512);
    le.setMuVoiceCatalog(&mu);

    le.queueShort(0x000000b0u, 0); // MSB 0
    le.queueShort(0x000000c0u, 0); // Program 0
    assert(!le.selectedVoice(0));
    assert(!le.queueShort(0x00643c90u, 0)); // MU owns base grand piano
    le.queueShort(0x007020b0u, 0); // LSB 112, 2006LE panel bank
    assert(le.selectedVoice(0));
    assert(le.queueShort(0x00643d90u, 0));
    le.queueShort(0x000020b0u, 0); // Back to MU base bank
    assert(!le.selectedVoice(0));
    assert(!le.queueShort(0x00643e90u, 0));
    // Castaway's PSR Dance Kit is absent in MU, but its other drum part is MU-owned.
    le.reset(hybrid::MidiSystemReset::xg);
    le.queueShort(0x007f00b8u, 0);
    le.queueShort(0x000020b8u, 0);
    le.queueShort(0x000070c8u, 0);
    assert(le.selectedVoice(8));
    assert(le.queueShort(0x00642498u, 0));
    assert(!le.queueShort(0x00002488u, 0)); // Releases also reach MU.
    le.queueShort(0x007f00b9u, 0);
    le.queueShort(0x000020b9u, 0);
    le.queueShort(0x000001c9u, 0);
    assert(!le.selectedVoice(9));
    assert(!le.queueShort(0x00642499u, 0));
    return 0;
}
