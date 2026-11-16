#include <juce_audio_utils/juce_audio_utils.h>
#include "KeyboardChoice.h"
#include "ControlHelp.h"
#include "ControlMnemonics.h"
#include "ReviewText.h"
#include "ProgramList.h"
#include "ControlCatalog.h"
#include "PresetAddress.h"
#include "PresetExport.h"
#include "StateCompression.h"
#include "NumericSlider.h"
#include "vst3/engine.h"
#include "xg/voices.h"
#include "xg/fx_types.h"
#include "ui/xg_ui.h"
#include "rom_search.h"
#include <array>
#include <chrono>
#include <unordered_set>

namespace {
using namespace juce;
using hs2port::setControlHelp;
using Engine = smu2000::vst3::engine;
constexpr auto product = "MU2000";
constexpr int partsPerPort = 16;
constexpr int portCount = 4;
constexpr int midiRange = 128;
constexpr int bankMsbOffset = 1, bankLsbOffset = 2, programOffset = 3;
constexpr std::array<uint8_t, 9> xgReset{0xf0, 0x43, 0x10, 0x4c, 0, 0, 0x7e, 0, 0xf7};
constexpr auto& partControls = mu_pilot::controls;
std::mutex effectCatalogueLock;
bool effectCatalogueLoaded = false;

String controlText(const xg::param& p, int value) {
    if (p.how == xg::view::part_off) {
        if (value == mu_pilot::routingOff) return "Off";
        if (value >= portCount * partsPerPort) return "A/D " + String(value - portCount * partsPerPort + 1);
        return "Port " + String::charToString(juce_wchar('A' + value / partsPerPort))
            + ", Part " + String(value % partsPerPort + 1);
    }
    if (mu_pilot::effectType(p)) {
        const std::lock_guard guard(effectCatalogueLock);
        const auto& types = p.hi == 2 && p.lo == 0 ? xg::rev_types()
            : p.hi == 2 && p.lo == 0x20 ? xg::cho_types() : xg::ins_types();
        for (const auto& type : types)
            if ((int(type.msb) * 128 + type.lsb) == value) return type.name;
        return "Type " + String(value);
    }
    if (p.how == xg::view::center) {
        const auto difference = value - p.center;
        return difference < 0 ? "minus " + String(-difference) : difference > 0 ? "plus " + String(difference) : "0";
    }
    if (p.how == xg::view::pan) {
        if (!value) return "Random";
        if (value == 64) return "Centre";
        return String(value < 64 ? "Left " : "Right ") + String(std::abs(value - 64));
    }
    return xg::format(p, value);
}

String effectValueText(const xg::fx_param& p, int value) {
    if (p.fmt == xg::fx_fmt::table && value >= p.lo && value <= p.hi) {
        const String text(p.texts[value - p.lo]);
        if (text.startsWithChar('+')) return "plus " + text.substring(1);
        if (text.startsWithChar('-')) return "minus " + text.substring(1);
        return text;
    }
    if (p.fmt == xg::fx_fmt::tenths) return String(value / 10.0, 1);
    return String(value);
}

class PartParameter final : public AudioParameterInt {
public:
    PartParameter(const mu_pilot::Control& c, const xg::param& p)
        : AudioParameterInt({c.id, 1}, p.where == xg::area::part && String(c.page) == "Mixer"
                            ? String(c.label) : String(c.page) + " " + c.label,
              p.min, p.max, p.def), definition(p), isEffectSlot(mu_pilot::effectSlot(c)) {}
    String getText(float normalised, int length) const override {
        const auto value = roundToInt(convertFrom0to1(normalised));
        const auto* effect = currentEffect.load(std::memory_order_relaxed);
        const auto text = isEffectSlot
            ? (effect ? effectValueText(*effect, mu_pilot::decodeEffectValue(*effect, definition.max, value)) : String("Unused"))
                                        : controlText(definition, value);
        return length > 0 ? text.substring(0, length) : text;
    }
    void setEffect(const xg::fx_param* effect) {
        if (currentEffect.exchange(effect, std::memory_order_relaxed) != effect)
            cacheChanged.store(true, std::memory_order_relaxed);
    }
    void refresh(int value) {
        const int observed = pending.load(std::memory_order_relaxed);
        if (observed < 0) static_cast<AudioProcessorParameter&>(*this).setValue(convertTo0to1(float(value)));
    }
    std::atomic<int> pending{-1};
private:
    const xg::param& definition;
    const bool isEffectSlot;
    std::atomic<const xg::fx_param*> currentEffect{nullptr};
    void valueChanged(int value) override {
        if (!refreshing) pending.store(value, std::memory_order_relaxed);
    }
public:
    void refreshFromEngine(int value) {
        const ScopedValueSetter<bool> guard(refreshing, true);
        if (get() == value || pending.load(std::memory_order_relaxed) >= 0) return;
        refresh(value);
        cacheChanged.store(true, std::memory_order_relaxed);
    }
    void notifyReadback() {
        if (cacheChanged.exchange(false, std::memory_order_relaxed))
            sendValueChangedMessageToListeners(convertTo0to1(float(get())));
    }
private:
    inline static thread_local bool refreshing = false;
    std::atomic<bool> cacheChanged{false};
};

// The engine otherwise calls the upstream graphical editor to publish ROM names.
std::shared_ptr<const std::vector<u8>> voiceData;

class Processor final : public AudioProcessor, private Timer {
public:
    Processor() : AudioProcessor(BusesProperties().withOutput("Stereo", AudioChannelSet::stereo(), true)) {
        for (size_t i = 0; i < partControls.size(); ++i) {
            parameters[i] = new PartParameter(partControls[i], definitions[i]);
            addParameter(parameters[i]);
        }
        engine.start();
        startTimerHz(15);
    }
    ~Processor() override { stopTimer(); }
    const String getName() const override { return product; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == AudioChannelSet::stereo(); }
    void prepareToPlay(double rate, int) override {
        rateHz = rate;
        engine.set_output_rate(rate);
        setLatencySamples(int(engine.latency_samples()));
        engine.set_processing(true);
    }
    void releaseResources() override { engine.set_processing(false); }
    void processBlock(AudioBuffer<float>& audio, MidiBuffer& midi) override {
        const auto started = std::chrono::steady_clock::now();
        const auto n = audio.getNumSamples();
        if (engine.state() == smu2000::vst3::status::ready) {
            const int note = parameters[mu_pilot::drumNoteIndex]->pending.exchange(-1, std::memory_order_relaxed);
            if (mu_pilot::validDrumNote(note)) drumNote.store(note, std::memory_order_relaxed);
            for (size_t i = 0; i < parameters.size(); ++i) {
                int value = parameters[i]->pending.exchange(-1, std::memory_order_relaxed);
                if (value < 0) continue;
                const auto& c = partControls[i];
                const auto& p = definitions[i];
                if (mu_pilot::effectType(p))
                    requestedTypes[p.hi == 3 ? 3 + p.mid : p.lo / 0x20] = {value, int(rateHz / 8)};
                const int overrideType = mu_pilot::effectSlot(c)
                    && parameterSnapshot.serial ? requestedType(c) : -1;
                if (mu_pilot::effectSlot(c)) {
                    const auto* effect = mu_pilot::effectBinding(c, parameterSnapshot, overrideType).parameter;
                    if (!effect) continue;
                    value = mu_pilot::decodeEffectValue(*effect, p.max, value);
                }
                const auto message = mu_pilot::writeControl(c, 0, drumNote.load(std::memory_order_relaxed),
                                                           parameterSnapshot, value, overrideType);
                if (i == 0 || i == 1) {
                    const uint8_t cc[]{0xb0, uint8_t(i == 0 ? 7 : 10), uint8_t(value)};
                    engine.midi(cc, sizeof cc);
                } else if (message.size) {
                    engine.midi(message.data.data(), message.size);
                }
                readbackDelay[i] = int(rateHz / 8);
            }
        }
        auto* left = audio.getWritePointer(0);
        auto* right = audio.getWritePointer(1);
        int cursor = 0;
        for (const auto event : midi) {
            const int offset = jlimit(cursor, n, event.samplePosition);
            if (offset > cursor) engine.fill(left + cursor, right + cursor, offset - cursor);
            engine.midi(event.data, size_t(event.numBytes), 0);
            cursor = offset;
        }
        if (n > cursor) engine.fill(left + cursor, right + cursor, n - cursor);
        if (engine.state() == smu2000::vst3::status::ready) {
            engine.panel().read_xg(parameterSnapshot);
            for (auto& request : requestedTypes) request.wait = jmax(0, request.wait - n);
            const bool fresh = parameterSnapshot.serial != lastParameterSerial;
            lastParameterSerial = parameterSnapshot.serial;
            const int note = drumNote.load(std::memory_order_relaxed);
            if (parameterSnapshot.serial)
                for (size_t i = 0; i < parameters.size(); ++i) {
                    const auto& c = partControls[i];
                    const bool delayEnded = readbackDelay[i] > 0 && readbackDelay[i] <= n;
                    readbackDelay[i] = jmax(0, readbackDelay[i] - n);
                    if (!(fresh || delayEnded || (mu_pilot::drumControl(c) && note != lastReadNote))) continue;
                    int value = 0;
                    const auto* effect = mu_pilot::effectSlot(c)
                        ? mu_pilot::effectBinding(c, parameterSnapshot).parameter : nullptr;
                    if (mu_pilot::effectSlot(c)) parameters[i]->setEffect(effect);
                    if (!readbackDelay[i] && mu_pilot::readControl(c, 0, note, parameterSnapshot, value)) {
                        if (effect) value = mu_pilot::encodeEffectValue(*effect, definitions[i].max, value);
                        parameters[i]->refreshFromEngine(value);
                    }
                }
            lastReadNote = note;
        }
        midi.clear();
        if (n > 0) {
            lastBlock.store(n, std::memory_order_relaxed);
            const auto fraction = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() * rateHz / n;
            if (fraction > 1.0) lateBlocks.fetch_add(1, std::memory_order_relaxed);
            if (fraction > peakLoad.load(std::memory_order_relaxed)) peakLoad.store(fraction, std::memory_order_relaxed);
            recentLoad.store(fraction, std::memory_order_relaxed);
        }
    }
    bool hasEditor() const override { return true; }
    AudioProcessorEditor* createEditor() override;
    void getStateInformation(MemoryBlock& data) override {
        const auto state = engine.save_state();
        if (state.empty()) {
            const auto word = pendingAddress.load(std::memory_order_relaxed);
            if (word >= 0) {
                const auto bytes = mu_pilot::PresetAddress{uint8_t(word >> 16), uint8_t(word >> 8), uint8_t(word)}.encode();
                data.replaceAll(bytes.data(), bytes.size());
                return;
            }
        }
        const auto compressed = mu_pilot::compressEngineState(state);
        MemoryOutputStream out(data, false);
        out.writeInt(compressed.isEmpty() ? stateMagicV2 : mu_pilot::compressedStateMagic);
        out.writeInt(int(parameters.size()));
        for (const auto* parameter : parameters) out.writeShort(short(parameter->get()));
        if (compressed.isEmpty()) out.write(state.data(), state.size());
        else out.write(compressed.getData(), compressed.getSize());
    }
    void setStateInformation(const void* data, int size) override {
        if (size <= 0) return;
        auto* bytes = static_cast<const uint8_t*>(data);
        MemoryBlock expanded;
        if (size >= 4 && ByteOrder::littleEndianInt(bytes) == mu_pilot::compressedStateMagic) {
            if (size < 8) return;
            const auto count = ByteOrder::littleEndianInt(bytes + 4);
            if (count > uint32_t((size - 8) / 2)) return;
            const auto offset = size_t(8 + count * 2);
            expanded = mu_pilot::expandEngineState({bytes + offset, size_t(size) - offset});
            if (expanded.isEmpty()) return;
        }
        constexpr int legacyCount = 14, headerSize = 4 + legacyCount;
        for (auto* parameter : parameters) parameter->pending.store(-1, std::memory_order_relaxed);
        mu_pilot::PresetAddress address{};
        if (mu_pilot::PresetAddress::decode({bytes, size_t(size)}, address)) {
            drumNote.store(partControls[mu_pilot::drumNoteIndex].initial, std::memory_order_relaxed);
            pendingAddress.store((int(address.msb) << 16) | (int(address.lsb) << 8) | address.program, std::memory_order_relaxed);
            const auto setup = address.setup();
            engine.load_state(bytes, 0, setup.data(), setup.size());
            return;
        }
        pendingAddress.store(-1, std::memory_order_relaxed);
        if (size >= headerSize && ByteOrder::littleEndianInt(bytes) == stateMagic) {
            for (size_t i = 0; i < legacyCount; ++i) parameters[i]->refreshFromEngine(bytes[4 + i] & 127);
            bytes += headerSize;
            size -= headerSize;
        } else if (size >= 8 && (ByteOrder::littleEndianInt(bytes) == stateMagicV2 || !expanded.isEmpty())) {
            const auto count = ByteOrder::littleEndianInt(bytes + 4);
            if (count > uint32_t((size - 8) / 2)) return;
            for (size_t i = 0; i < jmin(size_t(count), parameters.size()); ++i) {
                const auto value = ByteOrder::littleEndianShort(bytes + 8 + i * 2);
                if (!xg::valid(definitions[i], value)) continue;
                if (i == mu_pilot::drumNoteIndex) drumNote.store(value, std::memory_order_relaxed);
                parameters[i]->refreshFromEngine(value);
            }
            bytes += 8 + count * 2;
            size -= int(8 + count * 2);
        }
        if (!expanded.isEmpty()) engine.load_state(static_cast<const uint8_t*>(expanded.getData()), expanded.getSize());
        else if (size > 0) engine.load_state(bytes, size_t(size));
    }
    bool send(int part, std::initializer_list<uint8_t> bytes) {
        return engine.panel().send_port(part / partsPerPort, bytes.begin(), bytes.size());
    }
    bool select(int part, int msb, int lsb, int program) {
        const auto ch = uint8_t(part % partsPerPort);
        const auto cc = uint8_t(0xb0 | ch);
        return send(part, {cc, 0, uint8_t(msb), cc, 32, uint8_t(lsb), uint8_t(0xc0 | ch), uint8_t(program)});
    }
    bool control(int part, int number, int value) {
        return send(part, {uint8_t(0xb0 | (part % partsPerPort)), uint8_t(number), uint8_t(value)});
    }
    bool editPart(int part, size_t index, int value, const ui::xg_snapshot& snapshot, int note) {
        const auto& c = partControls[index];
        const auto& p = definitions[index];
        if (mu_pilot::effectSlot(c)) {
            const auto* effect = mu_pilot::effectBinding(c, snapshot).parameter;
            if (!effect) return false;
            value = mu_pilot::encodeEffectValue(*effect, p.max, value);
        }
        if (part == 0 || (!mu_pilot::drumControl(c) && p.where != xg::area::part)) {
            auto* parameter = parameters[index];
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(float(value)));
            parameter->endChangeGesture();
            return true;
        }
        const auto message = mu_pilot::writeControl(c, part, note, snapshot, value);
        return message.size && engine.panel().send_port(0, message.data.data(), message.size);
    }
    int selectedDrumNote() const {
        const auto pending = parameters[mu_pilot::drumNoteIndex]->pending.load(std::memory_order_relaxed);
        return mu_pilot::validDrumNote(pending) ? pending : drumNote.load(std::memory_order_relaxed);
    }
    bool resetXg() { return engine.panel().send(xgReset.data(), xgReset.size()); }
    std::function<void()> audioSettings;
    Engine engine;
    std::atomic<int> lastBlock{0};
    std::atomic<unsigned> lateBlocks{0};
    std::atomic<double> peakLoad{0}, recentLoad{0};
private:
    int requestedType(const mu_pilot::Control& control) const {
        const auto& request = requestedTypes[int(control.target) - int(mu_pilot::Target::reverb)];
        return request.wait ? request.type : -1;
    }
    void timerCallback() override { for (auto* parameter : parameters) parameter->notifyReadback(); }
    static constexpr uint32_t stateMagic = 0x3150554f; // OUP1, followed by Part 1 values and the original engine state.
    static constexpr uint32_t stateMagicV2 = 0x3250554f;
    decltype(mu_pilot::definitions()) definitions = mu_pilot::definitions();
    std::array<PartParameter*, partControls.size()> parameters{};
    std::array<int, partControls.size()> readbackDelay{};
    struct RequestedType { int type = -1, wait = 0; };
    std::array<RequestedType, 7> requestedTypes{};
    std::atomic<int> pendingAddress{-1};
    std::atomic<int> drumNote{partControls[mu_pilot::drumNoteIndex].initial};
    ui::xg_snapshot parameterSnapshot{};
    uint64_t lastParameterSerial = 0;
    int lastReadNote = -1;
    double rateHz = 44100.0;
};

class Editor final : public AudioProcessorEditor, private Timer, private KeyListener {
public:
    explicit Editor(Processor& p) : AudioProcessorEditor(p), processor(p) {
        configure(part, "Part", "Select one of the four MIDI ports and 16 parts per port.");
        for (int i = 0; i < portCount * partsPerPort; ++i) part.addItem(partPrefix(i), i + 1);
        part.setSelectedId(1, dontSendNotification);
        part.onChange = [this] { lastSerial = 0; updatePart(true); };
        configure(msb, "Bank MSB", "Choose the coarse MIDI bank. The firmware may ignore unsupported banks.");
        configure(lsb, "Bank LSB", "Choose the fine MIDI bank. The firmware may ignore unsupported banks.");
        configure(voice, "Current sound", "Use arrows or Page Up and Down to change sound; Enter opens the sound list.");
        configure(variation, "Variation", "Browse named bank variations of the current program. This also shows drum and sound-effect kits for that program when available.");
        for (int i = 0; i < midiRange; ++i) {
            msb.addItem(String(i), i + 1);
            lsb.addItem(String(i), i + 1);
        }
        msb.onChange = lsb.onChange = [this] { rebuildVoices(); sendVoice(); rebuildVariations(); };
        voice.onChange = [this] { sendVoice(); rebuildVariations(); };
        variation.onChange = [this] {
            const auto index = variation.getSelectedItemIndex();
            if (index < 0 || size_t(index) >= variations.size()) return;
            const auto [a, b] = variations[size_t(index)];
            msb.setSelectedId(a + 1, dontSendNotification);
            lsb.setSelectedId(b + 1, dontSendNotification);
            rebuildVoices();
            sendVoice();
        };
        configure(controlPage, "Control page", "Choose a group of controls. Host automation addresses Port A, Part 1 and global settings.");
        for (const auto& control : partControls)
            if (!pageNames.contains(control.page)) {
                pageNames.add(control.page);
                controlPage.addItem(control.page, pageNames.size());
            }
        controlPage.setSelectedId(1, dontSendNotification);
        controlPage.onChange = [this] { showControlPage(); };
        for (size_t i = 0; i < sliders.size(); ++i) {
            auto& slider = sliders[i];
            const auto& p = mu_pilot::definitions()[i];
            const auto& c = partControls[i];
            configure(slider, c.label, c.target == mu_pilot::Target::drumNote
                ? "Choose the MIDI note to edit in the numbered drum setup used by this part."
                : c.target == mu_pilot::Target::drum
                ? "Edits the selected note. Choose a numbered drum setup in Routing if the part is in plain Drum mode."
                : mu_pilot::effectSlot(c) ? "The active effect algorithm determines this parameter and its range."
                : p.how == xg::view::part_off ? "Choose which part receives the insertion effect, or Off."
                : p.where == xg::area::part
                ? "Adjust this setting for the selected part." : "This setting affects the whole instrument.");
            slider.setName(partControls[i].label);
            slider.setWantsKeyboardFocus(true);
            slider.setRange(p.min, p.max, 1);
            slider.setSliderStyle(Slider::LinearHorizontal);
            slider.textFromValueFunction = [this, i, &p](double value) {
                if (const auto* effect = effectDetails[i]) return effectValueText(*effect, roundToInt(value));
                return controlText(p, roundToInt(value));
            };
            slider.valueFromTextFunction = [this, i, &p](const String& text) {
                if (const auto* effect = effectDetails[i]) {
                    if (effect->fmt == xg::fx_fmt::tenths) return double(roundToInt(text.getDoubleValue() * 10.0));
                    for (int value = effect->lo; effect->fmt == xg::fx_fmt::table && value <= effect->hi; ++value)
                        if (effectValueText(*effect, value).equalsIgnoreCase(text.trim())) return double(value);
                    return double(text.getIntValue());
                }
                for (int value = p.min; value <= p.max; ++value)
                    if (controlText(p, value).equalsIgnoreCase(text.trim())) return double(value);
                return double(text.getIntValue() + (p.how == xg::view::center ? p.center : 0));
            };
            slider.configureValueField(112);
            slider.onValueChange = [this, i] {
                const int value = roundToInt(sliders[i].getValue());
                if (i == mu_pilot::drumNoteIndex && selected() != 0) {
                    localDrumNote = value;
                } else {
                    processor.editPart(selected(), i, value, snapshot, currentDrumNote());
                }
                if (i == mu_pilot::drumNoteIndex) updatePart(false);
                holdUntil = Time::getMillisecondCounterHiRes() + 300.0;
            };
            if (mu_pilot::effectType(p)) {
                auto combo = std::make_unique<hs2port::KeyboardChoice>();
                configure(*combo, "Type", "Select the effect algorithm. Changing type resets its parameters.");
                combo->onChange = [this, i] {
                    if (const auto id = effectChoices[i]->getSelectedId(); id > 0) {
                        processor.editPart(selected(), i, id - 1, snapshot, currentDrumNote());
                        holdUntil = Time::getMillisecondCounterHiRes() + 300.0;
                    }
                };
                effectChoices[i] = std::move(combo);
            }
        }
        configure(panic, "All Notes Off", "Stop notes on the selected part. F9 also stops notes.");
        configure(reset, "XG Reset", "Reset the whole instrument to XG defaults, including its loaded sounds.");
        configure(help, "Keyboard help", "F1 opens help for the focused control.");
        panic.onClick = [this] { processor.control(selected(), 64, 0); processor.control(selected(), 120, 0); processor.control(selected(), 123, 0); };
        reset.onClick = [this] {
            auto safe = Component::SafePointer<Editor>(this);
            NativeMessageBox::showForOptionsAsync(MessageBoxOptions().withTitle("Reset to XG defaults?")
                .withMessage("This changes the current sounds and mixer settings on all parts.")
                .withButton(MessageBoxButtonOptions{"Cancel", 0}.withRole(MessageBoxButtonRole::cancel).withPreferredAction())
                .withButton("Reset", 1).withAssociatedComponent(this), [safe](int result) {
                    if (safe && result == 1) safe->processor.resetXg();
                });
        };
        help.onClick = [this] { showHelp(nullptr); };
        if (processor.audioSettings) {
            configure(settings, "Audio and MIDI Settings", "Choose the audio output and physical MIDI inputs.");
            settings.onClick = [this] { processor.audioSettings(); };
        }
        configure(status, "Engine status", "Reports block size and render deadlines. Control+S refreshes and focuses this readout.");
        for (auto [control, letter] : std::initializer_list<std::pair<Component*, char>>{
                 {&part, 'i'}, {&msb, 'b'}, {&lsb, 'l'}, {&voice, 'w'}, {&variation, 'r'}, {&controlPage, 'c'}, {&reset, 'x'}})
            hs2port::setControlMnemonic(*control, letter);
        hs2port::setControlAccelerator(status, "Ctrl+S");
        hs2port::setControlAccelerator(help, "F1");
        hs2port::setControlAccelerator(panic, "F9");
        hs2port::setControlAccelerator(settings, "Ctrl+,");
        review.setTitle("Keyboard help text");
        review.addKeyListener(this);
        addChildComponent(review);
        updatePart(true);
        setSize(680, 710);
        showControlPage();
        startTimerHz(8);
    }
    void visibilityChanged() override {
        if (isShowing() && getCurrentlyFocusedComponent() == nullptr) part.grabKeyboardFocus();
    }
    void resized() override {
        auto bounds = getLocalBounds().reduced(14);
        review.setBounds(bounds);
        for (size_t i = 0; i < controls.size(); ++i) {
            auto* c = controls[i];
            if (!c->isVisible()) continue;
            auto row = bounds.removeFromTop(34);
            labels[i]->setBounds(row.removeFromLeft(175));
            c->setBounds(row.reduced(0, 3));
        }
    }
#if JUCE_WINDOWS
    bool handleMnemonic(char key) {
        return std::any_of(controls.begin(), controls.end(), [key](auto* control) {
            return mu_pilot::useMnemonic(*control, key);
        });
    }
#endif
private:
    void showControlPage() {
        for (size_t i = 0; i < sliders.size(); ++i) {
            auto& slider = sliders[i];
            const bool visible = controlPage.getText() == partControls[i].page;
            const auto index = size_t(std::find(controls.begin(), controls.end(), &slider) - controls.begin());
            slider.setVisible(visible && !effectChoices[i]);
            labels[index]->setVisible(slider.isVisible());
            if (auto* combo = effectChoices[i].get()) {
                combo->setVisible(visible);
                const auto slot = size_t(std::find(controls.begin(), controls.end(), combo) - controls.begin());
                labels[slot]->setVisible(visible);
            }
        }
        resized();
    }
    void configure(Component& component, const String& title, const String& info) {
        auto label = std::make_unique<Label>();
        label->setText(title, dontSendNotification);
        label->setAccessible(false);
        addAndMakeVisible(*label);
        labels.push_back(std::move(label));
        controls.push_back(&component);
        addAndMakeVisible(component);
        component.setTitle(title);
        component.setExplicitFocusOrder(int(controls.size()));
        component.addKeyListener(this);
        setControlHelp(component, info);
    }
    int selected() const { return jmax(0, part.getSelectedItemIndex()); }
    int currentDrumNote() const { return selected() == 0 ? processor.selectedDrumNote() : localDrumNote; }
    static String partPrefix(int index) {
        return "Port " + String::charToString(juce_wchar('A' + index / partsPerPort))
            + ", Part " + String(index % partsPerPort + 1);
    }
    String name(int a, int b, int pc, const u8* ram = nullptr) const {
        if (rom) {
            auto n = ram ? rom->name(ram, a, pc) : std::string{};
            if (n.empty()) n = a == 126 || a == 127 ? rom->kit_name(a, pc)
                : rom->record_name(rom->lookup(snapshot.voice_mode, snapshot.voice_set, a, b, pc));
            if (!n.empty()) return String::fromUTF8(n.c_str());
        }
        return "Program " + String(pc + 1);
    }
    void rebuildVoices() {
        const auto prior = jmax(0, currentProgram());
        const auto a = msb.getSelectedItemIndex(), b = lsb.getSelectedItemIndex();
        const auto list = mu_pilot::programsFor([&](int pc) { return catalogueName(a, b, pc); });
        voice.clear(dontSendNotification);
        for (const auto& [pc, kit] : list)
            voice.addItem(String(pc + 1) + ": " + (kit.empty() ? name(a, b, pc) : String::fromUTF8(kit.c_str())), pc + 1);
        voice.setSelectedId(mu_pilot::atOrAfter(list, prior) + 1, dontSendNotification);
    }
    std::optional<std::string> catalogueName(int a, int b, int pc) const {
        if (!rom || !rom->ok() || a < 0 || b < 0) return std::nullopt;
        if (mu_pilot::isKitBank(a)) return rom->kit_name(a, pc);
        const auto record = rom->lookup(snapshot.voice_mode, snapshot.voice_set, a, b, pc);
        if (!record) return std::nullopt; // Firmware-specific and user/sample addresses can lack a ROM record.
        auto label = rom->record_name(record);
        if (label == "Silence") label.clear();
        return label;
    }
    int currentProgram() const { return voice.getSelectedId() - 1; }
    void rebuildVariations() {
        const int pc = currentProgram();
        if (pc < 0) return;
        const int key = (int(snapshot.voice_mode) << 16) | (int(snapshot.voice_set) << 8) | pc;
        if (rom && key != variationKey) {
            variations.clear();
            variation.clear(dontSendNotification);
            std::unordered_set<u32> seen;
            const auto add = [&](int a, int b, const std::string& label) {
                if (label.empty() || label == "Silence") return;
                variations.emplace_back(a, b);
                variation.addItem(String::fromUTF8(label.c_str()) + " (bank " + String(a) + ":" + String(b) + ")",
                                  int(variations.size()));
            };
            for (int a = 0; a < 126; ++a)
                for (int b = 0; b < midiRange; ++b) {
                    if (!rom->lsb_ok(snapshot.voice_mode, snapshot.voice_set, a, b)) continue;
                    const auto rec = rom->lookup(snapshot.voice_mode, snapshot.voice_set, a, b, pc);
                    if (rec && seen.insert(rec).second) add(a, b, rom->record_name(rec));
                }
            for (int a : {126, 127}) add(a, 0, rom->kit_name(a, pc));
            variationKey = key;
        }
        const auto current = std::pair{msb.getSelectedItemIndex(), lsb.getSelectedItemIndex()};
        const auto item = std::find(variations.begin(), variations.end(), current);
        if (item == variations.end()) variation.setText("Select a named variation", dontSendNotification);
        else variation.setSelectedItemIndex(int(item - variations.begin()), dontSendNotification);
    }
    void sendVoice() {
        const auto pc = currentProgram();
        if (pc < 0) return;
        processor.select(selected(), jmax(0, msb.getSelectedItemIndex()), jmax(0, lsb.getSelectedItemIndex()),
                         pc);
        holdUntil = Time::getMillisecondCounterHiRes() + 300.0;
    }
    void updatePart(bool force) {
        const auto* row = snapshot.parts[selected()];
        const int a = row[bankMsbOffset] & 127, b = row[bankLsbOffset] & 127;
        const int modeKey = (int(snapshot.voice_mode) << 8) | int(snapshot.voice_set);
        if (force || voiceModeKey != modeKey || a + 1 != msb.getSelectedId() || b + 1 != lsb.getSelectedId()) {
            msb.setSelectedId(a + 1, dontSendNotification);
            lsb.setSelectedId(b + 1, dontSendNotification);
            rebuildVoices();
            voiceModeKey = modeKey;
        }
        const int pc = row[programOffset] & 127;
        if (const auto label = catalogueName(a, b, pc); label && label->empty())
            voice.setText(String(pc + 1) + ": Unassigned", dontSendNotification);
        else
            voice.setSelectedId(pc + 1, dontSendNotification);
        rebuildVariations();
        for (size_t i = 0; i < sliders.size(); ++i) {
            int value = 0;
            const auto& p = mu_pilot::definitions()[i];
            const auto& c = partControls[i];
            if (mu_pilot::effectSlot(c)) {
                const auto* effect = mu_pilot::effectBinding(c, snapshot).parameter;
                if (effect != effectDetails[i] && !sliders[i].isEnteringValue()) {
                    effectDetails[i] = effect;
                    const String title = effect ? String::fromUTF8(effect->label) : String(c.label) + " (unused)";
                    sliders[i].setName(title);
                    sliders[i].setTitle(title);
                    const auto rowIndex = size_t(std::find(controls.begin(), controls.end(), &sliders[i]) - controls.begin());
                    labels[rowIndex]->setText(title, dontSendNotification);
                    if (effect && effect->lo < effect->hi) sliders[i].setRange(effect->lo, effect->hi, 1);
                }
                sliders[i].setEnabled(effectDetails[i] && effectDetails[i]->lo < effectDetails[i]->hi);
            }
            if (c.target == mu_pilot::Target::drum)
                sliders[i].setEnabled(mu_pilot::drumSetup(snapshot, selected()) >= 0);
            if (!mu_pilot::readControl(c, selected(), currentDrumNote(), snapshot, value)) continue;
            if (auto* combo = effectChoices[i].get()) {
                if (!combo->getNumItems()) {
                    const std::lock_guard guard(effectCatalogueLock);
                    const auto& types = p.hi == 2 && p.lo == 0 ? xg::rev_types()
                        : p.hi == 2 && p.lo == 0x20 ? xg::cho_types() : xg::ins_types();
                    for (const auto& type : types) {
                        const auto id = int(type.msb) * 128 + type.lsb + 1;
                        if (combo->indexOfItemId(id) < 0) combo->addItem(type.name, id);
                    }
                }
                if (combo->indexOfItemId(value + 1) >= 0) combo->setSelectedId(value + 1, dontSendNotification);
                else combo->setText(controlText(p, value), dontSendNotification);
            } else if (!sliders[i].isEnteringValue())
                sliders[i].setValue(value, dontSendNotification);
        }
    }
    void refreshStatus() {
        const auto state = processor.engine.state();
        status.refreshText(state == smu2000::vst3::status::ready
            ? "Ready. Block " + String(processor.lastBlock.load()) + " samples. Late blocks "
                + String(processor.lateBlocks.load()) + ". Render load " + String(int(processor.recentLoad.load() * 100))
                + " percent; peak " + String(int(processor.peakLoad.load() * 100)) + " percent."
            : state == smu2000::vst3::status::failed ? "Engine not ready. Check the MU2000 ROM folder."
                                                  : "Starting the MU2000 engine...");
    }
    void timerCallback() override {
        if (!status.hasKeyboardFocus(true)) refreshStatus();
        const auto state = processor.engine.state();
        if (state != smu2000::vst3::status::ready || Time::getMillisecondCounterHiRes() < holdUntil) return;
        processor.engine.panel().read_xg(snapshot);
        const auto data = !rom ? mu_pilot::presetRom() : nullptr;
        const bool gotRom = data != nullptr;
        if (gotRom) {
            rom = std::make_unique<xg::voice_rom>(data);
            for (auto& choice : effectChoices) if (choice) choice->clear(dontSendNotification);
        }
        if (!gotRom && lastSerial == snapshot.serial) return;
        lastSerial = snapshot.serial;
        for (int i = 0; i < portCount * partsPerPort; ++i) {
            const auto* row = snapshot.parts[i];
            const auto text = partPrefix(i) + ": " + name(row[bankMsbOffset] & 127, row[bankLsbOffset] & 127,
                                                           row[programOffset] & 127, row);
            if (part.getItemText(i) != text) part.changeItemText(i + 1, text);
        }
        updatePart(gotRom);
    }
    void showHelp(Component* focused) {
        returnFocus = focused;
        const auto info = hs2port::controlHelp(focused, *this);
        review.refreshText((info.isNotEmpty() ? info : String("Tab moves between controls. Arrows and Page Up or Down change choices. Enter opens the list. F9 stops notes on the selected part."))
                           + "\n\nUse arrows to review, Escape to close.");
        review.setCaretPosition(0);
        for (auto* c : controls) c->setVisible(false);
        for (auto& l : labels) l->setVisible(false);
        review.setVisible(true);
        review.grabKeyboardFocus();
    }
    bool keyPressed(const KeyPress& key, Component* source) override {
#if JUCE_WINDOWS
        if (const auto letter = mu_pilot::controlMnemonic(key); letter && handleMnemonic(letter)) return true;
#endif
        if (key == KeyPress::F1Key && !review.isVisible()) { showHelp(source); return true; }
        if (key == KeyPress::F9Key && !review.isVisible()) { panic.onClick(); return true; }
        if (key.isKeyCode('S') && key.getModifiers() == ModifierKeys(ModifierKeys::ctrlModifier) && !review.isVisible()) {
            refreshStatus();
            status.grabKeyboardFocus();
            return true;
        }
        if (key.isKeyCode(',') && key.getModifiers().isCommandDown() && !review.isVisible() && processor.audioSettings) {
            processor.audioSettings();
            return true;
        }
        if (key == KeyPress::escapeKey && review.isVisible()) {
            review.setVisible(false);
            for (auto* c : controls) c->setVisible(true);
            for (auto& l : labels) l->setVisible(true);
            showControlPage();
            (returnFocus != nullptr ? returnFocus.getComponent() : &help)->grabKeyboardFocus();
            return true;
        }
        return false;
    }
    Processor& processor;
    hs2port::KeyboardChoice part, msb, lsb, voice, variation, controlPage;
    StringArray pageNames;
    std::array<scbridge::NumericSlider, partControls.size()> sliders;
    std::array<std::unique_ptr<hs2port::KeyboardChoice>, partControls.size()> effectChoices;
    std::array<const xg::fx_param*, partControls.size()> effectDetails{};
    TextButton panic{"All Notes Off"}, reset{"XG Reset"}, help{"Keyboard help"}, settings{"Audio and MIDI Settings"};
    hs2port::ReviewText status, review;
    std::vector<Component*> controls;
    std::vector<std::unique_ptr<Label>> labels;
    Component::SafePointer<Component> returnFocus;
    ui::xg_snapshot snapshot;
    std::unique_ptr<xg::voice_rom> rom;
    std::vector<std::pair<int, int>> variations;
    int variationKey = -1;
    int voiceModeKey = -1;
    int localDrumNote = 36;
    uint64_t lastSerial = 0;
    double holdUntil = 0;
};

AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }
}

void ui::xgui::set_voice_rom(std::shared_ptr<const std::vector<u8>> rom) {
    const std::lock_guard guard(effectCatalogueLock);
    if (rom && !effectCatalogueLoaded) effectCatalogueLoaded = xg::set_fx_type_rom(*rom);
    voiceData = std::move(rom);
}
std::shared_ptr<const std::vector<u8>> mu_pilot::presetRom() {
    const std::lock_guard guard(effectCatalogueLock);
    return voiceData;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor; }

void sendVirtualMidi(juce::AudioProcessor& processor, int port, const juce::MidiMessage& message) {
    if (auto* p = dynamic_cast<Processor*>(&processor))
        p->engine.panel().send_port(port, message.getRawData(), size_t(message.getRawDataSize()));
}

bool muEngineReady(juce::AudioProcessor& processor) {
    auto* p = dynamic_cast<Processor*>(&processor);
    return p != nullptr && p->engine.state() == smu2000::vst3::status::ready;
}

void setMuAudioSettings(juce::AudioProcessor& processor, std::function<void()> callback) {
    if (auto* p = dynamic_cast<Processor*>(&processor)) p->audioSettings = std::move(callback);
}

#if JUCE_WINDOWS
bool useMuEditorMnemonic(juce::AudioProcessorEditor& editor, char letter) {
    if (auto* mu = dynamic_cast<Editor*>(&editor)) return mu->handleMnemonic(letter);
    return false;
}
#endif

bool checkMuPorts(juce::AudioProcessor& processor) {
    auto* p = dynamic_cast<Processor*>(&processor);
    if (p == nullptr || p->engine.state() != smu2000::vst3::status::ready) return false;
    ui::xg_snapshot snapshot;
    p->engine.panel().read_xg(snapshot);
    for (int port = 0; port < portCount; ++port)
        if ((snapshot.parts[port * partsPerPort][programOffset] & 127) != 80 + port) return false;
    auto* editor = processor.getActiveEditor();
    if (editor == nullptr) return false;
    for (auto* child : editor->getChildren()) {
        if (child->getTitle() != "Variation") continue;
        const auto* list = dynamic_cast<juce::ComboBox*>(child);
        return list != nullptr && list->getNumItems() > 1;
    }
    return false;
}
