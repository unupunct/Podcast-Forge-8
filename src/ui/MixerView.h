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

    // Which channels get a strip: those with a microphone assigned (an unplugged one stays, shown
    // OFFLINE), all eight when none is assigned yet or when "Show all 8 channels" is on.
    static std::array<bool, kNumChannels> visibleChannels(const ControllerStatus& s, bool showAll);
    int visibleCount() const;

private:
    void timerCallback() override { refresh(); }

    EngineController& controller_;
    std::array<std::unique_ptr<ChannelStripView>, kNumChannels> strips_;
    std::array<bool, kNumChannels> visible_{};
    MasterStripView master_;
};

} // namespace pf8::ui
