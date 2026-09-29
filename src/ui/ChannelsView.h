#pragma once
// Eight channel rows: name, microphone (+ input channel), headphones, status, sync, level meter.
// (The full hardware-style mixer arrives in Stage 4; this page is the assignment surface.)
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "engine/EngineController.h"

namespace pf8::ui {

class LevelMeter : public juce::Component
{
public:
    void setLevel(float peakLinear, float rmsLinear);
    void paint(juce::Graphics&) override;
    float displayedPeakDb() const noexcept { return peakDb_; }

private:
    float peakDb_ = -120.0f, rmsDb_ = -120.0f, holdDb_ = -120.0f;
    int holdFrames_ = 0;
};

class ChannelsView : public juce::Component, private juce::Timer
{
public:
    explicit ChannelsView(EngineController& controller);
    ~ChannelsView() override;
    void resized() override;
    void paint(juce::Graphics&) override;

    struct Row
    {
        juce::Label number;
        juce::TextEditor name;
        juce::ComboBox mic, micChannel, headphones;
        juce::Label micStatus, hpStatus, sync;
        juce::TextButton acceptMic{"Use"}, acceptHp{"Use"};
        LevelMeter meter;
        std::vector<std::string> micIds, hpIds; // combo item id - 2 → endpoint id
    };
    const Row& row(int i) const { return *rows_[static_cast<size_t>(i)]; }

private:
    void timerCallback() override;
    void rebuildDeviceLists();
    void refreshStatus();

    EngineController& controller_;
    std::array<std::unique_ptr<Row>, kNumChannels> rows_;
    juce::Label header_[7];
    uint64_t registryGeneration_ = ~0ull;
    std::string assignmentsSnapshot_;
};

} // namespace pf8::ui
