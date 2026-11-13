#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "AccessibleToggleButton.h"
#include "ControlMnemonics.h"
#include "KeyboardChoice.h"
#include "ReviewText.h"

namespace mu_pilot {
class AudioPreferences final : public juce::Component, private juce::ChangeListener {
public:
    explicit AudioPreferences(juce::AudioDeviceManager& manager) : devices(manager) {
        constexpr const char* names[]{"Audio driver", "Output device", "Sample rate", "Audio buffer"};
        for (int i = 0; i < 4; ++i) {
            labels[i].setText(names[i], juce::dontSendNotification);
            labels[i].setAccessible(false);
            auto& field = choices[i];
            field.setTitle(names[i]); field.setName(names[i]); field.setExplicitFocusOrder(i+1);
            field.setWantsKeyboardFocus(true);
            field.onChange = [this,i] { apply(i); };
            addAndMakeVisible(labels[i]); addAndMakeVisible(field);
        }
        refresh.setExplicitFocusOrder(5); panel.setExplicitFocusOrder(6);
        test.setExplicitFocusOrder(7); status.setExplicitFocusOrder(8);
        for (auto* button : {&refresh, &panel, &test}) button->setTitle(button->getButtonText());
        status.setTitle("Audio status");
        constexpr char keys[]{'d', 'o', 's', 'u'};
        for (int i = 0; i < 4; ++i) hs2port::setControlMnemonic(choices[i], keys[i]);
        hs2port::setControlMnemonic(refresh, 'r');
        hs2port::setControlMnemonic(panel, 'n');
        hs2port::setControlMnemonic(test, 't');
        midi.setExplicitFocusOrder(9);
        midi.setFocusContainerType(FocusContainerType::focusContainer);
        midi.setViewedComponent(&inputs, false); midi.setScrollBarsShown(true, false);
        midi.setWantsKeyboardFocus(false);
        midiLabel.setText("MIDI inputs", juce::dontSendNotification); midiLabel.setAccessible(false);
        refresh.onClick = [this] { if (auto* type = devices.getCurrentDeviceTypeObject()) type->scanForDevices(); update(); };
        panel.onClick = [this] { if (auto* device = devices.getCurrentAudioDevice()) device->showControlPanel(); update(); };
        for (auto* item : std::initializer_list<juce::Component*>{&refresh, &panel, &test, &status, &midiLabel, &midi})
            addAndMakeVisible(item);
        devices.addChangeListener(this);
        update();
    }
    ~AudioPreferences() override { devices.removeChangeListener(this); }
    void setPlaying(bool playing) {
        test.setButtonText(playing ? "Stop test phrase" : "Play test phrase");
        test.setTitle(test.getButtonText());
    }
    void report(const juce::String& value) { status.refreshText(value); }
#if JUCE_WINDOWS
    bool handleMnemonic(char key) {
        for (auto& field : choices)
            if (useMnemonic(field, key)) return true;
        for (auto* button : {&refresh, &panel, &test})
            if (useMnemonic(*button, key)) return true;
        if (key != 'm' || midiSwitches.empty() || !midiSwitches.front()->isShowing()) return false;
        midiSwitches.front()->grabKeyboardFocus();
        return true;
    }
#endif
    void resized() override {
        auto area = getLocalBounds().reduced(16);
        for (int i = 0; i < 4; ++i) {
            auto row = area.removeFromTop(42).withTrimmedBottom(8);
            labels[i].setBounds(row.removeFromLeft(146)); choices[i].setBounds(row);
        }
        auto row = area.removeFromTop(42).withTrimmedBottom(8);
        refresh.setBounds(row.removeFromLeft(146)); row.removeFromLeft(8);
        panel.setBounds(row.removeFromLeft(152)); row.removeFromLeft(8);
        test.setBounds(row.removeFromLeft(166));
        status.setBounds(area.removeFromTop(52).withTrimmedBottom(8));
        midiLabel.setBounds(area.removeFromTop(26));
        midi.setBounds(area);
        inputs.setSize(juce::jmax(0, area.getWidth() - midi.getScrollBarThickness()),
                       juce::jmax(area.getHeight(), int(midiSwitches.size()) * midiRowHeight));
        for (size_t i = 0; i < midiSwitches.size(); ++i)
            midiSwitches[i]->setBounds(0, int(i) * midiRowHeight, inputs.getWidth(), midiRowHeight);
    }
    juce::TextButton test{"Play test phrase"};
    std::function<void()> beforeOutputChange;
private:
    static void fill(juce::ComboBox& box, const juce::StringArray& names, int index) {
        juce::StringArray previous;
        for (int i = 0; i < box.getNumItems(); ++i) previous.add(box.getItemText(i));
        if (previous != names) { box.clear(juce::dontSendNotification); box.addItemList(names,1); }
        box.setSelectedItemIndex(index,juce::dontSendNotification);
        box.setEnabled(!names.isEmpty());
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { update(); }
    void update() {
        juce::StringArray types;
        for (auto* t : devices.getAvailableDeviceTypes()) types.add(t->getTypeName());
        fill(choices[0], types, types.indexOf(devices.getCurrentAudioDeviceType()));
        auto* selected = devices.getCurrentDeviceTypeObject();
        const auto setup = devices.getAudioDeviceSetup();
        const auto outputs = selected ? selected->getDeviceNames(false) : juce::StringArray{};
        fill(choices[1], outputs, outputs.indexOf(setup.outputDeviceName));
        rates.clear(); sizes.clear();
        auto* device = devices.getCurrentAudioDevice();
        juce::StringArray rateNames, sizeNames;
        if (device) {
            rates = device->getAvailableSampleRates(); sizes = device->getAvailableBufferSizes();
            for (auto rate : rates) rateNames.add(juce::String(rate,0) + " Hz");
            for (auto size : sizes) sizeNames.add(juce::String(size) + " samples");
        }
        fill(choices[2], rateNames, rates.indexOf(setup.sampleRate));
        fill(choices[3], sizeNames, sizes.indexOf(setup.bufferSize));
        panel.setEnabled(device && device->hasControlPanel());
        test.setEnabled(device != nullptr);
        report(error.isNotEmpty() ? error : device ? "Audio output is active." : "No audio output device is active.");
        const auto available = juce::MidiInput::getAvailableDevices();
        if (available != ports) {
            ports = available;
            midiSwitches.clear();
            for (const auto& port : ports) {
                auto toggle = std::make_unique<scbridge::AccessibleToggleButton>(port.name);
                toggle->setTitle(port.name);
                if (midiSwitches.empty()) hs2port::setControlMnemonic(*toggle, 'm');
                auto* button = toggle.get();
                button->onClick = [this, id = port.identifier, button] {
                    const bool requested = button->getToggleState();
                    devices.setMidiInputDeviceEnabled(id, requested);
                    const bool enabled = devices.isMidiInputDeviceEnabled(id);
                    button->setToggleState(enabled, juce::dontSendNotification);
                    if (requested && !enabled) report("Could not open MIDI input " + button->getTitle() + ".");
                };
                inputs.addAndMakeVisible(*button);
                midiSwitches.push_back(std::move(toggle));
            }
            resized();
        }
        for (int i = 0; i < ports.size(); ++i)
            midiSwitches[size_t(i)]->setToggleState(devices.isMidiInputDeviceEnabled(ports[i].identifier), juce::dontSendNotification);
    }
    void apply(int field) {
        const auto index = choices[field].getSelectedItemIndex();
        if (index < 0) return;
        if (beforeOutputChange) beforeOutputChange();
        const auto previousSetup = devices.getAudioDeviceSetup();
        error.clear();
        if (field == 0) {
            devices.setCurrentAudioDeviceType(choices[0].getText(), true);
            if (auto* type = devices.getCurrentDeviceTypeObject(); !devices.getCurrentAudioDevice() && type && !type->getDeviceNames(false).isEmpty())
                error = "The audio driver could not open its default device. Choose an output device, or close other apps using the interface.";
        } else {
            auto setup = previousSetup;
            if (field == 1) { setup.outputDeviceName = choices[1].getText(); setup.sampleRate = 0; setup.bufferSize = 0; }
            if (field == 2) setup.sampleRate = rates[index];
            if (field == 3) setup.bufferSize = sizes[index];
            error = devices.setAudioDeviceSetup(setup, true);
            if (error.isNotEmpty() && !previousSetup.outputDeviceName.isEmpty())
                devices.setAudioDeviceSetup(previousSetup, true);
        }
        update();
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Audio output", error);
    }
    static constexpr int midiRowHeight = 32;
    juce::AudioDeviceManager& devices;
    juce::Label labels[4], midiLabel;
    hs2port::KeyboardChoice choices[4];
    juce::TextButton refresh{"Refresh devices"}, panel{"Driver settings..."};
    hs2port::ReviewText status;
    struct MidiRows final : juce::Component {
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
            return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::ignored);
        }
    } inputs;
    juce::Viewport midi;
    juce::Array<juce::MidiDeviceInfo> ports;
    std::vector<std::unique_ptr<scbridge::AccessibleToggleButton>> midiSwitches;
    juce::Array<double> rates;
    juce::Array<int> sizes;
    juce::String error;
};
}
