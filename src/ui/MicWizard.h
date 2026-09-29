#pragma once
// Microphone setup wizard (DSP.md §4): detect → noise floor (5 s silence) → speech (10 s) →
// results and recommendations → Apply (software settings only; hardware gain is never changed).
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "dsp/MicAnalyzer.h"
#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

class MicWizard : public juce::Component, public LayoutSelfCheck, private juce::Timer
{
public:
    MicWizard(EngineController& controller, int channel);
    ~MicWizard() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;
    std::function<void()> onClose;

    // Test hook: feed samples as if they came from the channel.
    void feedForTest(const float* x, int n) { analyzer_.feed(x, n); }
    void startPhaseForTest(dsp::MicAnalyzer::Phase p) { analyzer_.begin(p); }
    const dsp::MicAnalysis& analysis() const noexcept { return result_; }
    void showResultsForTest(const dsp::MicAnalysis& r)
    {
        result_ = r;
        step_ = Step::Results;
        apply_.setEnabled(result_.signalDetected);
        updateTexts();
    }

private:
    enum class Step { Detect, Noise, Speech, Results };
    void timerCallback() override;
    void startNoise();
    void startSpeech();
    void finish();
    void apply();
    void updateTexts();

    EngineController& controller_;
    const int channel_;
    dsp::MicAnalyzer analyzer_;
    dsp::MicAnalysis result_;
    Step step_ = Step::Detect;
    double phaseSeconds_ = 0.0;
    std::vector<float> buffer_;
    float liveDb_ = -120.0f;
    double detectSeconds_ = 0.0;
    bool sawSignal_ = false;

    juce::Label title_, instructions_, results_, advice_, note_;
    Meter meter_{1};
    juce::TextButton measureNoise_{"1. MEASURE NOISE (stay silent 5 s)"}, measureSpeech_{"2. MEASURE SPEECH (talk 10 s)"},
        apply_{"APPLY"}, close_{"CANCEL"};
};

} // namespace pf8::ui
