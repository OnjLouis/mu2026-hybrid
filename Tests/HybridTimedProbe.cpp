#include "../Source/Vst2Abi.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int maximumBlockSize = 4096;
constexpr int sampleRate = 44100;
constexpr int maxEventsPerBlock = 2048;
int blockSize = maximumBlockSize;

void put32(std::array<std::uint8_t, 44>& header, std::size_t offset,
           std::uint32_t value)
{
    for (int byte = 0; byte < 4; ++byte)
        header[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
}

void put16(std::array<std::uint8_t, 44>& header, std::size_t offset,
           std::uint16_t value)
{
    header[offset] = static_cast<std::uint8_t>(value);
    header[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

struct TimedEvent {
    std::uint64_t frame {};
    char kind {};
    std::vector<char> bytes;
};

struct EventBatch {
    std::int32_t numEvents {};
    vst2::IntPtr reserved {};
    vst2::Event* events[maxEventsPerBlock] {};
};

struct SlowBlock {
    double totalMs {};
    double eventMs {};
    double renderMs {};
    double second {};
    int events {};
};

vst2::IntPtr host(vst2::AEffect*, std::int32_t opcode, std::int32_t,
                  vst2::IntPtr, void*, float)
{
    switch (opcode) {
    case vst2::hostVersion: return 2400;
    case vst2::hostGetSampleRate: return sampleRate;
    case vst2::hostGetBlockSize: return blockSize;
    default: return 0;
    }
}

std::vector<TimedEvent> loadEvents(const char* path)
{
    std::ifstream input(path);
    std::string line;
    if (!std::getline(input, line) || line != "TIMED_MIDI 1")
        throw std::runtime_error("invalid timed MIDI trace");
    std::vector<TimedEvent> result;
    while (std::getline(input, line)) {
        const auto first = line.find(' ');
        if (first == std::string::npos || first + 1 >= line.size())
            throw std::runtime_error("malformed timed MIDI record");
        TimedEvent event;
        event.frame = std::stoull(line.substr(0, first));
        event.kind = line[first + 1];
        if (event.kind == 'E') {
            result.push_back(std::move(event));
            break;
        }
        if ((event.kind != 'M' && event.kind != 'S')
            || first + 2 >= line.size() || line[first + 2] != ' ')
            throw std::runtime_error("invalid timed MIDI event type");
        const auto hex = line.substr(first + 3);
        if (hex.size() % 2 != 0)
            throw std::runtime_error("odd timed MIDI byte count");
        for (std::size_t i = 0; i < hex.size(); i += 2)
            event.bytes.push_back(static_cast<char>(
                std::stoul(hex.substr(i, 2), nullptr, 16)));
        result.push_back(std::move(event));
    }
    if (result.empty() || result.back().kind != 'E')
        throw std::runtime_error("timed MIDI trace lacks end marker");
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 7) {
        std::fprintf(stderr, "usage: HybridTimedProbe <wrapper.dll> <events.txt> [seconds] [block frames] [output.wav] [instances]\n");
        return 2;
    }
    const int instanceCount = argc == 7 ? std::stoi(argv[6]) : 1;
    if (instanceCount < 1 || instanceCount > 3)
        throw std::runtime_error("instance count must be 1 to 3");
    if (argc >= 5) {
        blockSize = std::stoi(argv[4]);
        if (blockSize < 16 || blockSize > maximumBlockSize)
            throw std::runtime_error("block size must be 16 to 4096 frames");
    }
    const auto events = loadEvents(argv[2]);
    const auto stopFrame = argc >= 4
        ? std::min(events.back().frame,
                   static_cast<std::uint64_t>(std::stod(argv[3]) * sampleRate))
        : events.back().frame;
    std::ofstream wave;
    if (argc >= 6 && argv[5][0] != '\0') {
        wave.open(argv[5], std::ios::binary);
        if (!wave)
            throw std::runtime_error("cannot create output WAV");
        const auto dataBytes = static_cast<std::uint32_t>(
            ((stopFrame + blockSize - 1) / blockSize) * blockSize * 8);
        std::array<std::uint8_t, 44> header {};
        std::copy_n("RIFF", 4, header.begin());
        put32(header, 4, dataBytes + 36);
        std::copy_n("WAVEfmt ", 8, header.begin() + 8);
        put32(header, 16, 16);
        put16(header, 20, 3);
        put16(header, 22, 2);
        put32(header, 24, sampleRate);
        put32(header, 28, sampleRate * 8);
        put16(header, 32, 8);
        put16(header, 34, 32);
        std::copy_n("data", 4, header.begin() + 36);
        put32(header, 40, dataBytes);
        wave.write(reinterpret_cast<const char*>(header.data()), header.size());
    }
    HMODULE module = LoadLibraryA(argv[1]);
    if (!module)
        throw std::runtime_error("failed to load wrapper DLL");
    const auto entry = reinterpret_cast<vst2::EntryPoint>(GetProcAddress(module, "main"));
    if (!entry)
        throw std::runtime_error("wrapper has no VST entry point");
    std::vector<vst2::AEffect*> effects;
    std::vector<std::array<float, maximumBlockSize>> channels(instanceCount * 2);
    std::vector<std::array<float*, 2>> outputs(instanceCount);
    for (int instance = 0; instance < instanceCount; ++instance) {
        auto* effect = entry(host);
        if (!effect || effect->magic != vst2::effectMagic)
            throw std::runtime_error("invalid VST effect");
        effect->dispatcher(effect, vst2::open, 0, 0, nullptr, 0.0f);
        effect->dispatcher(effect, vst2::setSampleRate, 0, 0, nullptr,
                           static_cast<float>(sampleRate));
        effect->dispatcher(effect, vst2::setBlockSize, 0, blockSize, nullptr, 0.0f);
        effect->dispatcher(effect, vst2::mainsChanged, 0, 1, nullptr, 0.0f);
        effects.push_back(effect);
        outputs[instance] = { channels[instance * 2].data(),
                              channels[instance * 2 + 1].data() };
    }
    EventBatch batch;
    std::array<vst2::MidiEvent, maxEventsPerBlock> midi;
    std::array<vst2::SysexEvent, maxEventsPerBlock> sysex;
    std::vector<double> blockMs;
    std::vector<SlowBlock> slowBlocks;
    double energy = 0.0;
    float peak = 0.0f;
    std::vector<double> instanceEnergy(instanceCount, 0.0);
    std::vector<float> instancePeak(instanceCount, 0.0f);
    std::size_t index = 0;
    std::size_t sent = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t frame = 0; frame < stopFrame; frame += blockSize) {
        if (std::getenv("HYBRID_PROBE_REALTIME") != nullptr)
            std::this_thread::sleep_until(start + std::chrono::duration_cast<
                std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(double(frame) / sampleRate)));
        batch.numEvents = 0;
        while (index < events.size() && events[index].frame < frame + blockSize) {
            const auto& event = events[index++];
            if (event.kind == 'E')
                break;
            const auto count = batch.numEvents++;
            if (count >= maxEventsPerBlock)
                throw std::runtime_error("too many events in one block");
            const int delta = static_cast<int>(event.frame - frame);
            if (event.kind == 'S') {
                auto& item = sysex[count];
                item.deltaFrames = delta;
                item.dumpBytes = static_cast<std::int32_t>(event.bytes.size());
                item.sysexDump = const_cast<char*>(event.bytes.data());
                batch.events[count] = reinterpret_cast<vst2::Event*>(&item);
            } else {
                auto& item = midi[count];
                item.deltaFrames = delta;
                std::fill(std::begin(item.midiData), std::end(item.midiData), 0);
                std::copy_n(event.bytes.begin(),
                            std::min<std::size_t>(4, event.bytes.size()),
                            item.midiData);
                batch.events[count] = reinterpret_cast<vst2::Event*>(&item);
            }
            ++sent;
        }
        const auto blockStart = std::chrono::steady_clock::now();
        if (batch.numEvents)
            effects[0]->dispatcher(effects[0], vst2::processEvents, 0, 0, &batch, 0.0f);
        const auto renderStart = std::chrono::steady_clock::now();
        for (int instance = 0; instance < instanceCount; ++instance)
            effects[instance]->processReplacing(effects[instance], nullptr,
                                                outputs[instance].data(), blockSize);
        if (wave) {
            for (int sample = 0; sample < blockSize; ++sample) {
                for (int channel = 0; channel < 2; ++channel) {
                    float mixed = 0.0f;
                    for (int instance = 0; instance < instanceCount; ++instance)
                        mixed += channels[instance * 2 + channel][sample];
                    wave.write(reinterpret_cast<const char*>(&mixed), sizeof(float));
                }
            }
        }
        const auto blockEnd = std::chrono::steady_clock::now();
        const auto eventMs = std::chrono::duration<double, std::milli>(
            renderStart - blockStart).count();
        const auto renderMs = std::chrono::duration<double, std::milli>(
            blockEnd - renderStart).count();
        blockMs.push_back(eventMs + renderMs);
        if (eventMs + renderMs > 1000.0 * blockSize / sampleRate)
            slowBlocks.push_back({ eventMs + renderMs, eventMs, renderMs,
                                   double(frame) / sampleRate, batch.numEvents });
        for (int sample = 0; sample < blockSize; ++sample) {
            for (int channel = 0; channel < 2; ++channel) {
                float mixed = 0.0f;
                for (int instance = 0; instance < instanceCount; ++instance) {
                    const float value = channels[instance * 2 + channel][sample];
                    mixed += value;
                    instancePeak[instance] = std::max(instancePeak[instance], std::abs(value));
                    instanceEnergy[instance] += double(value) * value;
                }
                peak = std::max(peak, std::abs(mixed));
                energy += double(mixed) * mixed;
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();
    std::sort(blockMs.begin(), blockMs.end());
    const double deadlineMs = 1000.0 * blockSize / sampleRate;
    const auto overruns = std::count_if(blockMs.begin(), blockMs.end(),
        [deadlineMs](double ms) { return ms > deadlineMs; });
    std::printf("events=%zu blocks=%zu audioSeconds=%.2f wallSeconds=%.2f "
                "peak=%.5f rms=%.5f medianMs=%.3f p95Ms=%.3f maxMs=%.3f "
                "blocksOverDeadline=%zu deadlineMs=%.3f\n",
                sent, blockMs.size(), double(stopFrame) / sampleRate,
                std::chrono::duration<double>(end - start).count(), peak,
                std::sqrt(energy / (2.0 * blockSize * blockMs.size())),
                blockMs[blockMs.size() / 2],
                blockMs[blockMs.size() * 95 / 100], blockMs.back(),
                overruns, deadlineMs);
    for (int instance = 0; instance < instanceCount; ++instance)
        std::printf("instance=%d peak=%.8f rms=%.8f\n", instance,
                    instancePeak[instance],
                    std::sqrt(instanceEnergy[instance]
                              / (2.0 * blockSize * blockMs.size())));
    std::sort(slowBlocks.begin(), slowBlocks.end(),
        [](const SlowBlock& a, const SlowBlock& b) {
            return a.totalMs > b.totalMs;
        });
    for (std::size_t i = 0; i < std::min<std::size_t>(10, slowBlocks.size()); ++i) {
        const auto& block = slowBlocks[i];
        std::printf("slowBlock second=%.3f events=%d totalMs=%.3f "
                    "eventMs=%.3f renderMs=%.3f\n",
                    block.second, block.events, block.totalMs,
                    block.eventMs, block.renderMs);
    }
    for (auto* effect : effects) {
        effect->dispatcher(effect, vst2::mainsChanged, 0, 0, nullptr, 0.0f);
        effect->dispatcher(effect, vst2::close, 0, 0, nullptr, 0.0f);
    }
    FreeLibrary(module);
}
