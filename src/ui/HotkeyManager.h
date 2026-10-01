#pragma once
// Windows side of the hotkeys (ARCHITECTURE.md §7, brief §27).
//
//  * Focused: a WH_KEYBOARD hook on the UI thread sees key down / up / repeat while Podcast Forge 8
//    is the active app. Keys are ignored while a text field or a modal dialog has focus (key-ups
//    are always processed, so a hold can never get stuck).
//  * Global press actions (F9 … with "global" on): RegisterHotKey on a message-only window.
//  * Global hold actions (talkback / cough with "global" on): a WH_KEYBOARD_LL hook, installed only
//    while such a binding exists; it never swallows keys and ignores them while our app is active.
// Only virtual-key codes are compared; keystrokes are never stored or logged.
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "engine/EngineController.h"
#include "project/Hotkeys.h"
#include "routing/CoughMute.h"

namespace pf8 {
class SettingsDb;
}

namespace pf8::ui {

class HotkeyManager : private juce::Timer
{
public:
    struct Transport
    {
        std::function<void()> record, stop, pause, marker;
        std::function<bool()> idle; // no recording running
    };

    HotkeyManager(EngineController& controller, SettingsDb* db, Transport transport);
    ~HotkeyManager() override;

    const HotkeyConfig& config() const noexcept { return config_; }
    void setConfig(const HotkeyConfig& c); // applies and saves
    // Global bindings Windows refused (another app owns the chord), for the settings page.
    const std::vector<std::string>& registrationErrors() const noexcept { return regErrors_; }

    // Entry points from the Win32 hooks (UI thread).
    bool onFocusedKey(int vk, bool down, bool repeat);
    void onGlobalHotkey(int id);
    void onGlobalKey(int vk, bool down);

private:
    void timerCallback() override;
    void handle(HotkeyAction a, bool down);
    void rebuild();
    std::vector<Binding> cartBindings() const;
    void installHooks();
    void removeHooks();
    KeyChord chordFor(int vk) const;

    EngineController& controller_;
    SettingsDb* db_;
    Transport transport_;
    HotkeyConfig config_;
    std::array<CoughMute, 8> coughs_;
    HotkeyDispatcher focused_, global_;
    std::vector<Binding> registered_; // global press bindings, id = index + 1
    std::vector<std::string> regErrors_;
    std::string cartKeys_;            // last seen cart hotkey strings
    bool wasForeground_ = true;
};

} // namespace pf8::ui
