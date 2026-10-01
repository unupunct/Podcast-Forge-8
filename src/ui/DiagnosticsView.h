#pragma once
// DIAGNOSTICS tab (brief §29): CPU, audio load, ticks, watchdog, per-stream sync / drift / xruns,
// recorder throughput and disk, the glitch log, a disk speed test and the JSON report.
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/SystemStats.h"
#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

class DiagnosticsView : public juce::Component, public LayoutSelfCheck, private juce::Timer
{
public:
    explicit DiagnosticsView(EngineController& controller);
    ~DiagnosticsView() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;

private:
    void timerCallback() override;
    void runDiskTest();

    struct Row
    {
        juce::String role, device, state, sync, ppm, fill, xruns, format;
        bool ok = false, master = false, warn = false;
    };

    EngineController& controller_;
    CpuMeter cpu_;
    double cpuPct_ = 0.0;
    ControllerStatus status_;
    EngineMeters meters_;
    Recorder::Status rec_;
    std::vector<Row> rows_;
    std::vector<GlitchEvent> glitches_;
    juce::TextButton report_{"SAVE REPORT"}, restart_{"RESTART AUDIO"}, disk_{"DISK SPEED TEST"};
    juce::Label note_;
    juce::Rectangle<int> tiles_, table_, log_;
    std::thread diskThread_;
    std::atomic<bool> diskRunning_{false};
    std::atomic<double> diskMBps_{-1.0};
    std::mutex diskMutex_;
    std::string diskError_; // guarded by diskMutex_
    int ticks_ = 0;
};

} // namespace pf8::ui
