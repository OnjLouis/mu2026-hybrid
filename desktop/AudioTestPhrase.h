#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
bool muEngineReady(juce::AudioProcessor&);

namespace mu_pilot {
// Pocket piano phrase, using a separate default instance of the synth.
class AudioTestPhrase final : private juce::Timer {
public:
    AudioTestPhrase(juce::AudioDeviceManager& output, std::function<void(bool)> finished)
        : devices(output), onFinished(std::move(finished)) {}
    std::function<void()> onTick;
    ~AudioTestPhrase() override { stop(); }
    bool isPlaying() const { return processor != nullptr; }
    bool start() {
        if (isPlaying() || devices.getCurrentAudioDevice() == nullptr) return false;
        processor.reset(createPluginFilter());
        player.setProcessor(processor.get());
        devices.addAudioCallback(&player);
        bootTime = now();
        playingSince = 0;
        next = 0;
        startTimer(5);
        return true;
    }
    void stop() {
        stopTimer();
        if (!processor) return;
        devices.removeAudioCallback(&player);
        player.setProcessor(nullptr);
        processor.reset();
    }
private:
    static double now() { return juce::Time::getMillisecondCounterHiRes(); }
    void finish(bool ok) {
        stop();
        onFinished(ok);
    }
    void timerCallback() override {
        if (onTick) onTick();
        const auto time = now();
        if (!devices.getCurrentAudioDevice()) { finish(false); return; }
        if (playingSince == 0) {
            if (!muEngineReady(*processor)) {
                if (time - bootTime >= bootTimeoutMs) finish(false);
                return;
            }
            playingSince = time;
        }
        const auto elapsed = time - playingSince;
        while (next < phrase.size() && elapsed >= phrase[next].ms) {
            const auto event = phrase[next++];
            auto midi = event.on ? juce::MidiMessage::noteOn(1, event.note, juce::uint8(88))
                                 : juce::MidiMessage::noteOff(1, event.note);
            midi.setTimeStamp(time * 0.001);
            player.getMidiMessageCollector().addMessageToQueue(midi);
        }
        if (elapsed >= phraseLengthMs) finish(true);
    }
    struct Event { int ms, note; bool on; };
    static constexpr std::array<Event, 8> phrase{{
        {100, 60, true}, {280, 60, false}, {320, 64, true}, {500, 64, false},
        {540, 67, true}, {720, 67, false}, {760, 72, true}, {1150, 72, false}
    }};
    static constexpr int bootTimeoutMs = 8000, phraseLengthMs = 1650;
    juce::AudioDeviceManager& devices;
    std::function<void(bool)> onFinished;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<juce::AudioProcessor> processor;
    double bootTime = 0, playingSince = 0;
    size_t next = 0;
};
}
