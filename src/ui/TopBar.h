#pragma once
// TOP BAR: Project | Backend | Sample rate | Buffer | CPU | Load | Disk | Devices | Record status.
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/SystemStats.h"
#include "engine/EngineController.h"

namespace pf8::ui {

class TopBar : public juce::Component, private juce::Timer
{
public:
    explicit TopBar(EngineController& controller);
    ~TopBar() override;

    void paint(juce::Graphics&) override;
    void refresh(); // pulls status now (also called by the 10 Hz timer)

    struct Field
    {
        juce::String caption;
        juce::String value;
        juce::Colour colour;
    };
    const std::vector<Field>& fields() const noexcept { return fields_; }

private:
    void timerCallback() override { refresh(); }

    EngineController& controller_;
    CpuMeter cpu_;
    std::vector<Field> fields_;
    int tickCount_ = 0;
    double cpuPercent_ = 0.0;
};

} // namespace pf8::ui
