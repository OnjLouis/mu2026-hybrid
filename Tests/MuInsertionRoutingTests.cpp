#include "../Source/MuInsertionRouting.h"

#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    constexpr std::array<std::uint8_t, 10> type {
        0xf0, 0x43, 0x10, 0x4c, 0x02, 0x01, 0x40, 0x49, 0x00, 0xf7,
    };
    const auto mappedType = hybrid::muInsertionForVariation(type);
    assert(mappedType && mappedType->size == 10);
    assert(mappedType->bytes[4] == 0x03);
    assert(mappedType->bytes[5] == 0x00);
    assert(mappedType->bytes[6] == 0x00);
    assert(mappedType->bytes[7] == 0x49);

    constexpr std::array<std::uint8_t, 9> part {
        0xf0, 0x43, 0x10, 0x4c, 0x02, 0x01, 0x5b, 0x02, 0xf7,
    };
    const auto mappedPart = hybrid::muInsertionForVariation(part);
    assert(mappedPart && mappedPart->bytes[6] == 0x0c);
    assert(mappedPart->bytes[7] == 0x02);

    constexpr std::array<std::uint8_t, 9> connection {
        0xf0, 0x43, 0x10, 0x4c, 0x02, 0x01, 0x5a, 0x00, 0xf7,
    };
    assert(!hybrid::muInsertionForVariation(connection));

    hybrid::MuInsertionRouting insertions;
    constexpr std::array<std::uint8_t, 10> distortion {
        0xf0, 0x43, 0x10, 0x4c, 0x03, 0x01, 0x00, 0x49, 0x00, 0xf7,
    };
    constexpr std::array<std::uint8_t, 9> assignedPart {
        0xf0, 0x43, 0x10, 0x4c, 0x03, 0x01, 0x0c, 0x00, 0xf7,
    };
    insertions.observe(distortion);
    insertions.observe(assignedPart);
    assert(insertions.targetFor(0) == 2);
    assert(insertions.targetForSoleRoute(1) == 2);
    assert(!insertions.targetForSoleRoute(3));
    assert(!insertions.targetForSoleRoute(2));
    assert(!insertions.targetFor(1));
    for (std::uint8_t block = 2; block <= 3; ++block) {
        auto nextType = distortion;
        auto nextPart = assignedPart;
        nextType[5] = block;
        nextPart[5] = block;
        nextPart[7] = block;
        insertions.observe(nextType);
        insertions.observe(nextPart);
        assert(insertions.targetFor(block) == block + 1);
        assert(insertions.targetForSoleRoute(1u << block) == block + 1);
    }
    constexpr std::array<std::uint8_t, 10> bypass {
        0xf0, 0x43, 0x10, 0x4c, 0x03, 0x01, 0x00, 0x00, 0x00, 0xf7,
    };
    insertions.observe(bypass);
    assert(!insertions.targetFor(0));
    assert(!insertions.targetForSoleRoute(1));
    assert(insertions.targetFor(2) == 3);
    insertions.reset();
    assert(!insertions.targetFor(2));
    return 0;
}
