#pragma once
// One vertical console channel strip (ARCHITECTURE/ROUTING/DSP params bound directly).
#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

class ChannelStripView : public juce::Component, public LayoutSelfCheck
{
public:
    ChannelStripView(EngineController& controller, int channel);

    // Called by MixerView at ~30 Hz with the latest controller status and meters.
    void update(const ChannelView& view, const EngineMeters& meters);

    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;

    std::function<void(int channel)> onConfigure; // "Configure Channel" (mic wizard, Stage 5)

private:
    void syncFromParams(); // reflect externally changed parameters (project load, hotkeys)

    EngineController& controller_;
    const int channel_;
    juce::Label number_, name_, input_;
    StatusLed inputLed_;
    Knob gain_;
    ToggleLed gate_{"GATE", colours::dsp}, comp_{"COMP", colours::dsp}, eq_{"EQ", colours::dsp},
        deess_{"DE-ESS", colours::dsp};
    Knob pan_;
    ToggleLed mute_{"MUTE", colours::mute}, solo_{"SOLO", colours::solo}, pfl_{"PFL", colours::pfl};
    Fader fader_;
    Meter meter_{1};
    ToggleLed rec_{"REC", colours::record}, mon_{"MON", colours::mon};
    juce::TextButton configure_{"CONFIG"};
    juce::String lastName_;
    std::array<juce::Rectangle<int>, 4> sections_{}; // for separators
};

} // namespace pf8::ui
