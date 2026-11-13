#pragma once

#include "ControlShortcuts.h"
#include "KeyboardShortcut.h"

namespace mu_pilot {
#if JUCE_WINDOWS
inline char controlMnemonic(const juce::KeyPress& key) {
    return key.getModifiers().withoutMouseButtons() == juce::ModifierKeys(juce::ModifierKeys::altModifier)
        ? hs2port::mnemonicLetter(key) : 0;
}

inline bool useMnemonic(juce::Component& control, char key) {
    if (!control.isShowing() || !control.isEnabled()) return false;
    const auto expected = "Alt+" + juce::String::charToString(juce::CharacterFunctions::toUpperCase(juce::juce_wchar(key)));
    if (control.getProperties()["hs2AccessKey"].toString() != expected) return false;
    control.grabKeyboardFocus();
    if (auto* button = dynamic_cast<juce::Button*>(&control)) button->triggerClick();
    return true;
}
#endif
}
