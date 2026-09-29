#include "ui/DeviceListView.h"

#include <algorithm>

#include "ui/LookAndFeel.h"

namespace pf8::ui {

DeviceListView::DeviceListView(EngineController& controller) : controller_(controller)
{
    auto& h = table_.getHeader();
    const int flags = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable;
    h.addColumn("Name", Name, 300, 120, -1, flags);
    h.addColumn("Manufacturer", Manufacturer, 150, 60, -1, flags);
    h.addColumn("Type", Type, 150, 60, -1, flags);
    h.addColumn("In/Out", Direction, 70, 50, -1, flags);
    h.addColumn("Sample rates", Rates, 190, 60, -1, flags);
    h.addColumn("Ch", Channels, 40, 30, -1, flags);
    h.addColumn("Device ID", DeviceId, 360, 80, -1, flags);
    h.addColumn("Status", Status, 110, 60, -1, flags);
    table_.setModel(this);
    table_.setRowHeight(26);
    addAndMakeVisible(table_);

    rescan_.onClick = [this] { controller_.rescanDevices(); };
    addAndMakeVisible(rescan_);
    timerCallback();
    startTimerHz(4);
}

DeviceListView::~DeviceListView() { stopTimer(); }

void DeviceListView::timerCallback()
{
    const auto gen = controller_.registry().generation();
    if (gen == generation_) return;
    generation_ = gen;
    rows_ = controller_.registry().devices();
    std::stable_sort(rows_.begin(), rows_.end(), [](const DeviceInfo& a, const DeviceInfo& b) {
        if (a.online() != b.online()) return a.online();
        if (a.raw.flow != b.raw.flow) return a.raw.flow == Flow::Capture;
        return a.raw.friendlyName < b.raw.friendlyName;
    });
    table_.updateContent();
    repaint();
}

void DeviceListView::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void DeviceListView::resized()
{
    auto r = getLocalBounds().reduced(12);
    auto top = r.removeFromTop(32);
    rescan_.setBounds(top.removeFromRight(160));
    r.removeFromTop(8);
    table_.setBounds(r);
}

void DeviceListView::paintRowBackground(juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll(selected ? colours::panelRaised : (row % 2 ? colours::panel : colours::background));
}

juce::String DeviceListView::cellText(int row, int column) const
{
    if (row < 0 || row >= rowCount()) return {};
    const auto& d = rows_[static_cast<size_t>(row)];
    switch (column)
    {
        case Name: return juce::String::fromUTF8(d.displayName().c_str());
        case Manufacturer: return juce::String::fromUTF8(d.raw.manufacturer.c_str());
        case Type: return toString(d.kind);
        case Direction: return toString(d.raw.flow);
        case Rates:
        {
            juce::StringArray r;
            for (int rate : d.raw.exclusiveRates) r.add(juce::String(rate / 1000.0, rate % 1000 ? 1 : 0) + "k");
            juce::String s = d.raw.mixRate > 0 ? "mix " + juce::String(d.raw.mixRate) : juce::String("-");
            if (!r.isEmpty()) s << "  excl " << r.joinIntoString("/");
            return s;
        }
        case Channels: return d.raw.mixChannels > 0 ? juce::String(d.raw.mixChannels) : "-";
        case DeviceId: return juce::String::fromUTF8(d.raw.endpointId.c_str());
        case Status: return toString(d.raw.state);
        default: return {};
    }
}

void DeviceListView::paintCell(juce::Graphics& g, int row, int column, int w, int h, bool)
{
    if (row < 0 || row >= rowCount()) return;
    const auto& d = rows_[static_cast<size_t>(row)];
    juce::Colour c = column == DeviceId ? colours::textDim : colours::text;
    if (column == Status) c = d.online() ? colours::ok : colours::warn;
    if (!d.online() && column != Status) c = colours::textDim;
    g.setColour(c);
    g.setFont(juce::Font(juce::FontOptions(column == DeviceId ? 12.0f : 14.0f)));
    g.drawText(cellText(row, column), 6, 0, w - 8, h, juce::Justification::centredLeft, true);
}

} // namespace pf8::ui
