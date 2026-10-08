#include "ui/MixerView.h"

#include <algorithm>

#include "ui/LookAndFeel.h"
#include "ui/UiPrefs.h"

namespace pf8::ui {

MixerView::MixerView(EngineController& controller) : controller_(controller), master_(controller)
{
    for (int i = 0; i < kNumChannels; ++i)
    {
        strips_[static_cast<size_t>(i)] = std::make_unique<ChannelStripView>(controller, i);
        strips_[static_cast<size_t>(i)]->onConfigure = [this](int ch) {
            if (onConfigureChannel) onConfigureChannel(ch);
        };
        addAndMakeVisible(*strips_[static_cast<size_t>(i)]);
    }
    addAndMakeVisible(master_);
    refresh();
    startTimerHz(30);
}

MixerView::~MixerView() { stopTimer(); }

std::array<bool, kNumChannels> MixerView::visibleChannels(const ControllerStatus& s, bool showAll)
{
    std::array<bool, kNumChannels> v{};
    bool any = false;
    for (int i = 0; i < kNumChannels; ++i)
    {
        v[static_cast<size_t>(i)] = s.channels[static_cast<size_t>(i)].mic.state != EndpointState::None;
        any = any || v[static_cast<size_t>(i)];
    }
    if (showAll || !any) v.fill(true);
    return v;
}

int MixerView::visibleCount() const
{
    int n = 0;
    for (bool b : visible_) n += b ? 1 : 0;
    return n;
}

void MixerView::refresh()
{
    const auto s = controller_.status();
    const auto m = controller_.meters();
    const auto v = visibleChannels(s, uiPrefs().showAllChannels.load());
    if (v != visible_)
    {
        visible_ = v;
        for (int i = 0; i < kNumChannels; ++i) strips_[static_cast<size_t>(i)]->setVisible(visible_[static_cast<size_t>(i)]);
        resized();
    }
    for (int i = 0; i < kNumChannels; ++i)
        if (visible_[static_cast<size_t>(i)]) strips_[static_cast<size_t>(i)]->update(s.channels[static_cast<size_t>(i)], m);
    master_.update(s, m);
}

void MixerView::resized()
{
    auto b = getLocalBounds().reduced(6);
    // Master is 1.35 strip-widths of the full 8-channel layout; strips share the rest equally, but
    // with only a few channels a strip grows to at most 1.6x that width (the rest stays empty).
    const float unit = static_cast<float>(b.getWidth()) / (kNumChannels + 1.35f);
    const int masterW = static_cast<int>(unit * 1.35f);
    master_.setBounds(b.removeFromRight(masterW).reduced(3, 0));
    b.removeFromRight(6);
    const int n = std::max(1, visibleCount());
    const float w = std::min(static_cast<float>(b.getWidth()) / static_cast<float>(n), unit * 1.6f);
    int slot = 0;
    for (int i = 0; i < kNumChannels; ++i)
    {
        if (!visible_[static_cast<size_t>(i)]) continue;
        const int x0 = b.getX() + static_cast<int>(w * static_cast<float>(slot));
        const int x1 = b.getX() + static_cast<int>(w * static_cast<float>(slot + 1));
        strips_[static_cast<size_t>(i)]->setBounds(juce::Rectangle<int>(x0, b.getY(), x1 - x0, b.getHeight()).reduced(3, 0));
        ++slot;
    }
}

void MixerView::paint(juce::Graphics& g) { g.fillAll(colours::background); }

} // namespace pf8::ui
