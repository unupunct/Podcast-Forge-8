#pragma once
// Live UI preferences shared by views (set at start-up from AppSettings and by the Settings page).
#include <atomic>

namespace pf8::ui {

struct UiPrefs
{
    // false (default): the mixer shows only channels with a microphone assigned, the HEADPHONES tab
    // only channels with headphones assigned. true: always all eight.
    std::atomic<bool> showAllChannels{false};
};

inline UiPrefs& uiPrefs()
{
    static UiPrefs prefs;
    return prefs;
}

} // namespace pf8::ui
