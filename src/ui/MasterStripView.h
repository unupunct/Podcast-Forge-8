#pragma once
// MASTER section: program meter + fader, limiter, mute; MONITOR: source, output device, volume,
// dim, mono, mute.
#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

class MasterStripView : public juce::Component, public LayoutSelfCheck
{
public:
    explicit MasterStripView(EngineController& controller);
    void update(const ControllerStatus& status, const EngineMeters& meters);
    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;

private:
    void rebuildOutputs();

    EngineController& controller_;
    juce::Label title_, monTitle_;
    Meter meter_{2};
    Fader fader_;
    ToggleLed limiter_{"LIMITER", colours::dsp}, mute_{"MUTE", colours::mute};
    juce::ComboBox monSource_, monOutput_;
    Knob monVolume_;
    ToggleLed dim_{"DIM", colours::warn}, mono_{"MONO", colours::accent}, monMute_{"MUTE", colours::mute};
    juce::Label monStatus_;
    std::vector<std::string> outputIds_;
    uint64_t registryGeneration_ = ~0ull;
    std::string assignedMonitor_;
    juce::Rectangle<int> monSection_;
};

} // namespace pf8::ui
