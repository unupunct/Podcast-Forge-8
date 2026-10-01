#pragma once
// Recording transport: REC / PAUSE / STOP / MARKER, timecode, disk time, alerts.
// Safety: low disk asks before recording; STOP asks for confirmation; nothing ever stops by itself.
#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

class TransportBar : public juce::Component, public LayoutSelfCheck, private juce::Timer
{
public:
    explicit TransportBar(EngineController& controller);
    ~TransportBar() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;

    // Actions (also used by hotkeys in Stage 12).
    void record();
    void pause();
    void stop();
    void marker();

private:
    void timerCallback() override;
    void beginRecording();
    void continueElsewhere();

    EngineController& controller_;
    juce::TextButton rec_{"REC"}, pause_{"PAUSE"}, stop_{"STOP"}, marker_{"MARKER"}, elsewhere_{"CONTINUE ELSEWHERE..."};
    juce::Label time_, info_;
    Recorder::Status status_;
    int blink_ = 0;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace pf8::ui
