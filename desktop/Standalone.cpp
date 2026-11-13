#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include <cstdio>
#include <thread>
#include "MuMenuBarStatus.h"
#if JUCE_WINDOWS
#include <windows.h>
#include "WindowsMenuBar.h"
#endif
#include "MusicalKeyboardStrip.h"
#include "AudioPreferences.h"
#include "AudioTestPhrase.h"
#include "ControlMnemonics.h"
#include "PresetExport.h"
#include "ReleaseIdentity.h"
#include "ReviewText.h"
#include "UpdatePresentation.h"
#include "rom_search.h"

void sendVirtualMidi(juce::AudioProcessor&, int port, const juce::MidiMessage&);
void setMuAudioSettings(juce::AudioProcessor&, std::function<void()>);
#if JUCE_WINDOWS
bool useMuEditorMnemonic(juce::AudioProcessorEditor&, char);
#endif
bool checkMuPorts(juce::AudioProcessor&);

namespace {
using namespace juce;
using Component = juce::Component;
using mu_pilot::PresetFormat;

void focusSoon(Component& component) {
    component.grabKeyboardFocus();
    MessageManager::callAsync([safe = Component::SafePointer<Component>(&component)] {
        if (safe && safe->isShowing()
            && (!Component::getCurrentlyFocusedComponent()
                || !safe->getParentComponent()->hasKeyboardFocus(true)))
            safe->grabKeyboardFocus();
    });
}

void message(const String& title, const String& text) {
    AlertWindow::showMessageBoxAsync(MessageBoxIconType::InfoIcon, title, text);
}

struct Preferences final : Component {
    struct Page final : Component {
        struct Folder final : Component {
            Folder(const String& title, const String& browseTitle) : browse(browseTitle) {
                label.setText(title, dontSendNotification);
                label.setAccessible(false);
                path.setName(title); path.setTitle(title); path.setMultiLine(false);
                browse.setTitle(browseTitle);
                addAndMakeVisible(label); addAndMakeVisible(path); addAndMakeVisible(browse);
            }
            void resized() override {
                auto area = getLocalBounds();
                label.setBounds(area.removeFromTop(24));
                browse.setBounds(area.removeFromRight(120));
                path.setBounds(area.withTrimmedRight(8));
            }
            Label label;
            hs2port::ReviewText path;
            TextButton browse{"Browse..."};
        };
        explicit Page(bool presetPage) : presets(presetPage) {
            if (presets) {
                generateNks.setTitle(generateNks.getButtonText());
                generateVst.setTitle(generateVst.getButtonText());
                addAndMakeVisible(nks); addAndMakeVisible(generateNks);
                addAndMakeVisible(vst); addAndMakeVisible(generateVst);
                hs2port::setControlMnemonic(nks.browse, 'n'); hs2port::setControlMnemonic(vst.browse, 'b');
                hs2port::setControlMnemonic(generateNks, 'k'); hs2port::setControlMnemonic(generateVst, 's');
            } else {
                updates.setTitle(updates.getButtonText());
                addAndMakeVisible(rom); addAndMakeVisible(updates);
                hs2port::setControlMnemonic(rom.browse, 'r'); hs2port::setControlMnemonic(updates, 'u');
            }
        }
#if JUCE_WINDOWS
        bool handleMnemonic(char key) {
            for (auto* button : {&rom.browse, &updates, &nks.browse, &generateNks, &vst.browse, &generateVst})
                if (mu_pilot::useMnemonic(*button, key)) return true;
            return false;
        }
#endif
        void resized() override {
            auto area = getLocalBounds().reduced(18);
            const auto place = [&](Folder& folder, TextButton& action) {
                folder.setBounds(area.removeFromTop(58));
                area.removeFromTop(8);
                action.setBounds(area.removeFromTop(34).removeFromLeft(266));
                area.removeFromTop(18);
            };
            if (presets) { place(nks, generateNks); place(vst, generateVst); }
            else place(rom, updates);
        }
        const bool presets;
        Folder rom{"MU2000 ROM folder", "Browse ROMs..."};
        Folder nks{"Komplete Kontrol User Content folder", "Browse NKS..."};
        Folder vst{"VST3 preset folder", "Browse VST3..."};
        TextButton generateNks{"Generate Komplete Kontrol Presets"}, generateVst{"Generate VST3 Presets"};
        TextButton updates{"Check for Updates..."};
    };
    class Tabs final : public TabbedComponent {
        class Tab final : public TabBarButton {
        public:
            Tab(const String& title, TabbedButtonBar& bar, int index) : TabBarButton(title, bar) {
                setTitle(title); setWantsKeyboardFocus(true); setExplicitFocusOrder(index + 1);
            }
            bool keyPressed(const KeyPress& key) override {
                if (key.getModifiers().isAnyModifierKeyDown()) return TabBarButton::keyPressed(key);
                auto& bar = getTabbedButtonBar();
                const int last = bar.getNumTabs() - 1;
                int next = getIndex();
                if (key == KeyPress::leftKey || key == KeyPress::upKey)
                    next = (next + last) % bar.getNumTabs();
                else if (key == KeyPress::rightKey || key == KeyPress::downKey)
                    next = (next + 1) % bar.getNumTabs();
                else if (key == KeyPress::homeKey) next = 0;
                else if (key == KeyPress::endKey) next = last;
                else return TabBarButton::keyPressed(key);
                bar.setCurrentTabIndex(next);
                bar.getTabButton(next)->grabKeyboardFocus();
                return true;
            }
        };
    public:
        static constexpr std::array<char, 3> mnemonics{'g', 'a', 'p'};
        Tabs() : TabbedComponent(TabbedButtonBar::TabsAtTop) {}
        TabBarButton* createTabButton(const String& title, int index) override {
            return new Tab(title, getTabbedButtonBar(), index);
        }
    };
    explicit Preferences(AudioDeviceManager& devices)
        : audio(devices) {
        setName("Preferences"); setTitle(getName());
        tabs.setName("Preferences categories"); tabs.setTitle(tabs.getName());
        tabs.setFocusContainerType(FocusContainerType::focusContainer);
        tabs.getTabbedButtonBar().setFocusContainerType(FocusContainerType::focusContainer);
        tabs.getTabbedButtonBar().setExplicitFocusOrder(1);
        general.setExplicitFocusOrder(2); presets.setExplicitFocusOrder(2); audio.setExplicitFocusOrder(2);
        const auto bg = getLookAndFeel().findColour(ResizableWindow::backgroundColourId);
        tabs.addTab("General", bg, &general, false);
        tabs.addTab("Audio and MIDI", bg, &audio, false);
        tabs.addTab("Presets", bg, &presets, false);
        for (int i = 0; i < tabs.getNumTabs(); ++i)
            hs2port::setControlMnemonic(*tabs.getTabbedButtonBar().getTabButton(i), Tabs::mnemonics[size_t(i)]);
        close.setTitle("Close Preferences");
        hs2port::setControlAccelerator(close, "Escape");
        tabs.setExplicitFocusOrder(1); close.setExplicitFocusOrder(2);
        addAndMakeVisible(tabs);
        addAndMakeVisible(close);
    }
    void focusCurrentTab() {
        if (auto* tab = tabs.getTabbedButtonBar().getTabButton(tabs.getCurrentTabIndex())) focusSoon(*tab);
    }
#if JUCE_WINDOWS
    bool handleMnemonic(char key) {
        for (int i = 0; i < tabs.getNumTabs(); ++i) {
            if (key != Tabs::mnemonics[size_t(i)]) continue;
            tabs.setCurrentTabIndex(i);
            focusCurrentTab();
            return true;
        }
        return general.handleMnemonic(key) || audio.handleMnemonic(key) || presets.handleMnemonic(key);
    }
#endif
    void resized() override {
        auto area = getLocalBounds().reduced(12);
        close.setBounds(area.removeFromBottom(34).removeFromRight(120));
        area.removeFromBottom(8);
        tabs.setBounds(area);
    }
    Page general{false}, presets{true};
    mu_pilot::AudioPreferences audio;
    Tabs tabs;
    TextButton close{"Close"};
};

enum MenuAction { showControls = 1, openPreferences, notesOff, quitApp, hideControls, musicalKeyboard, checkUpdates };

#if JUCE_MAC
constexpr auto hideTitle = "Hide to Menu Bar";
constexpr auto hideMenuTitle = "&Hide to Menu Bar";
constexpr auto hideModifiers = ModifierKeys::commandModifier | ModifierKeys::shiftModifier;
#else
constexpr auto hideTitle = "Hide to System Tray";
constexpr auto hideMenuTitle = "&Hide to System Tray";
constexpr auto hideModifiers = ModifierKeys::commandModifier;
#endif

class Application final : public juce::JUCEApplication, private MenuBarModel {
    class ExportJob final : public juce::ThreadWithProgressWindow {
    public:
        ExportJob(File folder, PresetFormat type)
            : ThreadWithProgressWindow(type == PresetFormat::nks ? "Generate Komplete Kontrol Presets" : "Generate VST3 Presets",
                                       true, true, 30000), destination(std::move(folder)), format(type) {}
        void run() override {
            result = mu_pilot::exportPresets(destination, format, cancel, [this](int done, int total) {
                cancel.store(threadShouldExit());
                if (total > 0) setProgress(double(done) / total);
                if (done % 20 == 0) setStatusMessage("Checking sound " + juce::String(done + 1) + " of " + juce::String(total));
            });
        }
        void threadComplete(bool userPressedCancel) override {
            if (completed) completed(userPressedCancel);
        }
        std::function<void(bool)> completed;
        juce::String result;
    private:
        juce::File destination;
        PresetFormat format;
        std::atomic<bool> cancel{false};
    };
    class Window final : public DocumentWindow, private KeyListener {
        class Content final : public Component {
        public:
            explicit Content(StandalonePluginHolder& holder)
                : processor(*holder.processor), editor(processor.createEditorAndMakeActive()),
                  preferences(holder.deviceManager),
                  keyboard([this](MidiMessage event) {
                      sendVirtualMidi(processor, port, event);
                      if (monitorKeyboard) monitorKeyboard(port, event);
                  }) {
                setSize(editor->getWidth(), editor->getHeight() + toolbarHeight);
                addAndMakeVisible(*editor);
                for (auto* child : editor->getChildren()) {
                    if (child->getTitle() == "Part") part = dynamic_cast<ComboBox*>(child);
                }
                typing.setTitle("Musical Keyboard");
                hs2port::setControlAccelerator(typing, "Ctrl+K");
                typing.setClickingTogglesState(true);
                typing.onClick = [this] {
                    keyboard.setArmed(typing.getToggleState(), isDrumPart());
                    refreshKeyboard();
                    resized();
                };
                keyboard.refresh = [this] { refreshKeyboard(); };
                addAndMakeVisible(typing);
                hideButton.setTitle(hideTitle);
                hs2port::setControlAccelerator(hideButton, "Ctrl+H");
                addAndMakeVisible(hideButton);
                addChildComponent(keyboard);
                addChildComponent(preferences);
                preferences.close.onClick = [this] { showPreferences(false); };
            }
            ~Content() override { keyboard.setArmed(false, false); }
            void showPreferences(bool visible) {
                if (visible != preferences.isVisible()) {
                    if (visible) previousFocus = getCurrentlyFocusedComponent();
                    editor->setVisible(!visible);
                    typing.setVisible(!visible);
                    hideButton.setVisible(!visible);
                    preferences.setVisible(visible);
                    refreshKeyboard();
                }
                if (visible) preferences.focusCurrentTab();
                else restoreFocus(previousFocus.getComponent());
            }
            void restoreFocus(Component* preferred = nullptr) {
                if (preferences.isVisible()) { preferences.focusCurrentTab(); return; }
                if (preferred != nullptr && preferred->isShowing() && isParentOf(preferred)) focusSoon(*preferred);
                else if (part != nullptr && part->isShowing()) focusSoon(*part);
                else if (typing.isShowing()) focusSoon(typing);
            }
            void toggleKeyboard() {
                if (!preferences.isVisible()) {
                    typing.setToggleState(!typing.getToggleState(), dontSendNotification);
                    if (typing.onClick) typing.onClick();
                }
            }
            bool play(const KeyPress& key) {
                refreshKeyboard();
                return keyboard.playKey(key);
            }
            void allNotesOff() {
                keyboard.releaseNotes();
                for (int p = 0; p < 4; ++p)
                    for (int ch = 1; ch <= 16; ++ch) sendVirtualMidi(processor, p, MidiMessage::allSoundOff(ch));
            }
            void refreshKeyboard() {
                const auto* focus = getCurrentlyFocusedComponent();
                const bool editingKeyboard = keyboard.isEnteringSetting();
                bool blocksTyping = !isShowing() || !editor->isVisible() || Component::getNumCurrentlyModalComponents() != 0;
                for (auto* node = focus; node != nullptr && node != this; node = node->getParentComponent())
                    if (auto* field = dynamic_cast<const TextEditor*>(node);
                        field != nullptr && !editingKeyboard
                        && (!field->isReadOnly() || field->getTitle() == "Keyboard help text")) blocksTyping = true;
                if (focus == nullptr || !isParentOf(focus)) blocksTyping = true;
                const int selected = selectedPart();
                if (const int nextPort = selected / 16; nextPort != port) {
                    keyboard.releaseNotes();
                    port = nextPort;
                }
                if (selected != keyboardPart) {
                    keyboard.setPart(selected % 16 + 1, String::charToString(juce_wchar('A' + port)) + String(selected % 16 + 1));
                    keyboardPart = selected;
                }
                keyboard.setPlaying(typing.getToggleState() && !blocksTyping);
            }
            bool isDrumPart() const {
                return selectedPart() % 16 == 9;
            }
            int selectedPart() const { return part != nullptr ? jmax(0, part->getSelectedItemIndex()) : 0; }
            void resized() override {
                auto area = getLocalBounds();
                preferences.setBounds(area);
                const bool armed = typing.getToggleState();
                if (armed) keyboard.setBounds(area.removeFromBottom(keyboardHeight).reduced(12, 4));
                auto row = area.removeFromBottom(toolbarHeight).reduced(10, 3);
                typing.setBounds(row.removeFromLeft(180));
                row.removeFromLeft(8);
                hideButton.setBounds(row.removeFromLeft(180));
                editor->setBounds(area);
            }
            int desiredHeight() const {
                return baseHeight + (typing.getToggleState() ? keyboardHeight : 0);
            }
            static constexpr int toolbarHeight = 36, keyboardHeight = 88;
            AudioProcessor& processor;
            std::unique_ptr<AudioProcessorEditor> editor;
            const int baseHeight = editor->getHeight() + toolbarHeight;
            Preferences preferences;
            scbridge::MusicalKeyboardStrip keyboard;
            TextButton typing{"Musical Keyboard"};
            TextButton hideButton{hideTitle};
            ComboBox* part = nullptr;
            SafePointer<Component> previousFocus;
            int port = 0;
            int keyboardPart = -1;
            std::function<void(int, MidiMessage)> monitorKeyboard;
        };
    public:
        Window(const String& name, StandalonePluginHolder& holder)
            : DocumentWindow(name, Colours::darkgrey, closeButton | minimiseButton), content(holder) {
            setUsingNativeTitleBar(true);
            setContentNonOwned(&content, true);
            auto buttonAction = content.typing.onClick;
            content.typing.onClick = [this, buttonAction] {
                buttonAction();
                setContentComponentSize(content.getWidth(), content.desiredHeight());
                if (auto* display = Desktop::getInstance().getDisplays().getDisplayForRect(getBounds()))
                    setBounds(getBounds().constrainedWithin(display->userBounds.toNearestInt()));
            };
            addKeyListener(this);
            content.hideButton.onClick = [this] { if (hide) hide(); };
            centreWithSize(getWidth(), getHeight());
        }
        ~Window() override { removeKeyListener(this); clearContentComponent(); }
        Preferences& prefs() { return content.preferences; }
        void showPreferences() { content.showPreferences(true); }
        void toggleKeyboard() { content.toggleKeyboard(); }
        void restoreFocus() { content.restoreFocus(); }
        void allNotesOff() { content.allNotesOff(); }
        void refreshKeyboard() { content.refreshKeyboard(); }
#if JUCE_WINDOWS
        bool handleMnemonic(char letter) {
            return prefs().isVisible() ? prefs().handleMnemonic(letter)
                                       : useMuEditorMnemonic(*content.editor, letter);
        }
        bool checkMnemonics(HWND native, const std::shared_ptr<bool>& generated) {
            const auto press = [native](char letter) {
                SendMessageW(native, WM_SYSKEYDOWN, WPARAM(letter), LPARAM(1) << 29);
            };
            const auto unique = [this] {
                StringArray seen;
                bool valid = true;
                const auto visit = [&](const auto& self, const Component& component) -> void {
                    if (!component.isShowing()) return;
                    const auto key = component.getProperties()["hs2AccessKey"].toString();
                    if (key.isNotEmpty()) {
                        valid = valid && !seen.contains(key) && key != "Alt+F" && key != "Alt+V" && key != "Alt+H";
                        seen.add(key);
                    }
                    for (auto* child : component.getChildren()) self(self, *child);
                };
                visit(visit, content);
                return valid && seen.size() >= 5;
            };
            const auto focused = [](const char* title) {
                for (auto* component = Component::getCurrentlyFocusedComponent(); component != nullptr;
                     component = component->getParentComponent())
                    if (component->getTitle() == title) return true;
                return false;
            };
            press('W');
            const bool sound = focused("Current sound") && unique();
            showPreferences();
            press('A');
            const bool audio = focused("Audio and MIDI") && unique();
            press('D');
            const bool driver = focused("Audio driver");
            press('P');
            const bool presets = focused("Presets") && unique();
            const bool scoped = !handleMnemonic('d') && !handleMnemonic('w');
            auto original = std::move(prefs().presets.generateNks.onClick);
            prefs().presets.generateNks.onClick = [generated] { *generated = true; };
            press('K');
            const bool generateFocused = focused("Generate Komplete Kontrol Presets");
            press('G');
            const bool general = focused("General") && unique();
            MessageManager::callAsync([safe = SafePointer<Window>(this), original = std::move(original)]() mutable {
                if (safe) safe->prefs().presets.generateNks.onClick = std::move(original);
            });
            content.showPreferences(false);
            std::printf("Mnemonic targets: sound %d; audio tab %d; driver %d; presets tab %d; scope %d; general tab %d; export %d\n",
                        int(sound), int(audio), int(driver), int(presets), int(scoped), int(general), int(generateFocused));
            return sound && audio && driver && presets && scoped && general && generateFocused;
        }
#endif
        bool checkPresetPlacement() {
            const auto* editor = content.editor.get();
            for (const auto* child : editor->getChildren())
                if (child->getTitle().startsWith("Generate ")) return false;
            return prefs().presets.generateNks.onClick && prefs().presets.generateVst.onClick;
        }
        bool checkHideButton() {
            if (!content.hideButton.isShowing() || !content.hideButton.getWantsKeyboardFocus()
                || content.hideButton.getTitle() != hideTitle || !content.hideButton.onClick) return false;
            content.hideButton.onClick();
            return !isVisible();
        }
        bool checkHideShortcut() {
            return keyPressed(KeyPress('H', hideModifiers, 0), this)
                && !isVisible();
        }
        bool checkFocus() {
            const bool first = prefs().tabs.getTabbedButtonBar().getTabButton(0)->hasKeyboardFocus(false);
            if (auto* focused = Component::getCurrentlyFocusedComponent()) focused->moveKeyboardFocusToSibling(true);
            const bool next = prefs().tabs.getTabbedButtonBar().getTabButton(1)->hasKeyboardFocus(false);
            if (auto* focused = Component::getCurrentlyFocusedComponent()) focused->moveKeyboardFocusToSibling(true);
            const bool presets = prefs().tabs.getTabbedButtonBar().getTabButton(2)->hasKeyboardFocus(false);
            content.showPreferences(false);
            const bool returned = content.part != nullptr && content.part->hasKeyboardFocus(false);
            std::printf("General initially focused: %d; Tab reaches Audio and MIDI: %d; restored: %d\n",
                        int(first), int(next), int(returned));
            return first && next && presets && returned;
        }
        bool checkKeyboard() {
            if (content.part == nullptr || content.part->getNumItems() < 18) return false;
            int count = 0, firstPort = -1, lastPort = -1, pitch = -1, channel = -1;
            bool released = false;
            content.monitorKeyboard = [&](int port, MidiMessage event) {
                if (count++ == 0) { firstPort = port; pitch = event.getNoteNumber(); channel = event.getChannel(); }
                lastPort = port;
                released = event.isNoteOff();
            };
            content.toggleKeyboard();
            content.part->setSelectedItemIndex(17, dontSendNotification);
            const bool played = content.play(KeyPress('a'));
            content.part->setSelectedItemIndex(33, dontSendNotification);
            content.refreshKeyboard();
            showPreferences();
            const bool suppressed = !content.play(KeyPress('a'));
            content.showPreferences(false);
            const bool resumed = content.keyboard.isVisible();
            content.toggleKeyboard();
            content.monitorKeyboard = {};
            std::printf("Played: %d; events: %d; ports: %d/%d; pitch: %d; channel: %d; released: %d; blocked: %d; resumed: %d\n",
                int(played), count, firstPort, lastPort, pitch, channel, int(released), int(suppressed), int(resumed));
            return played && count == 2 && firstPort == 1 && lastPort == 1
                && pitch == 60 && channel == 2 && released && suppressed && resumed;
        }
        std::function<void()> hide;
        void closeButtonPressed() override {
            juce::JUCEApplicationBase::getInstance()->systemRequestedQuit();
        }
    private:
        bool keyPressed(const KeyPress& key, Component*) override {
#if JUCE_WINDOWS
            if (const auto letter = mu_pilot::controlMnemonic(key); letter && handleMnemonic(letter)) return true;
#endif
            const auto mods = key.getModifiers();
            if (key == KeyPress::escapeKey && prefs().isVisible()) { content.showPreferences(false); return true; }
            if (mods.isCommandDown() && !mods.isAltDown()) {
                if (key.isKeyCode(',') && !mods.isShiftDown()) { showPreferences(); return true; }
                if (key.isKeyCode('K') && !mods.isShiftDown() && !prefs().isVisible()) { content.toggleKeyboard(); return true; }
                if (key.isKeyCode('H') && mods.withoutMouseButtons() == ModifierKeys(hideModifiers) && hide) {
                    hide();
                    return true;
                }
            }
            return content.play(key);
        }
        Content content;
    };
    struct Receiver final : juce::MidiInputCallback {
        juce::AudioProcessor* target = nullptr;
        int port = 0;
        void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override {
            if (target != nullptr) sendVirtualMidi(*target, port, message);
        }
    };
public:
    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String& args) override {
        juce::PropertiesFile::Options options;
        options.applicationName = "MU2000 Pilot";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        const auto testProfile = juce::SystemStats::getEnvironmentVariable("MU_PILOT_TEST_PROFILE", {});
        const auto testExport = juce::SystemStats::getEnvironmentVariable("MU_PILOT_TEST_EXPORT", {});
        if ((args == "--check-export" || args == "--check-export-vst3")
            && File::isAbsolutePath(testProfile) && File::isAbsolutePath(testExport)) {
            const bool requestCancel = juce::SystemStats::getEnvironmentVariable("MU_PILOT_TEST_CANCEL", {}) == "1";
            std::atomic<bool> cancelled{requestCancel};
            const auto result = mu_pilot::exportPresets(File(testExport),
                args == "--check-export" ? PresetFormat::nks : PresetFormat::vst3, cancelled, {});
            std::puts(result.toRawUTF8());
            setApplicationReturnValue(result.startsWith(requestCancel ? "Cancelled." : "Generated ") ? 0 : 1);
            quit();
            return;
        }
        settings = juce::File::isAbsolutePath(testProfile)
            ? std::make_unique<juce::PropertiesFile>(juce::File(testProfile), options)
            : std::make_unique<juce::PropertiesFile>(options);
        holder = std::make_unique<juce::StandalonePluginHolder>(settings.get(), false, juce::String{}, nullptr,
            juce::Array<juce::StandalonePluginHolder::PluginInOuts>{}, false);
        if (args == "--check-audio-drivers" && File::isAbsolutePath(testProfile)) {
            StringArray asio;
            bool wasapi = false;
            for (auto* type : holder->deviceManager.getAvailableDeviceTypes()) {
                if (type->getTypeName() == "ASIO") {
                    type->scanForDevices();
                    asio = type->getDeviceNames(false);
                }
                wasapi |= type->getTypeName().startsWith("Windows Audio");
            }
            std::printf("WASAPI: %d; ASIO outputs: %s\n", int(wasapi), asio.joinIntoString(", ").toRawUTF8());
            setApplicationReturnValue(wasapi && asio.contains("Komplete Audio ASIO Driver") ? 0 : 1);
            quit();
            return;
        }
        if (args == "--check-audio-restore" && File::isAbsolutePath(testProfile)) {
            auto& devices = holder->deviceManager;
            auto* device = devices.getCurrentAudioDevice();
            const auto type = devices.getCurrentAudioDeviceType();
            const auto chosen = device != nullptr ? device->getName() : String{};
            std::printf("Opened device: %s / %s\n", type.toRawUTF8(), chosen.toRawUTF8());
            const bool ready = device != nullptr && type == "ASIO" && chosen == "Komplete Audio ASIO Driver";
            setApplicationReturnValue(ready ? 0 : 1);
            if (ready) holder->saveAudioDeviceState();
            quit();
            return;
        }
        setMuAudioSettings(*holder->processor, [this] { window->showPreferences(); });
        window = std::make_unique<Window>(getApplicationName(), *holder);
        auto& audio = window->prefs().audio;
        testPhrase = std::make_unique<mu_pilot::AudioTestPhrase>(holder->deviceManager, [this](bool ok) {
            auto& page = window->prefs().audio;
            page.setPlaying(false);
            if (testMeter) {
                std::printf("Test phrase completed: %d; captured output peak: %.6f\n", int(ok), double(testPeak));
                setApplicationReturnValue(ok && testPeak > 0.001 ? 0 : 1);
                systemRequestedQuit();
                return;
            }
            if (!ok) message("Test phrase", "The audio device or MU2000 ROMs are not ready.");
        });
        const auto stopPhrase = [this] { testPhrase->stop(); window->prefs().audio.setPlaying(false); };
        audio.beforeOutputChange = stopPhrase;
        audio.test.onClick = [this, stopPhrase] {
            if (testPhrase->isPlaying()) { stopPhrase(); return; }
            if (testPhrase->start()) window->prefs().audio.setPlaying(true);
            else message("Test phrase", "Choose an active audio output first.");
        };
        auto& general = window->prefs().general;
        auto& presets = window->prefs().presets;
        general.rom.browse.onClick = [this] { chooseRomFolder(); };
        presets.nks.browse.onClick = [this] { choosePresetFolder(PresetFormat::nks, false); };
        presets.vst.browse.onClick = [this] { choosePresetFolder(PresetFormat::vst3, false); };
        presets.generateNks.onClick = [this] { generatePresets(PresetFormat::nks); };
        presets.generateVst.onClick = [this] { generatePresets(PresetFormat::vst3); };
        general.updates.onClick = [this] { checkForUpdates(); };
        refreshPaths();
        window->hide = [this] { hideWindow(); };
        status = std::make_unique<scbridge::MenuBarStatus>(
            [this](int action) { menuItemSelected(action, -1); }, showControls, openPreferences, notesOff, quitApp,
            true);
#if JUCE_MAC
        MenuBarModel::setMacMainMenu(this);
#endif
#if JUCE_MAC
        for (int i = 0; args != "--check-standalone-ui" && i < int(inputs.size()); ++i) {
            auto& receiver = receivers[size_t(i)];
            receiver.port = i;
            receiver.target = holder->processor.get();
            const auto name = juce::String("MU2000 Pilot Port ") + juce::String::charToString(juce::juce_wchar('A' + i));
            inputs[size_t(i)] = juce::MidiInput::createNewDevice(name, &receiver);
            if (inputs[size_t(i)] != nullptr) inputs[size_t(i)]->start();
        }
#endif
        anotherInstanceStarted({});
        if (args == "--check-test-phrase" && File::isAbsolutePath(testProfile)) {
            testMeter = holder->deviceManager.getOutputLevelGetter();
            if (!testPhrase->start()) { setApplicationReturnValue(1); systemRequestedQuit(); return; }
            testPhrase->onTick = [this] {
                testPeak = jmax(testPeak, testMeter->getCurrentLevel());
            };
        }
#if JUCE_WINDOWS
        commands.registerAllCommandsForTarget(this);
        commands.setFirstCommandTarget(this);
        windowsMenu = std::make_unique<scbridge::WindowsMenuBar>(*window, commands,
            std::vector<scbridge::WindowsMenuBar::Section>{
                {"&File", {openPreferences, quitApp}, {"&Preferences...", "&Quit"}},
                {"&View", {musicalKeyboard, hideControls, notesOff}, {"&Musical Keyboard", hideMenuTitle, "&All Sounds Off"}},
                {"&Help", {checkUpdates}, {"Check for &Updates..."}}});
        if (auto* peer = window->getPeer()) {
            const auto hwnd = static_cast<HWND>(peer->getNativeHandle());
            if (SetWindowSubclass(hwnd, mnemonicMessage, mnemonicSubclassId, reinterpret_cast<DWORD_PTR>(this)))
                mnemonicWindow = hwnd;
        }
#endif
        if (args == "--check-standalone-ui" && File::isAbsolutePath(testProfile))
            Timer::callAfterDelay(450, [this] {
                window->showPreferences();
                const bool focus = window->checkFocus();
                const bool keyboard = window->checkKeyboard();
                const bool versions = mu_pilot::parseReleaseTag("mu2000-v1.2.10") > mu_pilot::parseReleaseTag("mu2000-v1.2.9")
                    && !mu_pilot::parseReleaseTag("v1.2.10") && !mu_pilot::parseReleaseTag("mu2000-v1..10");
                const bool placement = window->checkPresetPlacement();
#if JUCE_WINDOWS
                const auto mnemonicAction = std::make_shared<bool>(false);
                const bool mnemonics = mnemonicWindow && window->checkMnemonics(mnemonicWindow, mnemonicAction);
#else
                const auto mnemonicAction = std::make_shared<bool>(true);
                const bool mnemonics = true;
#endif
                const bool button = window->checkHideButton();
                menuItemSelected(showControls, -1);
                const bool restored = window->isVisible();
                const bool shortcut = window->checkHideShortcut();
                menuItemSelected(showControls, -1);
                const bool menuDispatched = checkHideMenu();
                MessageManager::callAsync([=, this] {
                    std::printf("Scoped mnemonics: %d; mnemonic button action: %d\n", int(mnemonics), int(*mnemonicAction));
                    const bool menu = menuDispatched && !window->isVisible();
                    menuItemSelected(showControls, -1);
                    const bool menuRestored = window->isVisible();
                    std::printf("Preferences focus: %d; musical keyboard: %d; release version: %d; preset placement: %d; hide button: %d; restore: %d; hide shortcut: %d; view menu: %d\n",
                                int(focus), int(keyboard), int(versions), int(placement), int(button),
                                int(restored && menuRestored), int(shortcut), int(menu));
                    setApplicationReturnValue(focus && keyboard && versions && placement && mnemonics && *mnemonicAction && button
                                              && restored && shortcut && menu && menuRestored ? 0 : 1);
                    systemRequestedQuit();
                });
            });
        if (args == "--check-ports" && juce::File::isAbsolutePath(testProfile))
            juce::Timer::callAfterDelay(7500, [this] {
                const bool pass = checkMuPorts(*holder->processor) && window->isShowing();
                std::puts(pass ? "Standalone: four independent MIDI programs reached the engine."
                               : "Standalone: port or window readback failed.");
                setApplicationReturnValue(pass ? 0 : 1);
                systemRequestedQuit();
            });
    }
    void anotherInstanceStarted(const juce::String&) override {
        if (window == nullptr) return;
        if (status != nullptr) status->setControlsHidden(false);
        window->setVisible(true);
        window->setMinimised(false);
        window->toFront(true);
        window->restoreFocus();
    }
    void systemRequestedQuit() override {
        if (exporting) return;
        if (holder != nullptr) { holder->saveAudioDeviceState(); holder->savePluginState(); }
        quit();
    }
    void shutdown() override {
        updateCheck.request_stop();
        if (updateCheck.joinable()) updateCheck.join();
#if JUCE_MAC
        MenuBarModel::setMacMainMenu(nullptr);
#else
        if (mnemonicWindow && IsWindow(mnemonicWindow))
            RemoveWindowSubclass(mnemonicWindow, mnemonicMessage, mnemonicSubclassId);
        windowsMenu.reset();
#endif
        status.reset();
        testPhrase.reset();
        for (auto& input : inputs) { if (input != nullptr) input->stop(); input.reset(); }
        window.reset();
        holder.reset();
        settings.reset();
    }
private:
    bool checkHideMenu() {
#if JUCE_WINDOWS
        if (window->getPeer() == nullptr) return false;
        const auto menu = GetMenu(static_cast<HWND>(window->getPeer()->getNativeHandle()));
        const auto view = GetSubMenu(menu, 1);
        wchar_t label[128]{};
        if (!GetMenuStringW(view, 1, label, int(std::size(label)), MF_BYPOSITION)
            || !String(label).startsWith(hideMenuTitle)) return false;
        const auto id = GetMenuItemID(view, 1);
        SendMessageW(static_cast<HWND>(window->getPeer()->getNativeHandle()), WM_COMMAND, id, 0);
        return true;
#else
        const auto menu = getMenuForIndex(1, "View");
        PopupMenu::MenuItemIterator item(menu);
        while (item.next())
            if (item.getItem().itemID == hideControls && item.getItem().text == hideMenuTitle) {
                menuItemSelected(hideControls, 1);
                return true;
            }
        return false;
#endif
    }
#if JUCE_WINDOWS
    static constexpr UINT_PTR mnemonicSubclassId = 1;
    static LRESULT CALLBACK mnemonicMessage(HWND hwnd, UINT message, WPARAM key, LPARAM data,
                                            UINT_PTR, DWORD_PTR context) {
        // The native menu receives system keys before JUCE. Leave its top-level
        // access keys, bare Alt, and modified combinations to the system.
        auto& app = *reinterpret_cast<Application*>(context);
        if (message == WM_SYSKEYDOWN) {
            app.consumedMnemonic = 0;
            if ((data & (LPARAM(1) << 29)) && key >= 'A' && key <= 'Z'
                && key != 'F' && key != 'V' && key != 'H'
                && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000)
                && !app.choosing && !app.exporting && !Component::getNumCurrentlyModalComponents()
                && app.window->handleMnemonic(char(key - 'A' + 'a'))) {
                app.consumedMnemonic = char(key - 'A' + 'a');
                return 0;
            }
        } else if (message == WM_SYSCHAR && app.consumedMnemonic
                   && CharacterFunctions::toLowerCase(juce_wchar(key)) == app.consumedMnemonic) {
            app.consumedMnemonic = 0;
            return 0;
        }
        return DefSubclassProc(hwnd, message, key, data);
    }
    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(Array<CommandID>& ids) override {
        for (int action : {openPreferences, quitApp, musicalKeyboard, hideControls, notesOff, checkUpdates}) ids.add(action);
    }
    void getCommandInfo(CommandID action, ApplicationCommandInfo& info) override {
        switch (action) {
            case openPreferences:
                info.setInfo("Preferences...", {}, "File", 0);
                info.addDefaultKeypress(',', ModifierKeys::commandModifier); break;
            case quitApp: info.setInfo("Quit", {}, "File", 0); break;
            case musicalKeyboard:
                info.setInfo("Musical Keyboard", {}, "View", 0);
                info.addDefaultKeypress('k', ModifierKeys::commandModifier); break;
            case hideControls:
                info.setInfo(hideTitle, {}, "View", 0);
                info.addDefaultKeypress('h', hideModifiers); break;
            case notesOff: info.setInfo("All Sounds Off", {}, "View", 0); break;
            case checkUpdates: info.setInfo("Check for Updates...", {}, "Help", 0); break;
            default: break;
        }
    }
    bool perform(const InvocationInfo& info) override {
        menuItemSelected(info.commandID, -1);
        return true;
    }
#endif
    StringArray getMenuBarNames() override { return {"&File", "&View", "&Help"}; }
    PopupMenu getMenuForIndex(int index, const String&) override {
        PopupMenu menu;
        if (index == 0) {
            menu.addItem(openPreferences, "&Preferences...");
            menu.addItem(quitApp, "&Quit");
        } else if (index == 1) {
            menu.addItem(musicalKeyboard, "&Musical Keyboard");
            menu.addItem(hideControls, hideMenuTitle);
            menu.addItem(notesOff, "&All Sounds Off");
        } else if (index == 2) {
            menu.addItem(checkUpdates, "Check for &Updates...");
        }
        return menu;
    }
    void menuItemSelected(int action, int) override {
        if (window == nullptr) return;
        switch (action) {
            case showControls: anotherInstanceStarted({}); break;
            case openPreferences:
                anotherInstanceStarted({});
                window->showPreferences();
                break;
            case notesOff: window->allNotesOff(); break;
            case quitApp: systemRequestedQuit(); break;
            case hideControls: hideWindow(); break;
            case musicalKeyboard: window->toggleKeyboard(); break;
            case checkUpdates: checkForUpdates(); break;
            default: break;
        }
    }
    void hideWindow() {
        if (status == nullptr || !status->available() || choosing || exporting) return;
        status->setControlsHidden(true);
        window->setVisible(false);
        window->refreshKeyboard();
    }
    static const char* presetSetting(PresetFormat format) {
        return format == PresetFormat::nks ? "nksFolder" : "vstPresetFolder";
    }
    File presetFolder(PresetFormat format) const {
        const auto stored = settings->getValue(presetSetting(format));
        if (File::isAbsolutePath(stored)) return File(stored);
        if (format == PresetFormat::nks)
            return File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("Native Instruments/User Content");
#if JUCE_MAC
        const auto root = File::getSpecialLocation(File::userHomeDirectory).getChildFile("Library/Audio/Presets");
#else
        const auto root = File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("VST3 Presets");
#endif
        return root.getChildFile("OnjMusic/MU2000");
    }
    String romFolder() const {
        std::string tried;
        return String::fromUTF8(smu2000::find_roms(File::getSpecialLocation(File::currentExecutableFile)
            .getParentDirectory().getFullPathName().toStdString(), false, tried).c_str());
    }
    void refreshPaths() {
        const auto path = romFolder();
        auto& prefs = window->prefs();
        prefs.general.rom.path.refreshText(path.isEmpty() ? "No ROM folder found" : path);
        prefs.presets.nks.path.refreshText(presetFolder(PresetFormat::nks).getFullPathName());
        prefs.presets.vst.path.refreshText(presetFolder(PresetFormat::vst3).getFullPathName());
        for (auto* field : {&prefs.general.rom.path, &prefs.presets.nks.path, &prefs.presets.vst.path})
            field->setCaretPosition(0);
    }
    void chooseRomFolder() {
        if (choosing || exporting) return;
        choosing = true;
        const auto current = romFolder();
        chooser = std::make_unique<FileChooser>("Choose MU2000 ROM Folder",
            File::isAbsolutePath(current) ? File(current) : File::getSpecialLocation(File::userHomeDirectory));
        chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
            [this](const FileChooser& dialog) {
                choosing = false;
                const auto directory = dialog.getResult();
                if (!directory.isDirectory()) return;
                const auto selected = smu2000::accept_roms_choice(directory.getFullPathName().toStdString());
                if (selected.empty()) {
                    message("MU2000 ROM folder", "That folder does not contain a complete MU2000 ROM set. Your previous location was kept.");
                    return;
                }
                const auto config = smu2000::config_dir();
                const auto path = String::fromUTF8(selected.c_str());
                if (config.empty() || path.containsAnyOf("\r\n") || path.getNumBytesAsUTF8() > 1022) {
                    message("MU2000 ROM folder", "That location could not be saved. Your previous location was kept.");
                    return;
                }
                const auto pointer = File(String::fromUTF8(config.c_str())).getChildFile("roms.txt");
                if (pointer.getParentDirectory().createDirectory().failed() || !pointer.replaceWithText(path + "\n")) {
                    message("MU2000 ROM folder", "That location could not be saved. Your previous location was kept.");
                    return;
                }
                refreshPaths();
                const auto override = SystemStats::getEnvironmentVariable("S_MU2000_ROMS", {});
                message("MU2000 ROM folder", override.isNotEmpty()
                    ? "Location saved. The S_MU2000_ROMS environment setting takes precedence when valid."
                    : "Location saved. Restart MU2000 and any hosts using its plug-in to load these ROMs.");
            });
    }
    void choosePresetFolder(PresetFormat format, bool generate) {
        if (choosing || exporting) return;
        choosing = true;
        chooser = std::make_unique<FileChooser>(format == PresetFormat::nks
            ? "Choose Komplete Kontrol User Content" : "Choose VST3 Preset Folder", presetFolder(format));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, format, generate](const juce::FileChooser& dialog) {
                choosing = false;
                const auto directory = dialog.getResult();
                if (!directory.isDirectory()) return;
                settings->setValue(presetSetting(format), directory.getFullPathName());
                refreshPaths();
                if (generate) startExport(directory, format);
            });
    }
    void generatePresets(PresetFormat format) {
        if (choosing || exporting) return;
        const auto folder = presetFolder(format);
        if (format == PresetFormat::vst3 || (settings->getValue(presetSetting(format)).isNotEmpty() && folder.isDirectory()))
            startExport(folder, format);
        else choosePresetFolder(format, true);
    }
    void startExport(const File& directory, PresetFormat format) {
        exporting = true;
        exportJob = std::make_unique<ExportJob>(directory, format);
        exportJob->completed = [this](bool cancelled) {
            exporting = false;
            message("MU2000 Presets", cancelled && exportJob->result.isEmpty()
                ? String("Generation cancelled. Your previous presets were retained.") : exportJob->result);
        };
        exportJob->launchThread();
    }
    void checkForUpdates(var release = {}) {
        auto& button = window->prefs().general.updates;
        if (!button.isEnabled()) return;
        const auto executable = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory();
#if JUCE_MAC
        const auto helper = executable.getParentDirectory().getChildFile("Resources/Mu2000Maintenance");
#else
        const auto helper = executable.getChildFile("Mu2000Maintenance.exe");
#endif
        if (!helper.existsAsFile()) {
            message("Check for Updates", "The MU2000 updater is missing from this installation.");
            return;
        }
        const bool preparing = release.isObject();
        const auto json = JSON::toString(release, true);
        StringArray args{helper.getFullPathName()};
        if (!preparing) args.addArray({"--check", getApplicationVersion()});
#if JUCE_MAC
        else args.addArray({"--prepare-json", json});
#else
        else args.addArray({"--prepare-base64", Base64::toBase64(json.toRawUTF8(), json.getNumBytesAsUTF8())});
#endif
        button.setEnabled(false);
        updateCheck = std::jthread([this, args, preparing, safe = Component::SafePointer<Window>(window.get())](std::stop_token stop) {
            ChildProcess process;
            var result;
            if (process.start(args)) {
                const auto deadline = Time::getMillisecondCounterHiRes() + (preparing ? 150000 : 40000);
                while (process.isRunning() && !stop.stop_requested() && Time::getMillisecondCounterHiRes() < deadline)
                    process.waitForProcessToFinish(25);
                if (process.isRunning()) {
                    process.kill();
                    process.waitForProcessToFinish(1000);
                } else {
                    // Both Canvas helpers bound their reply below the capacity of the pipe.
                    char reply[3074];
                    const auto count = process.readProcessOutput(reply, sizeof(reply));
                    if (count > 0 && count < int(sizeof(reply)))
                        result = JSON::parse(String::fromUTF8(reply, count));
                }
            }
            if (stop.stop_requested()) return;
            MessageManager::callAsync([this, safe, result, preparing] {
                if (!safe) return;
                safe->prefs().general.updates.setEnabled(true);
                const auto state = result.getProperty("status", {}).toString();
                if (!preparing && state == "available") {
                    const auto release = result.getProperty("release", {});
                    const auto version = release.getProperty("version", {}).toString();
                    if (!mu_pilot::parseReleaseTag(("mu2000-v" + version).toStdString())) {
                        message("Check for Updates", "The updater returned an invalid version.");
                        return;
                    }
                    AlertWindow::showAsync(MessageBoxOptions().withTitle("MU2000 " + version + " Is Available")
                        .withMessage("Download the verified installer? Save your work and unload MU2000 from audio hosts before installing.")
                        .withButton("Download and Open Installer", 1)
                        .withButton(MessageBoxButtonOptions("Later", 0).withRole(MessageBoxButtonRole::cancel)),
                        [this, safe, release](int answer) { if (safe && answer == 1) checkForUpdates(release); });
                    return;
                }
                if (scbridge::shouldShowUpdateResult(true, preparing, state.isEmpty(), state.toStdString()))
                    message("Check for Updates", result.getProperty("message",
                        "The update did not complete. No files were replaced.").toString());
            });
        });
    }
    bool choosing = false, exporting = false;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<ExportJob> exportJob;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<juce::StandalonePluginHolder> holder;
    std::unique_ptr<Window> window;
    std::unique_ptr<mu_pilot::AudioTestPhrase> testPhrase;
    AudioDeviceManager::LevelMeter::Ptr testMeter;
    double testPeak = 0;
    std::unique_ptr<scbridge::MenuBarStatus> status;
    std::jthread updateCheck;
#if JUCE_WINDOWS
    ApplicationCommandManager commands;
    std::unique_ptr<scbridge::WindowsMenuBar> windowsMenu;
    HWND mnemonicWindow = nullptr;
    char consumedMnemonic = 0;
#endif
    std::array<Receiver, 4> receivers;
    std::array<std::unique_ptr<juce::MidiInput>, 4> inputs;
};
}
JUCE_CREATE_APPLICATION_DEFINE(Application)
