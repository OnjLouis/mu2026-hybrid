#include "../Source/MuVoiceCatalog.h"
#include "../Source/XglVoiceMap.h"

#include <cstdint>
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    const hybrid::MuVoiceCatalog mu(argv[1]);
    const auto le = hybrid::XglVoiceMap::load(argv[2]);
    if (!mu.valid() || !mu.hasDistinctVoice(0, 0, 0)
        || !mu.hasDistinctVoice(127, 0, 0))
        return 3;
    unsigned muDistinct = 0;
    unsigned leFallback = 0;
    for (int lsb = 0; lsb != 128; ++lsb)
        for (int program = 0; program != 128; ++program) {
            const auto b = static_cast<std::uint8_t>(lsb);
            const auto p = static_cast<std::uint8_t>(program);
            if (mu.hasDistinctVoice(0, b, p))
                ++muDistinct;
            else if (le.shouldUse2006Engine(0, b, p)) {
                ++leFallback;
                if (leFallback <= 12)
                    std::printf("MU gap: 0:%d:%d\n", lsb, program);
            }
        }
    std::printf("MU distinct=%u, 2006LE fallbacks=%u\n",
                muDistinct, leFallback);
    return muDistinct > 128 && leFallback > 0 ? 0 : 4;
}
