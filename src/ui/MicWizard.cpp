#include "ui/MicWizard.h"

#include <cmath>

#include "dsp/Presets.h"

namespace pf8::ui {
namespace {
juce::String db1(float v) { return v <= -119.0f ? juce::String("-") : juce::String(v, 1) + " dBFS"; }
} // namespace

MicWizard::MicWizard(EngineController& controller, int channel) : controller_(controller), channel_(channel)
{
    analyzer_.prepare(controller_.settings().sampleRate);
    buffer_.resize(48000);
    const auto a = controller_.assignments();
    const auto& ca = a.ch[static_cast<size_t>(channel_)];
    title_.setText("MICROPHONE SETUP  -  CHANNEL " + juce::String(channel_ + 1) + "  " + juce::String::fromUTF8(ca.name.c_str()),
                   juce::dontSendNotification);
    title_.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    allowEllipsis(title_);
    for (auto* l : {&instructions_, &results_, &advice_, &note_})
    {
        l->setFont(juce::FontOptions(14.0f));
        l->setJustificationType(juce::Justification::topLeft);
        allowEllipsis(*l);
    }
    results_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::plain));
    note_.setColour(juce::Label::textColourId, colours::textDim);
    note_.setText("Podcast Forge never changes the microphone's hardware gain. If the wizard asks for more or less gain, "
                  "turn the gain knob on the microphone or interface.",
                  juce::dontSendNotification);
    measureNoise_.onClick = [this] { startNoise(); };
    measureSpeech_.onClick = [this] { startSpeech(); };
    apply_.onClick = [this] { apply(); };
    close_.onClick = [this] {
        controller_.engine().setAnalysisChannel(-1);
        if (onClose) onClose();
    };
    apply_.setEnabled(false);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&title_, &instructions_, &results_, &advice_, &note_, &meter_,
                                                                      &measureNoise_, &measureSpeech_, &apply_, &close_})
        addAndMakeVisible(c);
    controller_.engine().setAnalysisChannel(channel_);
    updateTexts();
    startTimerHz(30);
}

MicWizard::~MicWizard()
{
    stopTimer();
    controller_.engine().setAnalysisChannel(-1);
}

void MicWizard::startNoise()
{
    step_ = Step::Noise;
    phaseSeconds_ = 0.0;
    analyzer_.begin(dsp::MicAnalyzer::Phase::Noise);
    updateTexts();
}

void MicWizard::startSpeech()
{
    step_ = Step::Speech;
    phaseSeconds_ = 0.0;
    analyzer_.begin(dsp::MicAnalyzer::Phase::Speech);
    updateTexts();
}

void MicWizard::finish()
{
    result_ = analyzer_.result();
    analyzer_.begin(dsp::MicAnalyzer::Phase::Idle);
    step_ = Step::Results;
    apply_.setEnabled(result_.signalDetected);
    updateTexts();
}

void MicWizard::apply()
{
    auto& p = controller_.engine().dsp(channel_);
    p.inputTrimDb.set(result_.recommendedTrimDb);
    p.hpfOn = true;
    p.hpfHz.set(result_.hpfHz);
    if (result_.haveNoise)
    {
        p.gateOn = true;
        p.gateThresholdDb.set(result_.gateThresholdDb);
    }
    dsp::apply(dsp::CompPreset::Podcast, p);
    if (result_.haveSpeech) p.compThresholdDb.set(result_.compThresholdDb);
    p.limiterOn = true;
    p.limiterCeilingDb.set(result_.limiterCeilingDb);
    controller_.engine().setAnalysisChannel(-1);
    if (onClose) onClose();
}

void MicWizard::timerCallback()
{
    // Drain the engine's analysis tap.
    size_t got = 0;
    float peak = 0.0f;
    double sq = 0.0;
    while ((got = controller_.engine().readAnalysis(buffer_.data(), buffer_.size())) > 0)
    {
        analyzer_.feed(buffer_.data(), static_cast<int>(got));
        for (size_t i = 0; i < got; ++i)
        {
            peak = std::max(peak, std::abs(buffer_[i]));
            sq += static_cast<double>(buffer_[i]) * buffer_[i];
        }
        if (step_ == Step::Noise || step_ == Step::Speech) phaseSeconds_ += static_cast<double>(got) / controller_.settings().sampleRate;
        if (step_ == Step::Detect) detectSeconds_ += static_cast<double>(got) / controller_.settings().sampleRate;
    }
    if (peak > 0.0f) sawSignal_ = true;
    const float rms = static_cast<float>(std::sqrt(sq / std::max<size_t>(1, got == 0 ? 1 : got)));
    meter_.setLevels(&peak, &rms);
    if (step_ == Step::Noise && phaseSeconds_ >= 5.0)
    {
        step_ = Step::Detect; // wait for the speech step
        result_ = analyzer_.result();
        updateTexts();
    }
    if (step_ == Step::Speech && phaseSeconds_ >= 10.0) finish();
    if (step_ == Step::Noise || step_ == Step::Speech) updateTexts();
    else if (step_ == Step::Detect && detectSeconds_ > 2.0 && !sawSignal_)
    {
        instructions_.setText("No audio is arriving from this channel's microphone. Check that it is connected and assigned "
                              "(DEVICE MATRIX), then try again.",
                              juce::dontSendNotification);
        instructions_.setColour(juce::Label::textColourId, colours::warn);
    }
}

void MicWizard::updateTexts()
{
    juce::String ins;
    switch (step_)
    {
        case Step::Detect:
            ins = result_.haveNoise ? "Noise measured. Now press 2 and talk normally, at your usual distance, for 10 seconds."
                                    : "Press 1, then stay completely silent for 5 seconds while the room noise is measured.";
            break;
        case Step::Noise: ins = "Measuring noise... stay silent  (" + juce::String(5.0 - phaseSeconds_, 1) + " s)"; break;
        case Step::Speech: ins = "Measuring speech... talk normally  (" + juce::String(10.0 - phaseSeconds_, 1) + " s)"; break;
        case Step::Results: ins = "Done. Review the recommendation and press APPLY, or CANCEL to keep the current settings."; break;
    }
    instructions_.setText(ins, juce::dontSendNotification);
    instructions_.setColour(juce::Label::textColourId, colours::text);

    juce::String r;
    r << "Noise floor:        " << db1(result_.haveNoise ? result_.noiseFloorDb : -120.0f) << "  (A-weighted)\n";
    r << "Peak level:         " << db1(result_.haveSpeech ? result_.peakDb : -120.0f) << "\n";
    r << "Average speech:     " << db1(result_.haveSpeech ? result_.averageDb : -120.0f) << "\n";
    r << "Clipping:           " << (result_.haveSpeech ? juce::String(result_.clipCount) + " samples" : juce::String("-")) << "\n";
    r << "Recommended gain:   " << (result_.haveSpeech ? formatDb(result_.recommendedTrimDb) + " dB (software trim)" : juce::String("-"));
    results_.setText(r, juce::dontSendNotification);

    juce::String adv;
    if (step_ == Step::Results)
    {
        for (const auto& a : result_.advice) adv << "-  " << juce::String::fromUTF8(a.c_str()) << "\n";
        adv << "Apply sets: trim " << formatDb(result_.recommendedTrimDb) << " dB, high-pass " << juce::String(juce::roundToInt(result_.hpfHz))
            << " Hz, gate " << juce::String(juce::roundToInt(result_.gateThresholdDb)) << " dBFS, Podcast compressor at "
            << juce::String(juce::roundToInt(result_.compThresholdDb)) << " dBFS, limiter -1 dBFS.";
    }
    advice_.setText(adv, juce::dontSendNotification);
    measureSpeech_.setEnabled(step_ == Step::Detect || step_ == Step::Results);
    measureNoise_.setEnabled(step_ == Step::Detect || step_ == Step::Results);
}

void MicWizard::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void MicWizard::resized()
{
    auto b = getLocalBounds().reduced(16);
    title_.setBounds(b.removeFromTop(30));
    b.removeFromTop(8);
    auto bottom = b.removeFromBottom(36);
    close_.setBounds(bottom.removeFromRight(120));
    bottom.removeFromRight(8);
    apply_.setBounds(bottom.removeFromRight(120));
    note_.setBounds(b.removeFromBottom(40));
    meter_.setBounds(b.removeFromRight(24));
    b.removeFromRight(12);
    auto buttons = b.removeFromTop(36);
    measureNoise_.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(0, 0).withTrimmedRight(6));
    measureSpeech_.setBounds(buttons.withTrimmedLeft(6));
    b.removeFromTop(10);
    instructions_.setBounds(b.removeFromTop(44));
    results_.setBounds(b.removeFromTop(120));
    advice_.setBounds(b);
}

void MicWizard::collectLayoutIssues(std::vector<std::string>& issues) const
{
    if (results_.getHeight() < 100) issues.push_back("wizard results area too short");
}

} // namespace pf8::ui
