#pragma once

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <cstdint>
#include <cstring>
#include <span>

namespace mu_pilot {
// The host frame wraps the existing emulator state, not a new engine format.
inline constexpr std::uint32_t compressedStateMagic = 0x3350554f; // OUP3
inline constexpr std::size_t maxEngineStateBytes = 32 * 1024 * 1024;
inline constexpr std::size_t digestBytes = 32;
inline constexpr std::size_t compressedHeaderBytes = sizeof(std::uint32_t) + digestBytes;

inline juce::MemoryBlock compressEngineState(std::span<const std::uint8_t> raw) {
    if (raw.empty() || raw.size() > maxEngineStateBytes) return {};
    juce::MemoryOutputStream framed;
    framed.writeInt(int(raw.size()));
    const auto digest = juce::SHA256(raw.data(), raw.size()).getRawData();
    framed.write(digest.getData(), digest.getSize());
    {
        juce::GZIPCompressorOutputStream stream(framed, 6);
        if (!stream.write(raw.data(), raw.size())) return {};
        stream.flush();
    }
    if (framed.getDataSize() >= raw.size()) return {};
    return framed.getMemoryBlock();
}

inline juce::MemoryBlock expandEngineState(std::span<const std::uint8_t> framed) {
    if (framed.size() <= compressedHeaderBytes || framed.size() > maxEngineStateBytes) return {};
    const auto size = juce::ByteOrder::littleEndianInt(framed.data());
    if (size == 0 || size > maxEngineStateBytes) return {};
    juce::MemoryBlock raw(size);
    juce::MemoryInputStream input(framed.data() + compressedHeaderBytes,
                                  framed.size() - compressedHeaderBytes, false);
    juce::GZIPDecompressorInputStream stream(input);
    auto* output = static_cast<char*>(raw.getData());
    int read = 0;
    while (read < int(size)) {
        const auto n = stream.read(output + read, int(size) - read);
        if (n <= 0) return {};
        read += n;
    }
    char extra{};
    if (stream.read(&extra, 1) != 0) return {};
    const auto digest = juce::SHA256(raw.getData(), raw.getSize()).getRawData();
    if (std::memcmp(digest.getData(), framed.data() + sizeof(std::uint32_t), digestBytes) != 0) return {};
    return raw;
}
}
