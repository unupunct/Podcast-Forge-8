#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "engine/EngineController.h"
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
    DeviceListView& deviceList() noexcept { return devices_; }

private:
    TopBar topBar_;
    DeviceListView devices_;
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
