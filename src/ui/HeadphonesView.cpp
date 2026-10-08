#include "ui/HeadphonesView.h"

#include <cmath>

namespace pf8::ui {
namespace {
const char* kExtraNames[4] = {"Music", "Carts", "Remote", "Reverb"};
const char* kShortExtra[4] = {"Mus", "Crt", "Rem", "Rev"};
} // namespace

int HpPanel::sourceFor(int row) noexcept
{
    if (row < kNumChannels) return row;
    switch (row - kNumChannels)
    {
        case 0: return idx(SourceId::Music);
        case 1: return idx(SourceId::Carts);
        case 2: return idx(SourceId::Remote);
        default: return idx(SourceId::Fx);
    }
}

HpPanel::HpPanel(EngineController& controller, int hp) : controller_(controller), hp_(hp)
{
    auto& p = controller_.engine().routing().headphones[static_cast<size_t>(hp_)];
    title_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    title_.setJustificationType(juce::Justification::centredLeft);
    allowEllipsis(title_);
    mode_.addItem("Main mix", 1);
    mode_.addItem("Personal", 2);
    mode_.addItem("Custom", 3);
    mode_.setSelectedId(static_cast<int>(p.mode.load()) + 1, juce::dontSendNotification);
    mode_.onChange = [this] {
        controller_.engine().routing().headphones[static_cast<size_t>(hp_)].mode = static_cast<HpMode>(mode_.getSelectedId() - 1);
        repaint();
    };
    mode_.setTooltip("MAIN: hear the programme. PERSONAL: self 100 %, others 70 %. CUSTOM: your own sends.");
    for (int db : {0, -3, -6, -10, -14, -20}) ceiling_.addItem("max " + juce::String(db) + " dB", 100 + db);
    ceiling_.setSelectedId(100 + static_cast<int>(p.protectCeilingDb.get()), juce::dontSendNotification);
    ceiling_.onChange = [this] {
        controller_.engine().routing().headphones[static_cast<size_t>(hp_)].protectCeilingDb.set(static_cast<float>(ceiling_.getSelectedId() - 100));
    };
    ceiling_.setTooltip("Hearing protection: this headphone feed never goes louder than this.");
    volume_.setRange(-60.0, 12.0, 0.1);
    volume_.setSkewFactorFromMidPoint(-12.0);
    volume_.setValue(20.0 * std::log10(std::max(1e-3f, p.volume.get())), juce::dontSendNotification);
    volume_.setDoubleClickReturnValue(true, 0.0);
    volume_.onValueChange = [this] {
        const double v = volume_.getValue();
        controller_.engine().routing().headphones[static_cast<size_t>(hp_)].volume.set(v <= -59.5 ? 0.0f : static_cast<float>(std::pow(10.0, v / 20.0)));
        repaint();
    };
    mute_.onToggle = [this](bool on) { controller_.engine().routing().headphones[static_cast<size_t>(hp_)].mute = on; };
    reset_.setTooltip("Reset the sends to the personal template (self 100 %, others 70 %, music 20 %)");
    reset_.onClick = [this] {
        auto& rp = controller_.engine().routing();
        rp.applyPersonalTemplate(hp_);
        rp.headphones[static_cast<size_t>(hp_)].mode = HpMode::Personal;
        mode_.setSelectedId(2, juce::dontSendNotification);
        repaint();
    };
    for (juce::Component* c : std::initializer_list<juce::Component*>{&title_, &mode_, &ceiling_, &volume_, &mute_, &reset_}) addAndMakeVisible(c);
}

void HpPanel::update(const ControllerStatus& s, const EngineMeters& m)
{
    const auto& c = s.channels[static_cast<size_t>(hp_)];
    for (size_t i = 0; i < kNumChannels; ++i) names_[i] = s.channels[i].name;
    juce::String t = "HP " + juce::String(hp_ + 1) + "  " + juce::String::fromUTF8(c.name.c_str());
    title_.setText(t, juce::dontSendNotification);
    title_.setColour(juce::Label::textColourId, c.headphones.state == EndpointState::Ok ? colours::text : colours::textDim);
    title_.setTooltip(c.headphones.state == EndpointState::None ? juce::String("no headphones assigned")
                                                                : juce::String::fromUTF8(c.headphones.assignedName.c_str()) + " - " + toString(c.headphones.state));
    const auto& p = controller_.engine().routing().headphones[static_cast<size_t>(hp_)];
    mode_.setSelectedId(static_cast<int>(p.mode.load()) + 1, juce::dontSendNotification);
    mute_.setToggleState(p.mute.load(), juce::dontSendNotification);
    protectGr_ = m.hpProtectGrDb[static_cast<size_t>(hp_)];
    repaint(sendsArea_.withHeight(sendsArea_.getHeight() + 20));
}

void HpPanel::resized()
{
    auto b = getLocalBounds().reduced(6);
    title_.setBounds(b.removeFromTop(20));
    auto r1 = b.removeFromTop(24);
    mode_.setBounds(r1.removeFromLeft(r1.getWidth() * 55 / 100).reduced(1));
    ceiling_.setBounds(r1.reduced(1));
    b.removeFromTop(2);
    auto r2 = b.removeFromTop(22);
    mute_.setBounds(r2.removeFromRight(52).reduced(1));
    volume_.setBounds(r2);
    auto bottom = b.removeFromBottom(22);
    reset_.setBounds(bottom.removeFromRight(86).reduced(1));
    b.removeFromTop(4);
    sendsArea_ = b;
    columns_ = sendsArea_.getHeight() / kSends < 12 ? 2 : 1;
}

juce::Rectangle<int> HpPanel::barBounds(int row) const
{
    const int perCol = kSends / columns_;
    const int col = row / perCol, r = row % perCol;
    const int colW = sendsArea_.getWidth() / columns_;
    const int nameW = columns_ == 1 ? 54 : 26;
    const float h = static_cast<float>(sendsArea_.getHeight()) / static_cast<float>(perCol);
    const int y0 = sendsArea_.getY() + static_cast<int>(h * static_cast<float>(r));
    const int y1 = sendsArea_.getY() + static_cast<int>(h * static_cast<float>(r + 1));
    const int x = sendsArea_.getX() + col * colW;
    return {x + nameW, y0, colW - nameW - (columns_ > 1 ? 4 : 0), y1 - y0};
}

int HpPanel::rowAt(juce::Point<int> p) const
{
    if (!sendsArea_.contains(p)) return -1;
    const int perCol = kSends / columns_;
    const int col = juce::jlimit(0, columns_ - 1, (p.x - sendsArea_.getX()) * columns_ / juce::jmax(1, sendsArea_.getWidth()));
    const int r = juce::jlimit(0, perCol - 1, (p.y - sendsArea_.getY()) * perCol / juce::jmax(1, sendsArea_.getHeight()));
    return col * perCol + r;
}

void HpPanel::paint(juce::Graphics& g)
{
    using namespace colours;
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(panel);
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(outline);
    g.drawRoundedRectangle(b, 5.0f, 1.0f);
    auto& rp = controller_.engine().routing();
    const auto mode = rp.headphones[static_cast<size_t>(hp_)].mode.load();
    const float fs = juce::jlimit(9.0f, 12.0f, static_cast<float>(barBounds(0).getHeight()) * 0.8f);
    g.setFont(juce::Font(juce::FontOptions(fs)));
    for (int r = 0; r < kSends; ++r)
    {
        const auto bar = barBounds(r);
        const bool compact = columns_ > 1;
        const juce::String name = r < kNumChannels
                                      ? (compact ? juce::String(r + 1) : juce::String(r + 1) + " " + juce::String::fromUTF8(names_[static_cast<size_t>(r)].c_str()))
                                      : juce::String(compact ? kShortExtra[r - kNumChannels] : kExtraNames[r - kNumChannels]);
        g.setColour(r == hp_ ? mon : textDim);
        const int nameW = compact ? 24 : 52;
        g.drawText(name, bar.getX() - nameW - 2, bar.getY(), nameW, bar.getHeight(), juce::Justification::centredLeft, true);
        const float gain = rp.gain[static_cast<size_t>(sourceFor(r))][static_cast<size_t>(hpBus(hp_))].get();
        auto rb = bar.reduced(0, 1).toFloat();
        g.setColour(panelRaised);
        g.fillRoundedRectangle(rb, 2.0f);
        g.setColour((mode == HpMode::Main ? textDim : mon).withAlpha(0.8f));
        g.fillRoundedRectangle(rb.withWidth(rb.getWidth() * juce::jmin(1.0f, gain)), 2.0f);
        g.setColour(text);
        g.drawText(juce::String(juce::roundToInt(gain * 100.0f)), rb.toNearestInt().reduced(4, 0), juce::Justification::centredRight, false);
    }
    if (mode == HpMode::Main)
    {
        g.setColour(background.withAlpha(0.55f));
        g.fillRect(sendsArea_);
        g.setColour(text);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText("hears the MAIN MIX", sendsArea_, juce::Justification::centred, false);
    }
    // Protection activity under the volume slider.
    if (protectGr_ < -0.5f)
    {
        g.setColour(warn);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("PROTECT " + juce::String(protectGr_, 1) + " dB", getLocalBounds().reduced(8).removeFromBottom(20), juce::Justification::centredLeft,
                   false);
    }
}

void HpPanel::mouseDown(const juce::MouseEvent& e)
{
    dragRow_ = rowAt(e.getPosition());
    if (dragRow_ < 0 || controller_.engine().routing().headphones[static_cast<size_t>(hp_)].mode.load() == HpMode::Main)
    {
        dragRow_ = -1;
        return;
    }
    dragStart_ = controller_.engine().routing().gain[static_cast<size_t>(sourceFor(dragRow_))][static_cast<size_t>(hpBus(hp_))].get();
}

void HpPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (dragRow_ < 0) return;
    const float w = static_cast<float>(barBounds(dragRow_).getWidth());
    const float g = juce::jlimit(0.0f, 1.0f, dragStart_ + static_cast<float>(e.getDistanceFromDragStartX()) / juce::jmax(1.0f, w));
    auto& rp = controller_.engine().routing();
    rp.gain[static_cast<size_t>(sourceFor(dragRow_))][static_cast<size_t>(hpBus(hp_))].set(std::round(g * 100.0f) / 100.0f);
    rp.headphones[static_cast<size_t>(hp_)].mode = HpMode::Custom;
    mode_.setSelectedId(3, juce::dontSendNotification);
    repaint();
}

void HpPanel::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int r = rowAt(e.getPosition());
    if (r < 0) return;
    auto& rp = controller_.engine().routing();
    auto& g = rp.gain[static_cast<size_t>(sourceFor(r))][static_cast<size_t>(hpBus(hp_))];
    g.set(g.get() > 0.0f ? 0.0f : 1.0f);
    rp.headphones[static_cast<size_t>(hp_)].mode = HpMode::Custom;
    mode_.setSelectedId(3, juce::dontSendNotification);
    repaint();
}

void HpPanel::collectLayoutIssues(std::vector<std::string>& issues) const
{
    if (barBounds(0).getHeight() < 9) issues.push_back("HP send rows below 9 px");
    if (barBounds(0).getWidth() < 40) issues.push_back("HP send bars below 40 px");
}

HeadphonesView::HeadphonesView(EngineController& controller) : controller_(controller)
{
    for (int i = 0; i < kNumChannels; ++i)
    {
        panels_[static_cast<size_t>(i)] = std::make_unique<HpPanel>(controller, i);
        addAndMakeVisible(*panels_[static_cast<size_t>(i)]);
    }
    timerCallback();
    startTimerHz(10);
}

HeadphonesView::~HeadphonesView() { stopTimer(); }

void HeadphonesView::timerCallback()
{
    const auto s = controller_.status();
    const auto m = controller_.meters();
    // Only mixes that have headphones assigned (all eight when none is, or "Show all 8 channels").
    std::array<bool, kNumChannels> v{};
    bool any = false;
    for (int i = 0; i < kNumChannels; ++i)
    {
        v[static_cast<size_t>(i)] = s.channels[static_cast<size_t>(i)].headphones.state != EndpointState::None;
        any = any || v[static_cast<size_t>(i)];
    }
    if (uiPrefs().showAllChannels.load() || !any) v.fill(true);
    if (v != visible_)
    {
        visible_ = v;
        for (int i = 0; i < kNumChannels; ++i) panels_[static_cast<size_t>(i)]->setVisible(visible_[static_cast<size_t>(i)]);
        resized();
    }
    for (int i = 0; i < kNumChannels; ++i)
        if (visible_[static_cast<size_t>(i)]) panels_[static_cast<size_t>(i)]->update(s, m);
}

void HeadphonesView::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void HeadphonesView::resized()
{
    auto b = getLocalBounds().reduced(6);
    int n = 0;
    for (bool x : visible_) n += x ? 1 : 0;
    // Wider panels with fewer mixes, at most twice the 8-mix width.
    const float unit = static_cast<float>(b.getWidth()) / kNumChannels;
    const float w = std::min(static_cast<float>(b.getWidth()) / static_cast<float>(std::max(1, n)), unit * 2.0f);
    int slot = 0;
    for (int i = 0; i < kNumChannels; ++i)
    {
        if (!visible_[static_cast<size_t>(i)]) continue;
        panels_[static_cast<size_t>(i)]->setBounds(juce::Rectangle<int>(b.getX() + static_cast<int>(w * static_cast<float>(slot)), b.getY(),
                                                                        static_cast<int>(w), b.getHeight()).reduced(3, 0));
        ++slot;
    }
}

} // namespace pf8::ui
