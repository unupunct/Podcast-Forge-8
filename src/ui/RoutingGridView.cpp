#include "ui/RoutingGridView.h"

#include "ui/LookAndFeel.h"

namespace pf8::ui {
namespace {

const char* kColNames[RoutingGridView::kCols] = {"MAIN", "CLEAN", "MUSIC", "HP 1", "HP 2", "HP 3", "HP 4",
                                                  "HP 5", "HP 6", "HP 7", "HP 8"};

float textWidth(const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, s, 0.0f, 0.0f);
    return ga.getBoundingBox(0, -1, true).getWidth();
}

} // namespace

RoutingGridView::RoutingGridView(EngineController& controller) : controller_(controller)
{
    setRepaintsOnMouseActivity(false);
    timerCallback();
    startTimerHz(10);
}

RoutingGridView::~RoutingGridView() { stopTimer(); }

void RoutingGridView::timerCallback()
{
    const auto a = controller_.assignments();
    for (size_t i = 0; i < kNumChannels; ++i) channelNames_[i] = a.ch[i].name;
    repaint();
}

juce::String RoutingGridView::rowName(int row) const
{
    if (row < kNumChannels)
    {
        const auto& n = channelNames_[static_cast<size_t>(row)];
        return juce::String(row + 1) + "  " + (n.empty() ? juce::String("Channel ") + juce::String(row + 1) : juce::String::fromUTF8(n.c_str()));
    }
    return toString(static_cast<SourceId>(row));
}

void RoutingGridView::resized()
{
    auto b = getLocalBounds().reduced(10);
    headerH_ = juce::jlimit(18, 28, b.getHeight() / (kRows + 2));
    labelW_ = juce::jlimit(110, 200, b.getWidth() / 8);
    grid_ = b.withTrimmedLeft(labelW_).withTrimmedTop(headerH_);
}

juce::Rectangle<int> RoutingGridView::cellBounds(int row, int col) const
{
    const float cw = static_cast<float>(grid_.getWidth()) / kCols;
    const float ch = static_cast<float>(grid_.getHeight()) / kRows;
    const int x0 = grid_.getX() + static_cast<int>(cw * static_cast<float>(col));
    const int y0 = grid_.getY() + static_cast<int>(ch * static_cast<float>(row));
    return {x0, y0, static_cast<int>(cw * static_cast<float>(col + 1)) - static_cast<int>(cw * static_cast<float>(col)),
            static_cast<int>(ch * static_cast<float>(row + 1)) - static_cast<int>(ch * static_cast<float>(row))};
}

bool RoutingGridView::cellAt(juce::Point<int> p, int& row, int& col) const
{
    if (!grid_.contains(p)) return false;
    col = juce::jlimit(0, kCols - 1, (p.x - grid_.getX()) * kCols / juce::jmax(1, grid_.getWidth()));
    row = juce::jlimit(0, kRows - 1, (p.y - grid_.getY()) * kRows / juce::jmax(1, grid_.getHeight()));
    return true;
}

void RoutingGridView::paint(juce::Graphics& g)
{
    using namespace colours;
    g.fillAll(background);
    auto& rp = controller_.engine().routing();
    const float fs = juce::jlimit(10.0f, 14.0f, static_cast<float>(cellBounds(0, 0).getHeight()) * 0.55f);
    const juce::Font font{juce::FontOptions(fs)}, bold{juce::FontOptions(fs, juce::Font::bold)};

    g.setFont(bold);
    for (int c = 0; c < kCols; ++c)
    {
        auto cb = cellBounds(0, c);
        g.setColour(c >= 3 ? mon : textDim);
        g.drawText(kColNames[c], cb.getX(), grid_.getY() - headerH_, cb.getWidth(), headerH_, juce::Justification::centred, false);
    }
    const bool tbToProgram = rp.talkbackToProgram.load();
    for (int r = 0; r < kRows; ++r)
    {
        auto rowB = cellBounds(r, 0);
        g.setFont(r < kNumChannels ? bold : font);
        g.setColour(r < kNumChannels ? text : textDim);
        g.drawText(rowName(r), grid_.getX() - labelW_, rowB.getY(), labelW_ - 8, rowB.getHeight(), juce::Justification::centredLeft, true);
        for (int c = 0; c < kCols; ++c)
        {
            auto cb = cellBounds(r, c).reduced(2);
            const auto s = static_cast<size_t>(r), bus = static_cast<size_t>(busForColumn(c));
            g.setFont(font);
            if (r == idx(SourceId::Talkback))
            {
                if (c == 2)
                {
                    g.setColour(textDim.withAlpha(0.6f));
                    g.drawText("-", cb, juce::Justification::centred, false);
                }
                else if (c < 2)
                {
                    const bool locked = !tbToProgram;
                    g.setColour(panelRaised);
                    g.fillRoundedRectangle(cb.toFloat(), 3.0f);
                    g.setColour(locked ? error.withAlpha(0.8f) : warn);
                    g.drawText(locked ? "LOCKED" : "ON", cb, juce::Justification::centred, false);
                }
                else
                {
                    const bool target = rp.talkbackTarget[static_cast<size_t>(c - 3)].load();
                    g.setColour(target ? warn.withAlpha(0.8f) : panelRaised);
                    g.fillRoundedRectangle(cb.toFloat(), 3.0f);
                    g.setColour(target ? juce::Colours::black : textDim);
                    g.drawText(target ? "TB" : "-", cb, juce::Justification::centred, false);
                }
                continue;
            }
            const float gain = rp.gain[s][bus].get();
            g.setColour(panelRaised);
            g.fillRoundedRectangle(cb.toFloat(), 3.0f);
            if (gain > 0.0f)
            {
                g.setColour((c >= 3 ? mon : accent).withAlpha(0.25f + 0.6f * juce::jmin(1.0f, gain)));
                g.fillRoundedRectangle(cb.toFloat().withTrimmedTop(cb.getHeight() * (1.0f - juce::jmin(1.0f, gain))), 3.0f);
            }
            g.setColour(gain > 0.0f ? text : textDim.withAlpha(0.6f));
            g.drawText(gain > 0.0f ? juce::String(juce::roundToInt(gain * 100.0f)) : juce::String("-"), cb, juce::Justification::centred, false);
        }
    }
}

void RoutingGridView::mouseDown(const juce::MouseEvent& e)
{
    int r, c;
    dragRow_ = dragCol_ = -1;
    if (!cellAt(e.getPosition(), r, c)) return;
    auto& rp = controller_.engine().routing();
    if (r == idx(SourceId::Talkback))
    {
        if (c >= 3)
        {
            auto& t = rp.talkbackTarget[static_cast<size_t>(c - 3)];
            t = !t.load();
            repaint();
        }
        return;
    }
    dragRow_ = r;
    dragCol_ = c;
    dragStart_ = rp.gain[static_cast<size_t>(r)][static_cast<size_t>(busForColumn(c))].get();
}

void RoutingGridView::mouseDrag(const juce::MouseEvent& e)
{
    if (dragRow_ < 0) return;
    const float delta = -static_cast<float>(e.getDistanceFromDragStartY()) / 150.0f;
    const float g = juce::jlimit(0.0f, 1.0f, dragStart_ + delta);
    controller_.engine().routing().gain[static_cast<size_t>(dragRow_)][static_cast<size_t>(busForColumn(dragCol_))].set(
        std::round(g * 100.0f) / 100.0f);
    // A hand-edited headphone column becomes a Custom mix.
    if (dragCol_ >= 3) controller_.engine().routing().headphones[static_cast<size_t>(dragCol_ - 3)].mode = HpMode::Custom;
    repaint();
}

void RoutingGridView::mouseDoubleClick(const juce::MouseEvent& e)
{
    int r, c;
    if (!cellAt(e.getPosition(), r, c) || r == idx(SourceId::Talkback)) return;
    auto& p = controller_.engine().routing().gain[static_cast<size_t>(r)][static_cast<size_t>(busForColumn(c))];
    p.set(p.get() > 0.0f ? 0.0f : 1.0f);
    if (c >= 3) controller_.engine().routing().headphones[static_cast<size_t>(c - 3)].mode = HpMode::Custom;
    repaint();
}

void RoutingGridView::collectLayoutIssues(std::vector<std::string>& issues) const
{
    const float fs = juce::jlimit(10.0f, 14.0f, static_cast<float>(cellBounds(0, 0).getHeight()) * 0.55f);
    const juce::Font bold{juce::FontOptions(fs, juce::Font::bold)}, font{juce::FontOptions(fs)};
    const int cw = cellBounds(0, 0).getWidth() - 4;
    for (const char* n : kColNames)
        if (textWidth(bold, n) > static_cast<float>(cw)) issues.push_back(std::string("Routing column '") + n + "' too narrow");
    if (textWidth(font, "LOCKED") > static_cast<float>(cw)) issues.push_back("Routing cell too narrow for LOCKED");
    if (cellBounds(0, 0).getHeight() < 12) issues.push_back("Routing rows below 12 px");
}

} // namespace pf8::ui
