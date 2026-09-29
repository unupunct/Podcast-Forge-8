#pragma once
// Eight channel strips + MASTER, refreshed together at 30 Hz from one status/meter snapshot.
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "engine/EngineController.h"
#include "ui/ChannelStripView.h"
#include "ui/MasterStripView.h"

namespace pf8::ui {

class MixerView : public juce::Component, private juce::Timer
{
public:
    explicit MixerView(EngineController& controller);
    ~MixerView() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh(); // pull status/meters now

    ChannelStripView& strip(int i) { return *strips_[static_cast<size_t>(i)]; }
    std::function<void(int channel)> onConfigureChannel;

private:
    void timerCallback() override { refresh(); }

    EngineController& controller_;
    std::array<std::unique_ptr<ChannelStripView>, kNumChannels> strips_;
    MasterStripView master_;
};

} // namespace pf8::ui
