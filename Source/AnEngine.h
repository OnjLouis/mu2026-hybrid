#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace hybrid {

// Native, parameter-compatible approximation; not Yamaha VOP3 chip emulation.
class AnEngine {
public:
    static constexpr unsigned busCount = 16;
    explicit AnEngine(const std::filesystem::path& bank);
    explicit AnEngine(std::span<const std::uint8_t> bank);
    ~AnEngine();
    void setSampleRate(float rate);
    bool queueShort(std::uint32_t packed, std::uint64_t frame);
    void queueSysex(std::span<const std::uint8_t> bytes, std::uint64_t frame);
    void render(float* output, unsigned stride, unsigned frames);
    unsigned selections() const noexcept;
    unsigned activeVoices() const noexcept;
    bool selected(unsigned channel) const noexcept;
    bool heldNote(unsigned channel, unsigned note) const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace hybrid
