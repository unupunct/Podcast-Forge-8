#pragma once
// Settings → Devices: every enumerated endpoint with its properties.
#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/EngineController.h"

namespace pf8::ui {

class DeviceListView : public juce::Component, private juce::TableListBoxModel, private juce::Timer
{
public:
    explicit DeviceListView(EngineController& controller);
    ~DeviceListView() override;

    void resized() override;
    void paint(juce::Graphics&) override;
    int rowCount() const noexcept { return static_cast<int>(rows_.size()); }

private:
    enum Column { Name = 1, Manufacturer, Type, Direction, Rates, Channels, DeviceId, Status };

    int getNumRows() override { return rowCount(); }
    void paintRowBackground(juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell(juce::Graphics&, int row, int column, int w, int h, bool selected) override;
    juce::String cellText(int row, int column) const;
    void timerCallback() override;

    EngineController& controller_;
    juce::TableListBox table_;
    juce::TextButton rescan_{"Rescan devices"};
    std::vector<DeviceInfo> rows_;
    uint64_t generation_ = ~0ull;
};

} // namespace pf8::ui
