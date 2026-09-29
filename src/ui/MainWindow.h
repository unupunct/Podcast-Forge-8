#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "engine/EngineController.h"
#include "ui/ChannelsView.h"
#include "ui/DeviceListView.h"
#include "ui/LookAndFeel.h"
#include "ui/TopBar.h"

namespace pf8::ui {

class MainComponent : public juce::Component
{
public:
    explicit MainComponent(EngineController& controller);
    void resized() override;
    void paint(juce::Graphics&) override;

    TopBar& topBar() noexcept { return topBar_; }
    juce::TabbedComponent& tabs() noexcept { return tabs_; }

private:
    TopBar topBar_;
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
};

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& title, EngineController& controller);
    ~MainWindow() override;
    void closeButtonPressed() override;

private:
    LookAndFeel lookAndFeel_;
};

} // namespace pf8::ui
