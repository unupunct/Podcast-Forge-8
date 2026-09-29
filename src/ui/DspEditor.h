#pragma once
// Channel DSP editor (CONFIG): input, HPF, gate, 4-band EQ with response curve, de-esser,
// compressor, limiter, reverb send, presets, and the microphone setup wizard entry point.
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

class EqCurve : public juce::Component
{
public:
    explicit EqCurve(ChannelDspParams& p) : p_(p) {}
    void paint(juce::Graphics&) override;

private:
    ChannelDspParams& p_;
};

// A titled group of controls with an on/off toggle.
class DspSection : public juce::Component
{
public:
    DspSection(juce::String title, std::atomic<bool>* enable);
    void addKnob(Knob& k);
    void resized() override;
    void paint(juce::Graphics&) override;
    void sync();
    juce::Component* extra = nullptr; // optional component laid out after the knobs (e.g. curve)
    int extraWeight = 0;
    std::function<void(juce::Graphics&, juce::Rectangle<int>)> drawActivity;

private:
    juce::String title_;
    std::atomic<bool>* enable_;
    ToggleLed toggle_;
    std::vector<Knob*> knobs_;
    juce::Rectangle<int> activity_;
};

class DspEditor : public juce::Component, public LayoutSelfCheck, private juce::Timer
{
public:
    DspEditor(EngineController& controller, int channel);
    ~DspEditor() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;
    std::function<void(int channel)> onRunWizard;

private:
    void timerCallback() override;
    void syncFromParams();
    Knob& knob(const char* caption, double min, double max, double def, std::function<juce::String(double)> fmt,
               AtomicParam& param, double skewMid = 0.0);

    EngineController& controller_;
    const int channel_;
    ChannelDspParams& p_;
    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::pair<Knob*, AtomicParam*>> bindings_;
    std::unique_ptr<DspSection> input_, hpf_, gate_, eq_, deess_, comp_, limiter_, reverb_;
    EqCurve curve_;
    juce::Label title_;
    juce::ComboBox compPreset_, eqPreset_;
    juce::TextButton wizard_{"MIC SETUP WIZARD..."};
    ToggleLed polarity_{"POLARITY", colours::warn}, hpf24_{"24 dB", colours::dsp}, autoMakeup_{"AUTO", colours::dsp};
    std::array<juce::ComboBox, 4> bandType_;
    dsp::StripMeters meters_;
};

} // namespace pf8::ui
