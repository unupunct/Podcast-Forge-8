#include "ui/MixerView.h"

#include "ui/LookAndFeel.h"

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

void MixerView::refresh()
{
    const auto s = controller_.status();
    const auto m = controller_.meters();
    for (int i = 0; i < kNumChannels; ++i) strips_[static_cast<size_t>(i)]->update(s.channels[static_cast<size_t>(i)], m);
    master_.update(s, m);
}

void MixerView::resized()
{
    auto b = getLocalBounds().reduced(6);
    // Master is 1.35 strips wide; strips share the rest equally.
    const float unit = static_cast<float>(b.getWidth()) / (kNumChannels + 1.35f);
    const int masterW = static_cast<int>(unit * 1.35f);
    master_.setBounds(b.removeFromRight(masterW).reduced(3, 0));
    b.removeFromRight(6);
    const float w = static_cast<float>(b.getWidth()) / kNumChannels;
    for (int i = 0; i < kNumChannels; ++i)
    {
        const int x0 = b.getX() + static_cast<int>(w * static_cast<float>(i));
        const int x1 = b.getX() + static_cast<int>(w * static_cast<float>(i + 1));
        strips_[static_cast<size_t>(i)]->setBounds(juce::Rectangle<int>(x0, b.getY(), x1 - x0, b.getHeight()).reduced(3, 0));
    }
}

void MixerView::paint(juce::Graphics& g) { g.fillAll(colours::background); }

} // namespace pf8::ui
