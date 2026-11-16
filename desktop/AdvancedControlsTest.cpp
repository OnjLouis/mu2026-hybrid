#include "ControlCatalog.h"
#include <iostream>

using namespace mu_pilot;

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) { std::cerr << name << '\n'; ++failures; }
    };
    ui::xg_snapshot snapshot{};
    int value = 0;
    const auto& variation = controls[controlIndex("variationFx1")];
    auto* effect = snapshot.effect + (xg::ram::VAR_BLOCK - xg::ram::EFFECT);
    effect[0] = 0x46; // Tremolo
    effect[xg::ram::VAR_WIDE + 1] = 65;
    check(readControl(variation, 0, 36, snapshot, value) && value == 65, "wide variation readback");
    auto message = writeControl(variation, 0, 36, snapshot, 127);
    check(message.size == 10 && message.data[6] == 0x42 && message.data[7] == 0 && message.data[8] == 127,
          "atomic variation write");
    const auto* binding = effectBinding(variation, snapshot).parameter;
    check(binding && decodeEffectValue(*binding, 16383, encodeEffectValue(*binding, 16383, 65)) == 65,
          "knob spans algorithm range without losing resolution");
    check(!writeControl(controls[controlIndex("chorusFx9")], 0, 36, snapshot, 1).size,
          "no write without a matching algorithm parameter");
    const auto& route = controls[controlIndex("insertion4Part")];
    message = writeControl(route, 0, 36, snapshot, 66);
    check(message.size == 9 && message.data[4] == 3 && message.data[5] == 3 && message.data[6] == 0x0c
          && message.data[7] == 127, "insertion 4 off routing");
    const auto& drum = controls[controlIndex("drumReverb")];
    check(!writeControl(drum, 3, 36, snapshot, 42).size, "no drum write to a melodic part");
    snapshot.parts[3][0x07] = 4; // DRUMS3
    snapshot.drum[2][36 - ui::XG_DRUM_KEY0][5] = 51;
    check(readControl(drum, 3, 36, snapshot, value) && value == 51, "selected kit/note readback");
    message = writeControl(drum, 3, 38, snapshot, 42);
    check(message.size == 9 && message.data[4] == 0x32 && message.data[5] == 38 && message.data[6] == 5
          && message.data[7] == 42, "drum write targets one setup and note");
    check(!writeControl(drum, 3, 12, snapshot, 42).size, "invalid drum note");
    return failures ? 1 : 0;
}
