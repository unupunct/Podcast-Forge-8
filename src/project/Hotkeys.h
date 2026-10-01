#pragma once
// Hotkey model (brief §27): actions, key chords, bindings, conflict detection and the key-event
// dispatcher. Pure logic — the Windows plumbing (focused keys, RegisterHotKey, the low-level hook
// for global hold keys) lives in ui/HotkeyManager and feeds HotkeyDispatcher.
//
// Keystrokes are never stored or logged: only virtual-key codes are compared against bindings.
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Json.h"
#include "routing/RoutingTypes.h"

namespace pf8 {

enum class HotkeyAction : uint8_t
{
    Record = 0, // F9 — start (or resume)
    Stop,       // F10 — asks for confirmation
    Pause,      // F11
    Marker,     // F12
    RecordToggle, // Space — start / stop (stop still asks)
    Talkback,     // hold / tap (TalkbackKey)
    Cough1, Cough2, Cough3, Cough4, Cough5, Cough6, Cough7, Cough8,
    Cart1, // … Cart24
    CartLast = Cart1 + 23,
    StopAllCarts,
    MusicPlayPause,
    Count
};
constexpr int kHotkeyActions = static_cast<int>(HotkeyAction::Count);

inline HotkeyAction coughAction(int ch) noexcept { return static_cast<HotkeyAction>(static_cast<int>(HotkeyAction::Cough1) + ch); }
inline HotkeyAction cartAction(int cart) noexcept { return static_cast<HotkeyAction>(static_cast<int>(HotkeyAction::Cart1) + cart); }
inline int coughIndex(HotkeyAction a) noexcept
{
    const int i = static_cast<int>(a) - static_cast<int>(HotkeyAction::Cough1);
    return i >= 0 && i < 8 ? i : -1;
}
inline int cartIndex(HotkeyAction a) noexcept
{
    const int i = static_cast<int>(a) - static_cast<int>(HotkeyAction::Cart1);
    return i >= 0 && i < 24 ? i : -1;
}

std::string actionId(HotkeyAction a);                      // "record", "cough.3", "cart.12"
std::optional<HotkeyAction> actionFromId(std::string_view id);
std::string actionLabel(HotkeyAction a);                   // "Record", "Cough CH 3", "Cart 12"
// Hold actions act on key down *and* key up (talkback, cough); the rest fire on key down.
bool isHoldAction(HotkeyAction a) noexcept;

// A key with modifiers. `vk` is a Windows virtual-key code (0 = unbound).
struct KeyChord
{
    int vk = 0;
    bool ctrl = false, shift = false, alt = false;
    bool empty() const noexcept { return vk == 0; }
    bool operator==(const KeyChord&) const = default;
};

// "Ctrl+Shift+F9", "Space", "A", "5", "Num 3", "F13" … (case-insensitive). nullopt when invalid.
std::optional<KeyChord> parseChord(std::string_view text);
std::string formatChord(const KeyChord& c);

struct Binding
{
    HotkeyAction action = HotkeyAction::Record;
    KeyChord chord;
    bool global = false; // also works while another application has focus
    bool operator==(const Binding&) const = default;
};

struct HotkeyConfig
{
    std::vector<Binding> bindings;
    std::array<CoughMode, 8> coughModes{};

    static HotkeyConfig defaults();
    const Binding* find(HotkeyAction a) const;
    void set(HotkeyAction a, KeyChord chord, bool global);
    // Pairs of actions bound to the same chord (the same chord may never trigger two actions).
    std::vector<std::pair<HotkeyAction, HotkeyAction>> conflicts() const;
    bool operator==(const HotkeyConfig&) const = default;

    json::Value toJson() const;
    static HotkeyConfig fromJson(const json::Value& v); // unknown entries are skipped; missing → defaults
};

// Turns key events into actions. A hold action remembers the key that started it, so releasing the
// key ends it even if a modifier was released first; a key's auto-repeat never re-triggers.
class HotkeyDispatcher
{
public:
    std::function<void(HotkeyAction, bool down)> onAction;
    // Called instead of onAction(a, false) when a hold ends without a real key-up (releaseAll,
    // a key found physically up): talkback must not count it as a tap. Falls back to onAction.
    std::function<void(HotkeyAction)> onCancel;
    // Keys currently held for hold actions (to check against the physical keyboard).
    std::vector<int> heldKeys() const
    {
        std::vector<int> k;
        for (const auto& h : held_) k.push_back(h.first);
        return k;
    }
    void cancelKey(int vk); // a held key found up without its key-up event

    // `bindings`: the active set (focused: all; global hook: the global ones). Cart chords from the
    // soundboard are merged in by the caller.
    void setBindings(std::vector<Binding> bindings) { bindings_ = std::move(bindings); }
    const std::vector<Binding>& bindings() const noexcept { return bindings_; }

    // Returns true when the key was consumed by a binding.
    bool keyDown(const KeyChord& chord);
    bool keyUp(int vk);
    void releaseAll(); // focus lost / app deactivated: end every hold

private:
    std::vector<Binding> bindings_;
    std::vector<std::pair<int, HotkeyAction>> held_; // vk → hold action in progress
    std::vector<int> down_;                          // keys currently down (repeat suppression)
};

} // namespace pf8
