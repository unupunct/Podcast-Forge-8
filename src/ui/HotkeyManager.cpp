#include "ui/HotkeyManager.h"

#include <windows.h>

#include "core/Log.h"
#include "core/SettingsDb.h"

namespace pf8::ui {
namespace {

constexpr const char* kKeyHotkeys = "hotkeys";
HotkeyManager* gInstance = nullptr; // one per process (hooks are process-wide)
HHOOK gThreadHook = nullptr, gLowLevelHook = nullptr;
HWND gMessageWindow = nullptr;

bool isModifier(int vk)
{
    switch (vk)
    {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN: return true;
        default: return false;
    }
}

bool ourAppIsForeground()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

LRESULT CALLBACK threadHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && gInstance)
    {
        const bool up = (lParam & 0x80000000) != 0;
        const bool repeat = !up && (lParam & 0x40000000) != 0;
        if (gInstance->onFocusedKey(static_cast<int>(wParam), !up, repeat)) return 1; // consumed
    }
    return CallNextHookEx(gThreadHook, code, wParam, lParam);
}

LRESULT CALLBACK lowLevelHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && gInstance)
    {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
        if ((down || up) && !(k->flags & LLKHF_INJECTED) && !ourAppIsForeground()) gInstance->onGlobalKey(static_cast<int>(k->vkCode), down);
    }
    return CallNextHookEx(gLowLevelHook, code, wParam, lParam); // never swallowed
}

LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_HOTKEY && gInstance) gInstance->onGlobalHotkey(static_cast<int>(wParam));
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

UINT winModifiers(const KeyChord& c)
{
    return (c.ctrl ? MOD_CONTROL : 0u) | (c.shift ? MOD_SHIFT : 0u) | (c.alt ? MOD_ALT : 0u) | MOD_NOREPEAT;
}

} // namespace

HotkeyManager::HotkeyManager(EngineController& controller, SettingsDb* db, Transport transport)
    : controller_(controller), db_(db), transport_(std::move(transport))
{
    config_ = HotkeyConfig::defaults();
    if (db_)
        if (auto text = db_->get(kKeyHotkeys))
            if (auto v = json::parse(*text)) config_ = HotkeyConfig::fromJson(*v);
    focused_.onAction = [this](HotkeyAction a, bool down) { handle(a, down); };
    global_.onAction = [this](HotkeyAction a, bool down) { handle(a, down); };
    gInstance = this;
    installHooks();
    rebuild();
    startTimerHz(5);
}

HotkeyManager::~HotkeyManager()
{
    stopTimer();
    focused_.releaseAll();
    global_.releaseAll();
    removeHooks();
    gInstance = nullptr;
}

void HotkeyManager::installHooks()
{
    gThreadHook = SetWindowsHookExW(WH_KEYBOARD, threadHookProc, nullptr, GetCurrentThreadId());
    if (!gThreadHook) PF8_LOG_ERROR("hotkeys", "keyboard hook failed (error %lu)", GetLastError());
    WNDCLASSW wc{};
    wc.lpfnWndProc = messageWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PodcastForge8Hotkeys";
    RegisterClassW(&wc);
    gMessageWindow = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
}

void HotkeyManager::removeHooks()
{
    if (gMessageWindow)
    {
        for (size_t i = 0; i < registered_.size(); ++i) UnregisterHotKey(gMessageWindow, static_cast<int>(i + 1));
        DestroyWindow(gMessageWindow);
        gMessageWindow = nullptr;
    }
    registered_.clear();
    if (gLowLevelHook) UnhookWindowsHookEx(gLowLevelHook), gLowLevelHook = nullptr;
    if (gThreadHook) UnhookWindowsHookEx(gThreadHook), gThreadHook = nullptr;
}

void HotkeyManager::setConfig(const HotkeyConfig& c)
{
    focused_.releaseAll();
    global_.releaseAll();
    config_ = c;
    if (db_) db_->set(kKeyHotkeys, json::serialize(config_.toJson()));
    rebuild();
}

std::vector<Binding> HotkeyManager::cartBindings() const
{
    std::vector<Binding> out;
    auto& sb = controller_.engine().soundboard();
    for (int i = 0; i < kCartCount; ++i)
        if (auto c = parseChord(sb.settings(i).hotkey)) out.push_back({cartAction(i), *c, false});
    return out;
}

void HotkeyManager::rebuild()
{
    for (int ch = 0; ch < 8; ++ch)
    {
        auto& cm = coughs_[static_cast<size_t>(ch)];
        if (cm.mode() != config_.coughModes[static_cast<size_t>(ch)]) cm.setMode(config_.coughModes[static_cast<size_t>(ch)]);
        controller_.engine().routing().channel[static_cast<size_t>(ch)].coughOpen = coughs_[static_cast<size_t>(ch)].open();
    }
    // Global press bindings go to RegisterHotKey (Windows then delivers them even while we are
    // active), so the focused dispatcher must not also see them.
    if (gMessageWindow)
        for (size_t i = 0; i < registered_.size(); ++i) UnregisterHotKey(gMessageWindow, static_cast<int>(i + 1));
    registered_.clear();
    regErrors_.clear();
    std::vector<Binding> focused, globalHolds;
    for (const auto& b : config_.bindings)
    {
        if (b.chord.empty()) continue;
        if (b.global && !isHoldAction(b.action))
        {
            const int id = static_cast<int>(registered_.size() + 1);
            if (gMessageWindow && RegisterHotKey(gMessageWindow, id, winModifiers(b.chord), static_cast<UINT>(b.chord.vk)))
            {
                registered_.push_back(b);
                continue;
            }
            registered_.push_back(Binding{}); // keep ids stable; this slot is unused
            regErrors_.push_back(formatChord(b.chord) + " (" + actionLabel(b.action) + ") is used by another program");
        }
        if (b.global && isHoldAction(b.action)) globalHolds.push_back(b);
        focused.push_back(b);
    }
    for (const auto& b : cartBindings()) focused.push_back(b);
    focused_.setBindings(std::move(focused));
    global_.setBindings(globalHolds);
    if (!globalHolds.empty() && !gLowLevelHook)
    {
        gLowLevelHook = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelHookProc, GetModuleHandleW(nullptr), 0);
        if (!gLowLevelHook) regErrors_.push_back("global hold keys unavailable (keyboard hook refused)");
    }
    else if (globalHolds.empty() && gLowLevelHook)
    {
        UnhookWindowsHookEx(gLowLevelHook);
        gLowLevelHook = nullptr;
    }
    std::string keys;
    for (int i = 0; i < kCartCount; ++i) keys += controller_.engine().soundboard().settings(i).hotkey + "\n";
    cartKeys_ = keys;
    for (const auto& e : regErrors_) PF8_LOG_WARN("hotkeys", "%s", e.c_str());
}

KeyChord HotkeyManager::chordFor(int vk) const
{
    KeyChord c;
    c.vk = vk;
    c.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    c.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    c.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    return c;
}

bool HotkeyManager::onFocusedKey(int vk, bool down, bool repeat)
{
    if (isModifier(vk)) return false;
    if (!down) return focused_.keyUp(vk);
    // Typing in a text field or answering a dialog is never a hotkey.
    if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0) return false;
    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr) return false;
    (void)repeat; // the dispatcher suppresses repeats itself (and still consumes them)
    return focused_.keyDown(chordFor(vk));
}

void HotkeyManager::onGlobalHotkey(int id)
{
    if (id < 1 || static_cast<size_t>(id) > registered_.size()) return;
    const auto& b = registered_[static_cast<size_t>(id - 1)];
    if (b.chord.empty()) return;
    if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0 && ourAppIsForeground()) return;
    handle(b.action, true);
}

void HotkeyManager::onGlobalKey(int vk, bool down)
{
    if (isModifier(vk)) return;
    if (down) global_.keyDown(chordFor(vk));
    else global_.keyUp(vk);
}

void HotkeyManager::handle(HotkeyAction a, bool down)
{
    auto& e = controller_.engine();
    if (const int ch = coughIndex(a); ch >= 0)
    {
        auto& cm = coughs_[static_cast<size_t>(ch)];
        if (down) cm.keyDown();
        else cm.keyUp();
        e.routing().channel[static_cast<size_t>(ch)].coughOpen = cm.open();
        return;
    }
    if (a == HotkeyAction::Talkback)
    {
        if (down) controller_.talkbackPress();
        else controller_.talkbackRelease();
        return;
    }
    if (!down) return;
    if (const int cart = cartIndex(a); cart >= 0)
    {
        if (e.soundboard().buffer(cart)) e.soundboard().play(cart);
        return;
    }
    switch (a)
    {
        case HotkeyAction::Record: if (transport_.record) transport_.record(); break;
        case HotkeyAction::Stop: if (transport_.stop) transport_.stop(); break;
        case HotkeyAction::Pause: if (transport_.pause) transport_.pause(); break;
        case HotkeyAction::Marker: if (transport_.marker) transport_.marker(); break;
        case HotkeyAction::RecordToggle:
            if (transport_.idle && transport_.idle()) { if (transport_.record) transport_.record(); }
            else if (transport_.stop) transport_.stop();
            break;
        case HotkeyAction::StopAllCarts: e.soundboard().stopAll(); break;
        case HotkeyAction::MusicPlayPause:
        {
            auto& mp = e.music();
            const auto st = mp.status();
            if (st.playing) mp.pause();
            else if (st.paused && st.index >= 0) mp.resume();
            else mp.play(0);
            break;
        }
        default: break;
    }
}

void HotkeyManager::timerCallback()
{
    // Losing focus ends every focused hold (a cough or talkback key can never stay stuck).
    const bool fg = ourAppIsForeground();
    if (wasForeground_ && !fg) focused_.releaseAll();
    if (!wasForeground_ && fg) global_.releaseAll();
    wasForeground_ = fg;
    // Cart hotkeys are edited on the soundboard: pick up changes.
    std::string keys;
    for (int i = 0; i < kCartCount; ++i) keys += controller_.engine().soundboard().settings(i).hotkey + "\n";
    if (keys != cartKeys_) rebuild();
}

} // namespace pf8::ui
