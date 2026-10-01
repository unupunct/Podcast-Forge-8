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

void TopBar::mouseUp(const juce::MouseEvent& e)
{
    const auto c = cells();
    if (!c.empty() && c[0].contains(e.position) && onProjectClicked) onProjectClicked();
}

void TopBar::refresh()
{
    using namespace colours;
    const auto s = controller_.status();
    const auto m = controller_.meters();
    if (tickCount_++ % 10 == 0) cpuPercent_ = cpu_.sample();

    fields_.clear();
    fields_.push_back({"PROJECT", projectName ? projectName() : juce::String("Untitled"), text});

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
    const auto rs = controller_.recorder().state();
    fields_.push_back({"RECORD", juce::String::fromUTF8("\xe2\x97\x8f ") + toString(rs),
                       rs == Recorder::State::Recording ? record : rs == Recorder::State::Paused ? warn : textDim});

    repaint();
}

namespace {
// Widths proportional to the typical length of each field's content.
const float kWeights[] = {1.0f, 1.9f, 1.0f, 2.1f, 0.8f, 1.45f, 1.05f, 1.75f, 1.0f};
juce::Font captionFont() { return juce::Font(juce::FontOptions(10.5f, juce::Font::bold)); }
juce::Font valueFont() { return juce::Font(juce::FontOptions(13.5f)); }
float textW(const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, s, 0.0f, 0.0f);
    return ga.getBoundingBox(0, -1, true).getWidth();
}
} // namespace

std::vector<juce::Rectangle<float>> TopBar::cells() const
{
    std::vector<juce::Rectangle<float>> out;
    auto area = getLocalBounds().reduced(12, 6).toFloat();
    float total = 0.0f;
    for (size_t i = 0; i < fields_.size() && i < std::size(kWeights); ++i) total += kWeights[i];
    float x = area.getX();
    for (size_t i = 0; i < fields_.size() && i < std::size(kWeights); ++i)
    {
        const float fw = area.getWidth() * kWeights[i] / total;
        out.emplace_back(x, area.getY(), fw - 10.0f, area.getHeight());
        x += fw;
    }
    return out;
}

void TopBar::paint(juce::Graphics& g)
{
    using namespace colours;
    g.fillAll(panel);
    g.setColour(outline);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
    const auto cs = cells();
    for (size_t i = 0; i < cs.size(); ++i)
    {
        auto cell = cs[i];
        g.setColour(textDim);
        g.setFont(captionFont());
        g.drawText(fields_[i].caption, cell.removeFromTop(cell.getHeight() * 0.42f), juce::Justification::bottomLeft, true);
        g.setColour(fields_[i].colour);
        g.setFont(valueFont());
        g.drawText(fields_[i].value, cell, juce::Justification::centredLeft, true);
    }
}

void TopBar::collectLayoutIssues(std::vector<std::string>& issues) const
{
    const auto cs = cells();
    for (size_t i = 0; i < cs.size(); ++i)
        if (textW(valueFont(), fields_[i].value) > cs[i].getWidth())
            issues.push_back("top bar " + fields_[i].caption.toStdString() + " value '" + fields_[i].value.toStdString() + "' clipped at " +
                             std::to_string(static_cast<int>(cs[i].getWidth())) + " px");
}

} // namespace pf8::ui
