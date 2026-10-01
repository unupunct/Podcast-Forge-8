#pragma once
// MARKERS dock tab: the current session's markers, rename (double-click), delete, export.
#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"

namespace pf8::ui {

class MarkersView : public juce::Component, private juce::TableListBoxModel, private juce::Timer
{
public:
    explicit MarkersView(EngineController& controller);
    ~MarkersView() override;
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    int getNumRows() override { return static_cast<int>(rows_.size()); }
    void paintRowBackground(juce::Graphics&, int, int, int, bool) override;
    void paintCell(juce::Graphics&, int row, int col, int w, int h, bool) override;
    void cellDoubleClicked(int row, int col, const juce::MouseEvent&) override;
    void timerCallback() override;
    void exportAs();

    EngineController& controller_;
    juce::TableListBox table_;
    juce::TextButton add_{"ADD MARKER"}, remove_{"DELETE"}, export_{"EXPORT..."};
    juce::Label hint_;
    std::vector<Marker> rows_;
};

} // namespace pf8::ui
