#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "engine/EngineController.h"
#include "ui/DeviceListView.h"
#include "ui/DeviceMatrixView.h"
#include "ui/DspEditor.h"
#include "ui/HeadphonesView.h"
#include "ui/MarkersView.h"
#include "ui/MicWizard.h"
#include "ui/SoundboardView.h"
#include "ui/TransportBar.h"
#include "ui/LookAndFeel.h"
#include "ui/MixerView.h"
#include "ui/RoutingGridView.h"
#include "ui/TopBar.h"

namespace pf8::ui {

// MIXER page: the console on top, the bottom dock (ROUTING, and later HEADPHONES, SOUNDBOARD,
// MUSIC, MARKERS) below.
class MixerPage : public juce::Component
{
public:
    explicit MixerPage(EngineController& controller);
    void resized() override;
    MixerView& mixer() noexcept { return mixer_; }
    juce::TabbedComponent& dock() noexcept { return dock_; }

private:
    MixerView mixer_;
    juce::TabbedComponent dock_{juce::TabbedButtonBar::TabsAtTop};
};

// Opens (or brings to front) the per-channel DSP editor and mic wizard windows.
class ChannelWindows
{
public:
    explicit ChannelWindows(EngineController& c) : controller_(c) {}
    ~ChannelWindows();
    void openDspEditor(int channel);
    void openWizard(int channel);

private:
    EngineController& controller_;
    std::array<juce::Component::SafePointer<juce::DialogWindow>, kNumChannels> dsp_{}, wizard_{};
};

class MainComponent : public juce::Component
{
public:
    explicit MainComponent(EngineController& controller);
    void resized() override;
    void paint(juce::Graphics&) override;

    TopBar& topBar() noexcept { return topBar_; }
    TransportBar& transport() noexcept { return transport_; }
    juce::TabbedComponent& tabs() noexcept { return tabs_; }
    void offerRecovery(); // interrupted sessions found at start-up

private:
    EngineController& controller_;
    TopBar topBar_;
    TransportBar transport_;
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
    ChannelWindows windows_;
};

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& title, EngineController& controller);
    ~MainWindow() override;
    void closeButtonPressed() override;

private:
    LookAndFeel lookAndFeel_;
    juce::TooltipWindow tooltips_{nullptr, 600};
};

} // namespace pf8::ui
