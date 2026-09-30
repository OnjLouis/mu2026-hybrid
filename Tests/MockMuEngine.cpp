#include "../Source/Vst2Abi.h"

#include <algorithm>

namespace {

struct Instance {
    vst2::AEffect effect {};
    int mode {1};
};

int lastMode = -1;
bool rejectMode = false;
int inputBus = -1;

vst2::IntPtr dispatch(vst2::AEffect* effect, std::int32_t opcode,
                     std::int32_t, vst2::IntPtr, void*, float)
{
    if (opcode == vst2::close)
        delete static_cast<Instance*>(effect->object);
    return 0;
}

void render(vst2::AEffect* effect, float** inputs, float** output, std::int32_t count)
{
    const auto mode = static_cast<Instance*>(effect->object)->mode;
    for (int channel = 0; channel < 2; ++channel)
        if (inputBus >= 0 && inputs)
            std::copy_n(inputs[inputBus + channel], count, output[channel]);
        else
            std::fill_n(output[channel], count, mode == 0 ? 0.125f : 0.25f);
}

}

extern "C" __declspec(dllexport) vst2::AEffect* VSTPluginMain(vst2::HostCallback)
{
    auto* instance = new Instance;
    auto& effect = instance->effect;
    effect.magic = vst2::effectMagic;
    effect.dispatcher = dispatch;
    effect.processReplacing = render;
    effect.numPrograms = 1;
    effect.numInputs = 16;
    effect.numOutputs = 2;
    effect.object = instance;
    lastMode = -1;
    return &effect;
}

extern "C" __declspec(dllexport) bool Mu2026SetNativeEngine(
    vst2::AEffect* effect, int mode)
{
    if (rejectMode) return false;
    static_cast<Instance*>(effect->object)->mode = mode;
    lastMode = mode;
    return true;
}

extern "C" __declspec(dllexport) int Mu2026TestMode()
{
    return lastMode;
}

extern "C" __declspec(dllexport) void Mu2026TestRejectMode(bool reject)
{
    rejectMode = reject;
}

extern "C" __declspec(dllexport) void Mu2026TestInputBus(int bus)
{
    inputBus = bus;
}
