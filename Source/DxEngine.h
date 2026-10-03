#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace hybrid {

// Native FM synthesis; recovered voice parameters remain an external asset.
class DxEngine {
public:
    static constexpr unsigned busCount = 16;
    explicit DxEngine(const std::filesystem::path& bank);
    explicit DxEngine(std::span<const std::uint8_t> bank);
    ~DxEngine();
    void setSampleRate(float rate);
    bool queueShort(std::uint32_t packed, std::uint64_t frame);
    void queueSysex(std::span<const std::uint8_t> bytes, std::uint64_t frame);
    void render(float* planar, unsigned stride, unsigned frames);
    unsigned selections() const noexcept;
    unsigned activeVoices() const noexcept;
    bool selected(unsigned channel) const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace hybrid
