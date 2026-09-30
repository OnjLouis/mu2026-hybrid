#include "../Source/NativeVlProtocol.h"

#include <windows.h>

#include <cstdint>
#include <algorithm>
#include <array>

namespace {

HANDLE parseHandle(const wchar_t* text)
{
    return reinterpret_cast<HANDLE>(_wcstoui64(text, nullptr, 10));
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc != 6)
        return 64;
    const auto mapping = parseHandle(argv[1]);
    const auto requestEvent = parseHandle(argv[2]);
    const auto responseEvent = parseHandle(argv[3]);
    auto* shared = static_cast<hybrid::ipc::SharedState*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0,
        sizeof(hybrid::ipc::SharedState)));
    if (shared == nullptr)
        return 65;

    InterlockedExchange(&shared->result, 0);
    SetEvent(responseEvent);
    bool running = true;
    const bool controllerAudio = GetEnvironmentVariableW(
        L"MU2026_TEST_CONTROLLER_STUB", nullptr, 0) != 0;
    std::array<std::uint8_t, 128> controllers {};
    const auto observe = [&](std::uint32_t message) {
        if ((message & 0xf0) == 0xb0)
            controllers[(message >> 8) & 0x7f] = (message >> 16) & 0x7f;
    };
    while (running
           && WaitForSingleObject(requestEvent, INFINITE) == WAIT_OBJECT_0) {
        const auto command = static_cast<hybrid::ipc::Command>(
            InterlockedExchange(&shared->command, 0));
        if (command == hybrid::ipc::Command::shutdown)
            running = false;
        else if (command == hybrid::ipc::Command::getRouteMask)
            shared->argument = 0;
        else if (controllerAudio && command == hybrid::ipc::Command::sendShort)
            observe(shared->argument);
        else if (controllerAudio && (command == hybrid::ipc::Command::render
                                      || command == hybrid::ipc::Command::renderTimed)) {
            for (std::size_t i = 0; i < shared->timedMidiEventCount; ++i)
                observe(shared->timedMidiEvents[i].message);
            constexpr std::array<int, 4> sends {7, 91, 93, 94};
            for (std::size_t plane = 0; plane < sends.size(); ++plane)
                std::fill_n(shared->audio.data() + plane * hybrid::ipc::maxStereoSamples,
                            shared->argument * 2, controllers[sends[plane]] * 16);
        }
        InterlockedExchange(&shared->result, 0);
        SetEvent(responseEvent);
    }
    UnmapViewOfFile(shared);
    return 0;
}
