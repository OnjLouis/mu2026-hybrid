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

    hybrid::MuInsertion2Routing insertion2;
    constexpr std::array<std::uint8_t, 10> distortion {
        0xf0, 0x43, 0x10, 0x4c, 0x03, 0x01, 0x00, 0x49, 0x00, 0xf7,
    };
    constexpr std::array<std::uint8_t, 9> assignedPart {
        0xf0, 0x43, 0x10, 0x4c, 0x03, 0x01, 0x0c, 0x00, 0xf7,
    };
    insertion2.observe(distortion);
    insertion2.observe(assignedPart);
    assert(insertion2.appliesTo(0));
    assert(insertion2.assignedPart() == 0);
    assert(insertion2.appliesToSoleRoute(1));
    assert(!insertion2.appliesToSoleRoute(3));
    assert(!insertion2.appliesToSoleRoute(2));
    assert(!insertion2.appliesTo(1));
    constexpr std::array<std::uint8_t, 10> bypass {
        0xf0, 0x43, 0x10, 0x4c, 0x03, 0x01, 0x00, 0x00, 0x00, 0xf7,
    };
    insertion2.observe(bypass);
    assert(!insertion2.appliesTo(0));
    assert(!insertion2.assignedPart());
    assert(!insertion2.appliesToSoleRoute(1));
    insertion2.observe(distortion);
    insertion2.reset();
    assert(!insertion2.appliesTo(0));
    return 0;
}
