# Match the HS2 Windows provider: expose shortcuts separately from names and help.
set(provider "${juce_SOURCE_DIR}/modules/juce_gui_basics/native/accessibility/juce_AccessibilityElement_windows.cpp")
file(READ "${provider}" contents)
set(marker "case UIA_AcceleratorKeyPropertyId:")
set(anchor "            case UIA_HelpTextPropertyId:")
string(FIND "${contents}" "${marker}" patched)
if(patched EQUAL -1)
    string(FIND "${contents}" "${anchor}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Review the JUCE Windows accessibility shortcut patch for this version")
    endif()
    set(shortcuts [=[
            case UIA_AcceleratorKeyPropertyId:
            case UIA_AccessKeyPropertyId:
            {
                const auto key = propertyId == UIA_AcceleratorKeyPropertyId ? "hs2AcceleratorKey" : "hs2AccessKey";
                const auto shortcut = accessibilityHandler.getComponent().getProperties()[key].toString();
                if (shortcut.isNotEmpty())
                    VariantHelpers::setString (shortcut, pRetVal);
                break;
            }

]=])
    string(REPLACE "${anchor}" "${shortcuts}${anchor}" contents "${contents}")
    file(CONFIGURE OUTPUT "${provider}" CONTENT "${contents}" @ONLY)
elseif(NOT contents MATCHES "hs2AccessKey" OR NOT contents MATCHES "hs2AcceleratorKey")
    message(FATAL_ERROR "JUCE already provides a different Windows shortcut implementation; review before building")
endif()
