#include "project/Hotkeys.h"

#include <algorithm>
#include <cctype>

namespace pf8 {
namespace {

// Windows virtual-key codes (kept here so the model has no <windows.h> dependency).
constexpr int kVkSpace = 0x20, kVkF1 = 0x70, kVkNum0 = 0x60, kVkReturn = 0x0D, kVkTab = 0x09, kVkEscape = 0x1B;
constexpr int kVkInsert = 0x2D, kVkDelete = 0x2E, kVkHome = 0x24, kVkEnd = 0x23, kVkPgUp = 0x21, kVkPgDn = 0x22;
constexpr int kVkPause = 0x13, kVkMinus = 0xBD, kVkPlus = 0xBB;

struct Named { const char* name; int vk; };
constexpr Named kNamed[] = {{"Space", kVkSpace},   {"Enter", kVkReturn}, {"Tab", kVkTab},       {"Esc", kVkEscape},
                            {"Insert", kVkInsert}, {"Delete", kVkDelete}, {"Home", kVkHome},    {"End", kVkEnd},
                            {"PageUp", kVkPgUp},   {"PageDown", kVkPgDn}, {"Pause", kVkPause},  {"Minus", kVkMinus},
                            {"Plus", kVkPlus}};

std::string lower(std::string_view s)
{
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string trim(std::string_view s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::optional<int> parseKey(const std::string& k)
{
    const std::string l = lower(k);
    if (l.empty()) return std::nullopt;
    for (const auto& n : kNamed)
        if (l == lower(n.name)) return n.vk;
    if (l.size() == 1)
    {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(l[0])));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return static_cast<int>(c);
        return std::nullopt;
    }
    if (l[0] == 'f' && l.size() <= 3 && std::all_of(l.begin() + 1, l.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
    {
        const int n = std::stoi(l.substr(1));
        if (n >= 1 && n <= 24) return kVkF1 + n - 1;
        return std::nullopt;
    }
    for (const char* prefix : {"num ", "num", "numpad ", "numpad"})
        if (l.rfind(prefix, 0) == 0)
        {
            const std::string d = l.substr(std::char_traits<char>::length(prefix));
            if (d.size() == 1 && std::isdigit(static_cast<unsigned char>(d[0]))) return kVkNum0 + (d[0] - '0');
        }
    return std::nullopt;
}

std::string keyName(int vk)
{
    for (const auto& n : kNamed)
        if (n.vk == vk) return n.name;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
    if (vk >= kVkF1 && vk < kVkF1 + 24) return "F" + std::to_string(vk - kVkF1 + 1);
    if (vk >= kVkNum0 && vk <= kVkNum0 + 9) return "Num " + std::to_string(vk - kVkNum0);
    return "VK" + std::to_string(vk);
}

const char* coughModeId(CoughMode m)
{
    switch (m)
    {
        case CoughMode::PushToMute: return "push-to-mute";
        case CoughMode::PushToTalk: return "push-to-talk";
        case CoughMode::Toggle: return "toggle";
    }
    return "push-to-mute";
}

} // namespace

std::string actionId(HotkeyAction a)
{
    switch (a)
    {
        case HotkeyAction::Record: return "record";
        case HotkeyAction::Stop: return "stop";
        case HotkeyAction::Pause: return "pause";
        case HotkeyAction::Marker: return "marker";
        case HotkeyAction::RecordToggle: return "record-toggle";
        case HotkeyAction::Talkback: return "talkback";
        case HotkeyAction::StopAllCarts: return "carts.stop-all";
        case HotkeyAction::MusicPlayPause: return "music.play-pause";
        default: break;
    }
    if (const int c = coughIndex(a); c >= 0) return "cough." + std::to_string(c + 1);
    if (const int c = cartIndex(a); c >= 0) return "cart." + std::to_string(c + 1);
    return "unknown";
}

std::optional<HotkeyAction> actionFromId(std::string_view id)
{
    for (int i = 0; i < kHotkeyActions; ++i)
        if (actionId(static_cast<HotkeyAction>(i)) == id) return static_cast<HotkeyAction>(i);
    return std::nullopt;
}

std::string actionLabel(HotkeyAction a)
{
    switch (a)
    {
        case HotkeyAction::Record: return "Record / resume";
        case HotkeyAction::Stop: return "Stop recording";
        case HotkeyAction::Pause: return "Pause / resume";
        case HotkeyAction::Marker: return "Add marker";
        case HotkeyAction::RecordToggle: return "Record start / stop";
        case HotkeyAction::Talkback: return "Talkback";
        case HotkeyAction::StopAllCarts: return "Stop all carts";
        case HotkeyAction::MusicPlayPause: return "Music play / pause";
        default: break;
    }
    if (const int c = coughIndex(a); c >= 0) return "Cough CH " + std::to_string(c + 1);
    if (const int c = cartIndex(a); c >= 0) return "Cart " + std::to_string(c + 1);
    return "?";
}

bool isHoldAction(HotkeyAction a) noexcept { return a == HotkeyAction::Talkback || coughIndex(a) >= 0; }

std::optional<KeyChord> parseChord(std::string_view text)
{
    KeyChord c;
    std::string rest = trim(text);
    if (rest.empty()) return std::nullopt;
    // Split on '+', but a trailing "+" alone is the Plus key ("Ctrl++").
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i < rest.size(); ++i)
        if (rest[i] == '+' && i > start)
        {
            parts.push_back(trim(std::string_view(rest).substr(start, i - start)));
            start = i + 1;
        }
    parts.push_back(trim(std::string_view(rest).substr(start)));
    for (size_t i = 0; i + 1 < parts.size(); ++i)
    {
        const std::string m = lower(parts[i]);
        if (m == "ctrl" || m == "control") c.ctrl = true;
        else if (m == "shift") c.shift = true;
        else if (m == "alt") c.alt = true;
        else return std::nullopt;
    }
    std::string key = parts.back();
    if (key == "+") key = "Plus";
    const auto vk = parseKey(key);
    if (!vk) return std::nullopt;
    c.vk = *vk;
    return c;
}

std::string formatChord(const KeyChord& c)
{
    if (c.empty()) return {};
    std::string s;
    if (c.ctrl) s += "Ctrl+";
    if (c.shift) s += "Shift+";
    if (c.alt) s += "Alt+";
    return s + keyName(c.vk);
}

HotkeyConfig HotkeyConfig::defaults()
{
    HotkeyConfig h;
    auto add = [&](HotkeyAction a, const char* chord) { h.bindings.push_back({a, *parseChord(chord), false}); };
    add(HotkeyAction::Record, "F9");
    add(HotkeyAction::Stop, "F10");
    add(HotkeyAction::Pause, "F11");
    add(HotkeyAction::Marker, "F12");
    add(HotkeyAction::RecordToggle, "Space");
    add(HotkeyAction::Talkback, "F8");
    for (int ch = 0; ch < 8; ++ch) h.bindings.push_back({coughAction(ch), *parseChord("Num " + std::to_string(ch + 1)), false});
    h.coughModes.fill(CoughMode::PushToMute);
    return h;
}

const Binding* HotkeyConfig::find(HotkeyAction a) const
{
    for (const auto& b : bindings)
        if (b.action == a) return &b;
    return nullptr;
}

void HotkeyConfig::set(HotkeyAction a, KeyChord chord, bool global)
{
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [a](const Binding& b) { return b.action == a; }), bindings.end());
    if (!chord.empty()) bindings.push_back({a, chord, global});
}

std::vector<std::pair<HotkeyAction, HotkeyAction>> HotkeyConfig::conflicts() const
{
    std::vector<std::pair<HotkeyAction, HotkeyAction>> out;
    for (size_t i = 0; i < bindings.size(); ++i)
        for (size_t j = i + 1; j < bindings.size(); ++j)
            if (!bindings[i].chord.empty() && bindings[i].chord == bindings[j].chord) out.emplace_back(bindings[i].action, bindings[j].action);
    return out;
}

json::Value HotkeyConfig::toJson() const
{
    json::Object root;
    json::Array arr;
    for (const auto& b : bindings)
    {
        json::Object o;
        o["action"] = actionId(b.action);
        o["key"] = formatChord(b.chord);
        o["global"] = b.global;
        arr.emplace_back(std::move(o));
    }
    root["bindings"] = std::move(arr);
    json::Array modes;
    for (auto m : coughModes) modes.emplace_back(coughModeId(m));
    root["coughModes"] = std::move(modes);
    return json::Value(std::move(root));
}

HotkeyConfig HotkeyConfig::fromJson(const json::Value& v)
{
    HotkeyConfig h = defaults();
    if (!v.isObject()) return h;
    if (v["bindings"].isArray())
    {
        h.bindings.clear();
        for (const auto& b : v["bindings"].asArray())
        {
            const auto a = actionFromId(b["action"].asString());
            const auto c = parseChord(b["key"].asString());
            if (a && c) h.bindings.push_back({*a, *c, b["global"].asBool(false)});
        }
    }
    const auto& modes = v["coughModes"].asArray();
    for (size_t i = 0; i < h.coughModes.size() && i < modes.size(); ++i)
    {
        const auto s = modes[i].asString();
        h.coughModes[i] = s == "push-to-talk" ? CoughMode::PushToTalk : s == "toggle" ? CoughMode::Toggle : CoughMode::PushToMute;
    }
    return h;
}

bool HotkeyDispatcher::keyDown(const KeyChord& chord)
{
    const bool repeat = std::find(down_.begin(), down_.end(), chord.vk) != down_.end();
    const Binding* hit = nullptr;
    for (const auto& b : bindings_)
        if (b.chord == chord) hit = &b;
    if (!hit)
    {
        // A hold key keeps holding through repeats even if modifiers changed meanwhile.
        return repeat && std::any_of(held_.begin(), held_.end(), [&](const auto& h) { return h.first == chord.vk; });
    }
    if (repeat) return true; // consumed, but auto-repeat never re-triggers
    down_.push_back(chord.vk);
    if (isHoldAction(hit->action)) held_.emplace_back(chord.vk, hit->action);
    if (onAction) onAction(hit->action, true);
    return true;
}

bool HotkeyDispatcher::keyUp(int vk)
{
    const bool wasDown = std::find(down_.begin(), down_.end(), vk) != down_.end();
    down_.erase(std::remove(down_.begin(), down_.end(), vk), down_.end());
    bool consumed = false;
    for (auto it = held_.begin(); it != held_.end();)
        if (it->first == vk)
        {
            const auto a = it->second;
            it = held_.erase(it);
            if (onAction) onAction(a, false);
            consumed = true;
        }
        else
            ++it;
    return consumed || wasDown;
}

void HotkeyDispatcher::releaseAll()
{
    auto held = std::move(held_);
    held_.clear();
    down_.clear();
    for (const auto& [vk, a] : held)
    {
        if (onCancel) onCancel(a);
        else if (onAction) onAction(a, false);
    }
}

void HotkeyDispatcher::cancelKey(int vk)
{
    down_.erase(std::remove(down_.begin(), down_.end(), vk), down_.end());
    for (auto it = held_.begin(); it != held_.end();)
        if (it->first == vk)
        {
            const auto a = it->second;
            it = held_.erase(it);
            if (onCancel) onCancel(a);
            else if (onAction) onAction(a, false);
        }
        else
            ++it;
}

} // namespace pf8
