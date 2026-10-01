#pragma once
// MUSIC dock tab: playlist, transport, volume, automatic ducking (brief §15).
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

class MusicView : public juce::Component, public LayoutSelfCheck, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit MusicView(EngineController& controller);
    ~MusicView() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void timerCallback() override;

    EngineController& controller_;
    juce::ListBox list_{"playlist", this};
    juce::TextButton add_{"ADD..."}, remove_{"REMOVE"}, play_{"PLAY"}, next_{"NEXT"}, fade_{"FADE"}, stop_{"STOP"};
    ToggleLed auto_{"AUTO NEXT", colours::accent}, record_{"REC TRACK", colours::record}, duck_{"DUCKING", colours::ok};
    juce::Slider volume_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
    juce::Label position_, volLabel_;
    Knob threshold_, depth_, release_;
    float duckDb_ = 0.0f;
    juce::Rectangle<int> duckMeter_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::vector<MusicTrackInfo> rows_;
};

} // namespace pf8::ui
