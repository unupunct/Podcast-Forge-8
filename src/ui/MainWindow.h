#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "engine/EngineController.h"
#include "ui/DeviceListView.h"
#include "ui/DeviceMatrixView.h"
#include "ui/DspEditor.h"
#include "ui/HotkeyManager.h"
#include "ui/ProjectController.h"
#include "ui/HeadphonesView.h"
#include "ui/MarkersView.h"
#include "ui/MicWizard.h"
#include "ui/MusicView.h"
#include "ui/SoundboardView.h"
#include "ui/TalkbackView.h"
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
    // Quit flow: a running recording is stopped and finalised only after confirmation, unsaved
    // project changes are offered for saving; then `quit` runs.
    void requestQuit(std::function<void()> quit);
    void shutdown(); // clean exit bookkeeping (crash-restore flag)

private:
    EngineController& controller_;
    std::unique_ptr<ProjectController> project_;
    std::unique_ptr<HotkeyManager> hotkeys_;
    bool quitting_ = false;
    LookAndFeel lookAndFeel_;
    juce::TooltipWindow tooltips_{nullptr, 600};
};

} // namespace pf8::ui
