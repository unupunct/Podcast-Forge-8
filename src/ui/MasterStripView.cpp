#include "ui/MasterStripView.h"

#include "ui/LookAndFeel.h"

namespace pf8::ui {

MasterStripView::MasterStripView(EngineController& controller)
    : controller_(controller), monVolume_("MON VOL", 0.0, 1.0, 1.0, [](double v) {
          return v <= 0.0005 ? juce::String::fromUTF8("-\xe2\x88\x9e") : formatDb(20.0 * std::log10(v)) + " dB";
      })
{
    auto& eng = controller_.engine();
    auto& rp = eng.routing();
    title_.setText("MASTER", juce::dontSendNotification);
    title_.setJustificationType(juce::Justification::centred);
    monTitle_.setText("MONITOR", juce::dontSendNotification);
    monTitle_.setJustificationType(juce::Justification::centred);
    monTitle_.setColour(juce::Label::textColourId, colours::textDim);

    fader_.onChange = [&rp](float g) { rp.masterFader.set(g); };
    limiter_.onToggle = [&eng](bool on) { eng.masterDsp().limiterOn = on; };
    mute_.onToggle = [&rp](bool on) { rp.masterMute = on; };

    monSource_.addItem("Main", 1);
    monSource_.addItem("PFL", 2);
    monSource_.addItem("Clean feed", 3);
    for (int i = 0; i < 8; ++i) monSource_.addItem("Headphones " + juce::String(i + 1), 4 + i);
    monSource_.setSelectedId(1, juce::dontSendNotification);
    monSource_.onChange = [this, &rp] {
        rp.monitor.source = static_cast<MonitorSource>(monSource_.getSelectedId() - 1);
    };
    monSource_.setTooltip("What the operator hears. Any PFL switches the monitor to PFL automatically.");
    monOutput_.onChange = [this] {
        const int id = monOutput_.getSelectedId();
        if (id == 1) controller_.assignOutput(OutputRole::Monitor, std::nullopt);
        else if (id >= 2 && static_cast<size_t>(id - 2) < outputIds_.size())
            controller_.assignOutput(OutputRole::Monitor, outputIds_[static_cast<size_t>(id - 2)]);
    };
    monOutput_.setTooltip("Operator monitor output device");
    monVolume_.onChange = [&rp](double v) { rp.monitor.volume.set(static_cast<float>(v)); };
    dim_.onToggle = [&rp](bool on) { rp.monitor.dim = on; };
    mono_.onToggle = [&rp](bool on) { rp.monitor.mono = on; };
    monMute_.onToggle = [&rp](bool on) { rp.monitor.mute = on; };
    monStatus_.setJustificationType(juce::Justification::centred);
    monStatus_.setMinimumHorizontalScale(0.8f);
    allowEllipsis(monStatus_);

    limiter_.setToggleState(eng.masterDsp().limiterOn.load(), juce::dontSendNotification);
    fader_.setGain(rp.masterFader.get());

    for (juce::Component* c : std::initializer_list<juce::Component*>{&title_, &monTitle_, &meter_, &fader_, &limiter_, &mute_,
                                                                      &monSource_, &monOutput_, &monVolume_, &dim_, &mono_,
                                                                      &monMute_, &monStatus_})
        addAndMakeVisible(c);
}

void MasterStripView::rebuildOutputs()
{
    const auto a = controller_.assignments();
    const auto& mon = a.outputs[static_cast<size_t>(OutputRole::Monitor)].device;
    monOutput_.clear(juce::dontSendNotification);
    outputIds_.clear();
    monOutput_.addItem("-- no monitor output --", 1);
    int selected = 1;
    for (const auto& d : controller_.registry().devices())
    {
        if (d.raw.flow != Flow::Render || (!d.online() && !(mon && mon->endpointId == d.raw.endpointId))) continue;
        outputIds_.push_back(d.raw.endpointId);
        const int id = static_cast<int>(outputIds_.size()) + 1;
        monOutput_.addItem(juce::String::fromUTF8(d.displayName().c_str()), id);
        if (mon && mon->endpointId == d.raw.endpointId) selected = id;
    }
    monOutput_.setSelectedId(selected, juce::dontSendNotification);
    assignedMonitor_ = mon ? mon->endpointId : std::string();
}

void MasterStripView::update(const ControllerStatus& s, const EngineMeters& m)
{
    const auto& mon = controller_.assignments().outputs[static_cast<size_t>(OutputRole::Monitor)].device;
    const std::string assigned = mon ? mon->endpointId : std::string();
    if (controller_.registry().generation() != registryGeneration_ || assigned != assignedMonitor_)
    {
        registryGeneration_ = controller_.registry().generation();
        rebuildOutputs();
    }
    const auto main = static_cast<size_t>(idx(BusId::Main));
    const float peak[2] = {m.busPeak[main][0], m.busPeak[main][1]};
    meter_.setLevels(peak, peak);
    const auto& v = s.outputs[static_cast<size_t>(OutputRole::Monitor)];
    monStatus_.setText(v.state == EndpointState::None ? juce::String("no monitor output") : juce::String(toString(v.state)),
                       juce::dontSendNotification);
    monStatus_.setColour(juce::Label::textColourId, v.state == EndpointState::Ok ? colours::ok : colours::textDim);
    const auto& rp = controller_.engine().routing();
    if (!fader_.slider().isMouseButtonDown()) fader_.setGain(rp.masterFader.get());
    mute_.setToggleState(rp.masterMute.load(), juce::dontSendNotification);
    const int src = static_cast<int>(m.anyPfl && rp.monitor.autoPfl.load() ? MonitorSource::Pfl : rp.monitor.source.load());
    monSource_.setSelectedId(src + 1, juce::dontSendNotification);
    monSource_.setColour(juce::ComboBox::outlineColourId, m.anyPfl ? colours::pfl : (m.anySolo ? colours::solo : colours::outline));
}

void MasterStripView::resized()
{
    auto b = getLocalBounds().reduced(6);
    const int H = b.getHeight(), W = b.getWidth();
    const int row = juce::jlimit(20, 34, H / 26);
    title_.setFont(juce::FontOptions(juce::jlimit(13.0f, 20.0f, row * 0.75f), juce::Font::bold));
    monTitle_.setFont(juce::FontOptions(juce::jlimit(11.0f, 15.0f, row * 0.55f), juce::Font::bold));
    monStatus_.setFont(juce::FontOptions(juce::jlimit(10.0f, 13.0f, row * 0.5f)));
    title_.setBounds(b.removeFromTop(row + row / 2));

    // Monitor section at the bottom.
    monSection_ = b.removeFromBottom(row * 5 + H / 7 + 16);
    auto mb = monSection_.reduced(0, 6);
    monTitle_.setBounds(mb.removeFromTop(row));
    monSource_.setBounds(mb.removeFromTop(row).reduced(2));
    monOutput_.setBounds(mb.removeFromTop(row).reduced(2));
    monStatus_.setBounds(mb.removeFromTop(row * 3 / 4));
    auto btns = mb.removeFromBottom(row);
    dim_.setBounds(btns.removeFromLeft(W / 3).reduced(1));
    mono_.setBounds(btns.removeFromLeft(W / 3).reduced(1));
    monMute_.setBounds(btns.reduced(1));
    monVolume_.setBounds(mb.reduced(2));

    auto top = b.removeFromTop(row);
    limiter_.setBounds(top.removeFromLeft(W / 2).reduced(1));
    mute_.setBounds(top.reduced(1));
    b.removeFromTop(6);
    meter_.setBounds(b.removeFromRight(juce::jmax(20, W / 4)).reduced(0, 2));
    fader_.setBounds(b.reduced(2, 0));
}

void MasterStripView::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(colours::panel.brighter(0.03f));
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(colours::accent.withAlpha(0.5f));
    g.drawRoundedRectangle(b, 5.0f, 1.2f);
    g.setColour(colours::outline);
    g.drawHorizontalLine(monSection_.getY(), static_cast<float>(monSection_.getX() + 6), static_cast<float>(monSection_.getRight() - 6));
}

void MasterStripView::collectLayoutIssues(std::vector<std::string>& issues) const
{
    fader_.collectLayoutIssues(issues);
    monVolume_.collectLayoutIssues(issues);
}

} // namespace pf8::ui
