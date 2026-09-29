#pragma once
// ROUTING tab: sources × buses. Each cell is a send gain (drag vertically, double-click toggles
// 0 ↔ 100 %). The talkback row shows the program lock and toggles headphone targets.
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

class RoutingGridView : public juce::Component, public LayoutSelfCheck, private juce::Timer
{
public:
    explicit RoutingGridView(EngineController& controller);
    ~RoutingGridView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;

    static constexpr int kCols = 11; // Main, Clean, Music, HP1..HP8
    static constexpr int kRows = kSourceCount;

private:
    void timerCallback() override;
    bool cellAt(juce::Point<int> p, int& row, int& col) const;
    juce::Rectangle<int> cellBounds(int row, int col) const;
    static int busForColumn(int col) noexcept { return col; } // BusId order matches Main, Clean, MusicOut, HP1..
    juce::String rowName(int row) const;

    EngineController& controller_;
    std::array<std::string, kNumChannels> channelNames_{};
    juce::Rectangle<int> grid_;
    int headerH_ = 24, labelW_ = 120;
    int dragRow_ = -1, dragCol_ = -1;
    float dragStart_ = 0.0f;
};

} // namespace pf8::ui
