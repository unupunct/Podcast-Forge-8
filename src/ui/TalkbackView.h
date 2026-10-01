#pragma once
// TALKBACK dock tab (ROUTING.md §5): talkback mic, key, targets, dim and the program lock.
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <string>
#include <vector>

#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

// Press-and-hold / tap-to-latch button; drives EngineController::talkbackPress/Release.
class TalkButton : public juce::Component
{
public:
    explicit TalkButton(EngineController& controller) : controller_(controller) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    EngineController& controller_;
};

class TalkbackView : public juce::Component, public LayoutSelfCheck, private juce::Timer
{
public:
    explicit TalkbackView(EngineController& controller);
    ~TalkbackView() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;

private:
    void timerCallback() override;
    void showDeviceMenu();
    void refreshSource();

    EngineController& controller_;
    TalkButton talk_;
    juce::ComboBox mode_, source_, inputChannel_;
    juce::TextButton device_{"-- no talkback mic --"};
    juce::Label modeLabel_, sourceLabel_, deviceLabel_, targetsLabel_, deviceState_;
    Knob trim_, level_;
    std::array<ToggleLed, kNumChannels> targets_{
        ToggleLed{"HP1", colours::accent}, ToggleLed{"HP2", colours::accent}, ToggleLed{"HP3", colours::accent}, ToggleLed{"HP4", colours::accent},
        ToggleLed{"HP5", colours::accent}, ToggleLed{"HP6", colours::accent}, ToggleLed{"HP7", colours::accent}, ToggleLed{"HP8", colours::accent}};
    juce::TextButton all_{"ALL"}, none_{"NONE"};
    ToggleLed dim_{"DIM -12 dB", colours::ok}, toProgram_{"TO PROGRAM", colours::warn};
    float peak_ = 0.0f;
    juce::Rectangle<int> meter_, lockNote_;
    int lastChannels_ = -1;
};

} // namespace pf8::ui
