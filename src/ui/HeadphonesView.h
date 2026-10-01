#pragma once
// HEADPHONES dock tab: eight personal monitor mixes (ROUTING.md §3).
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

// One headphone mix: mode, volume, mute, protection, and its send bars.
class HpPanel : public juce::Component, public LayoutSelfCheck
{
public:
    HpPanel(EngineController& controller, int hp);
    void update(const ControllerStatus& s, const EngineMeters& m);
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;

    static constexpr int kSends = 12; // CH1–8, Music, Carts, Remote, Reverb

private:
    static int sourceFor(int row) noexcept;
    juce::Rectangle<int> barBounds(int row) const;
    int rowAt(juce::Point<int> p) const;

    EngineController& controller_;
    const int hp_;
    juce::Label title_;
    juce::ComboBox mode_, ceiling_;
    juce::Slider volume_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
    ToggleLed mute_{"MUTE", colours::mute};
    juce::TextButton reset_{"PERSONAL"};
    juce::Rectangle<int> sendsArea_;
    int columns_ = 1; // two columns of sends when the dock is short
    std::array<std::string, kNumChannels> names_{};
    float protectGr_ = 0.0f;
    int dragRow_ = -1;
    float dragStart_ = 0.0f;
};

class HeadphonesView : public juce::Component, private juce::Timer
{
public:
    explicit HeadphonesView(EngineController& controller);
    ~HeadphonesView() override;
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    EngineController& controller_;
    std::array<std::unique_ptr<HpPanel>, kNumChannels> panels_;
};

} // namespace pf8::ui
