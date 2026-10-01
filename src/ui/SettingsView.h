#pragma once
// SETTINGS tab (brief §31): Audio & performance, Devices, Routing, Recording & projects, DSP,
// Hotkeys, Appearance, Diagnostics. Every option here acts on something real; audio engine
// options are saved and take effect at the next start (the page says so).
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/EngineController.h"
#include "project/AppSettings.h"
#include "project/Hotkeys.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

// Hotkey access for the page (the HotkeyManager lives in the main window; absent in --verify-ui).
struct HotkeyAccess
{
    std::function<HotkeyConfig()> get;
    std::function<void(const HotkeyConfig&)> set;
    std::function<std::vector<std::string>()> errors;
};

class SettingsPage;

class SettingsView : public juce::Component, public LayoutSelfCheck, private juce::ListBoxModel
{
public:
    explicit SettingsView(EngineController& controller);
    ~SettingsView() override;
    void setHotkeyAccess(HotkeyAccess access);
    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;
    int pageCount() const noexcept { return static_cast<int>(pages_.size()); }
    void showPage(int index);

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int row) override;

    EngineController& controller_;
    juce::ListBox list_{"settings-pages", this};
    std::vector<std::unique_ptr<SettingsPage>> pages_;
    juce::Viewport viewport_;
    std::unique_ptr<HotkeyAccess> hotkeyAccess_; // shared with the Hotkeys page
    int current_ = 0;
};

} // namespace pf8::ui
