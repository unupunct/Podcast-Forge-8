#include "ui/TopBar.h"

#include "core/Paths.h"
#include "ui/LookAndFeel.h"

namespace pf8::ui {

TopBar::TopBar(EngineController& controller) : controller_(controller)
{
    refresh();
    startTimerHz(10);
}

TopBar::~TopBar() { stopTimer(); }

void TopBar::refresh()
{
    using namespace colours;
    const auto s = controller_.status();
    const auto m = controller_.meters();
    if (tickCount_++ % 10 == 0) cpuPercent_ = cpu_.sample();

    fields_.clear();
    fields_.push_back({"PROJECT", "Untitled", text});

    juce::String backend;
    juce::Colour backendColour = ok;
    if (s.internalClock)
    {
        backend = "INTERNAL CLOCK";
        backendColour = warn;
    }
    else
        backend = juce::String(s.backend) + " " + toString(s.masterMode);
    fields_.push_back({"BACKEND", backend, backendColour});

    fields_.push_back({"SAMPLE RATE", juce::String(s.sampleRate) + " Hz", text});

    juce::String buffer = juce::String(s.blockFrames) + " smp (" + juce::String(1000.0 * s.blockFrames / s.sampleRate, 2) + " ms)";
    if (!s.internalClock && s.masterPeriod > 0) buffer << "  master " << s.masterPeriod;
    fields_.push_back({"BUFFER", buffer, s.masterPeriod > s.blockFrames * 2 ? warn : text});

    fields_.push_back({"CPU", juce::String(cpuPercent_, 1) + " %", cpuPercent_ > 70 ? warn : text});
    const double loadPct = m.load * 100.0;
    fields_.push_back({"AUDIO LOAD", juce::String(loadPct, 1) + " %  (peak " + juce::String(m.loadPeak * 100.0, 0) + ")",
                       m.loadPeak > 0.8 ? error : (m.loadPeak > 0.5 ? warn : text)});

    if (auto disk = diskSpace(paths::defaultProjects()))
    {
        const double gb = static_cast<double>(disk->freeBytes) / (1024.0 * 1024.0 * 1024.0);
        fields_.push_back({"DISK", juce::String(gb, 1) + " GB free", gb < 5.0 ? error : (gb < 20.0 ? warn : text)});
    }
    else
    {
        fields_.push_back({"DISK", "unavailable", error});
    }

    int assigned = 0, okCount = 0;
    for (const auto& c : s.channels)
        for (const auto* v : {&c.mic, &c.headphones})
            if (v->state != EndpointState::None)
            {
                ++assigned;
                if (v->state == EndpointState::Ok) ++okCount;
            }
    fields_.push_back({"DEVICES", juce::String(okCount) + " / " + juce::String(assigned) + " OK  (" + juce::String(s.openStreams) + " streams)",
                       okCount < assigned ? warn : text});
    fields_.push_back({"RECORD", juce::String::fromUTF8("\xe2\x97\x8f STOPPED"), textDim});

    repaint();
}

void TopBar::paint(juce::Graphics& g)
{
    using namespace colours;
    g.fillAll(panel);
    g.setColour(outline);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));

    const float scale = static_cast<float>(getHeight()) / 56.0f;
    auto area = getLocalBounds().reduced(juce::roundToInt(12 * scale), juce::roundToInt(6 * scale));
    const juce::Font caption(juce::FontOptions(10.5f * scale, juce::Font::bold));
    const juce::Font value(juce::FontOptions(14.0f * scale));

    // Widths proportional to the typical length of each field's content.
    static const float weights[] = {1.1f, 1.5f, 0.9f, 2.0f, 0.7f, 1.3f, 1.0f, 1.1f, 1.0f};
    float totalWeight = 0.0f;
    for (size_t i = 0; i < fields_.size() && i < std::size(weights); ++i) totalWeight += weights[i];

    const float w = static_cast<float>(area.getWidth());
    float x = static_cast<float>(area.getX());
    for (size_t i = 0; i < fields_.size() && i < std::size(weights); ++i)
    {
        const float fw = w * weights[i] / totalWeight;
        auto cell = juce::Rectangle<float>(x, static_cast<float>(area.getY()), fw - 8.0f * scale,
                                           static_cast<float>(area.getHeight()));
        g.setColour(textDim);
        g.setFont(caption);
        g.drawText(fields_[i].caption, cell.removeFromTop(cell.getHeight() * 0.42f), juce::Justification::bottomLeft, true);
        g.setColour(fields_[i].colour);
        g.setFont(value);
        g.drawText(fields_[i].value, cell, juce::Justification::centredLeft, true);
        x += fw;
    }
}

} // namespace pf8::ui
