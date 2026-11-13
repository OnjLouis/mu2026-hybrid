#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <functional>

namespace mu_pilot {
std::shared_ptr<const std::vector<std::uint8_t>> presetRom();
using Progress = std::function<void(int, int)>;
enum class PresetFormat { nks, vst3 };
// Exports to a dedicated, receipt-guarded child of the chosen folder.
juce::String exportPresets(const juce::File& destination, PresetFormat format,
                           const std::atomic<bool>& cancel, const Progress& progress);
}
