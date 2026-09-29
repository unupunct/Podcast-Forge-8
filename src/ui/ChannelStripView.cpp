#include "ui/ChannelStripView.h"

#include <cmath>

#include "ui/LookAndFeel.h"

namespace pf8::ui {
namespace {

juce::String panText(double p)
{
    if (std::abs(p) < 0.01) return "C";
    return (p < 0 ? "L" : "R") + juce::String(juce::roundToInt(std::abs(p) * 100.0));
}

juce::Colour ledFor(EndpointState s)
{
    switch (s)
    {
        case EndpointState::Ok: return colours::ok;
        case EndpointState::None: return colours::outline;
        case EndpointState::PossibleMatch: return colours::accent;
        case EndpointState::Offline:
        case EndpointState::Disconnected: return colours::warn;
        default: return colours::error;
    }
}

} // namespace

ChannelStripView::ChannelStripView(EngineController& controller, int channel)
    : controller_(controller), channel_(channel),
      gain_("GAIN", -24.0, 24.0, 0.0, [](double v) { return formatDb(v) + " dB"; }),
      pan_("PAN", -1.0, 1.0, 0.0, panText)
{
    auto& eng = controller_.engine();
    auto& rp = eng.routing().channel[static_cast<size_t>(channel_)];
    auto& dp = eng.dsp(channel_);
    auto& hp = eng.routing().headphones[static_cast<size_t>(channel_)];

    number_.setText("CH " + juce::String(channel_ + 1), juce::dontSendNotification);
    number_.setJustificationType(juce::Justification::centred);
    number_.setColour(juce::Label::textColourId, colours::textDim);
    name_.setJustificationType(juce::Justification::centred);
    name_.setEditable(false, true);
    name_.onTextChange = [this] { controller_.setChannelName(channel_, name_.getText().toStdString()); };
    input_.setJustificationType(juce::Justification::centredLeft);
    input_.setColour(juce::Label::textColourId, colours::textDim);
    input_.setMinimumHorizontalScale(0.8f);
    allowEllipsis(input_); // device names can be any length; the full name is in the tooltip

    gain_.onChange = [&dp](double v) { dp.inputTrimDb.set(static_cast<float>(v)); };
    pan_.onChange = [&rp](double v) { rp.pan.set(static_cast<float>(v)); };
    gate_.onToggle = [&dp](bool on) { dp.gateOn = on; };
    comp_.onToggle = [&dp](bool on) { dp.compOn = on; };
    eq_.onToggle = [&dp](bool on) { dp.eqOn = on; };
    deess_.onToggle = [&dp](bool on) { dp.deesserOn = on; };
    mute_.onToggle = [&rp](bool on) { rp.mute = on; };
    solo_.onToggle = [&rp](bool on) { rp.solo = on; };
    pfl_.onToggle = [&rp](bool on) { rp.pfl = on; };
    fader_.onChange = [&rp](float g) { rp.fader.set(g); };
    rec_.onToggle = [&eng, this](bool on) { eng.recordArm(channel_) = on; };
    mon_.onToggle = [&hp](bool on) { hp.mute = !on; };
    configure_.onClick = [this] {
        if (onConfigure) onConfigure(channel_);
    };
    configure_.setTooltip("Configure channel (microphone setup wizard)");
    gate_.setTooltip("Noise gate");
    comp_.setTooltip("Compressor");
    deess_.setTooltip("De-esser");
    pfl_.setTooltip("Pre-fade listen on the monitor (doesn't change the program)");
    solo_.setTooltip("Solo in place on the monitor only");
    rec_.setTooltip("Record this channel's isolated track");
    mon_.setTooltip("This person's headphones on/off");

    for (juce::Component* c : std::initializer_list<juce::Component*>{&number_, &name_, &input_, &inputLed_, &gain_, &gate_, &comp_, &eq_,
                                                                      &deess_, &pan_, &mute_, &solo_, &pfl_, &fader_, &meter_,
                                                                      &rec_, &mon_, &configure_})
        addAndMakeVisible(c);
    syncFromParams();
}

void ChannelStripView::syncFromParams()
{
    auto& eng = controller_.engine();
    const auto& rp = eng.routing().channel[static_cast<size_t>(channel_)];
    const auto& dp = eng.dsp(channel_);
    const auto& hp = eng.routing().headphones[static_cast<size_t>(channel_)];
    if (!gain_.slider().isMouseButtonDown()) gain_.setValue(dp.inputTrimDb.get());
    if (!pan_.slider().isMouseButtonDown()) pan_.setValue(rp.pan.get());
    if (!fader_.slider().isMouseButtonDown()) fader_.setGain(rp.fader.get());
    gate_.setToggleState(dp.gateOn.load(), juce::dontSendNotification);
    comp_.setToggleState(dp.compOn.load(), juce::dontSendNotification);
    eq_.setToggleState(dp.eqOn.load(), juce::dontSendNotification);
    deess_.setToggleState(dp.deesserOn.load(), juce::dontSendNotification);
    mute_.setToggleState(rp.mute.load(), juce::dontSendNotification);
    solo_.setToggleState(rp.solo.load(), juce::dontSendNotification);
    pfl_.setToggleState(rp.pfl.load(), juce::dontSendNotification);
    rec_.setToggleState(eng.recordArm(channel_).load(), juce::dontSendNotification);
    mon_.setToggleState(!hp.mute.load(), juce::dontSendNotification);
}

void ChannelStripView::update(const ChannelView& v, const EngineMeters& m)
{
    const auto name = juce::String::fromUTF8(v.name.c_str());
    if (name != lastName_ && !name_.isBeingEdited())
    {
        name_.setText(name, juce::dontSendNotification);
        lastName_ = name;
    }
    juce::String in = v.mic.state == EndpointState::None ? juce::String("no mic")
                                                         : juce::String::fromUTF8(v.mic.assignedName.c_str());
    if (v.mic.state != EndpointState::Ok && v.mic.state != EndpointState::None) in = juce::String("[") + toString(v.mic.state) + "] " + in;
    input_.setText(in, juce::dontSendNotification);
    input_.setTooltip(in);
    inputLed_.setColour(ledFor(v.mic.state));
    const auto c = static_cast<size_t>(channel_);
    meter_.setLevels(&m.peak[c], &m.rms[c]);
    // The cough key or a hotkey may have changed a parameter.
    const bool coughClosed = !controller_.engine().routing().channel[c].coughOpen.load();
    mute_.setButtonText(coughClosed ? "COUGH" : "MUTE");
    syncFromParams();
}

void ChannelStripView::resized()
{
    auto b = getLocalBounds().reduced(4);
    const int W = b.getWidth();
    const int H = b.getHeight();
    const int row = juce::jlimit(20, 30, H / 28);
    const int gap = juce::jmax(3, H / 150);

    number_.setFont(juce::FontOptions(juce::jlimit(11.0f, 15.0f, row * 0.55f), juce::Font::bold));
    name_.setFont(juce::FontOptions(juce::jlimit(12.0f, 18.0f, row * 0.7f), juce::Font::bold));
    input_.setFont(juce::FontOptions(juce::jlimit(10.0f, 13.0f, row * 0.5f)));

    number_.setBounds(b.removeFromTop(row * 3 / 4));
    name_.setBounds(b.removeFromTop(row));
    auto inRow = b.removeFromTop(row * 3 / 4);
    inputLed_.setBounds(inRow.removeFromLeft(inRow.getHeight()).reduced(2));
    input_.setBounds(inRow);
    sections_[0] = b.removeFromTop(gap);

    const int knob = juce::jlimit(64, 120, juce::jmin(W, H * 2 / 15));
    gain_.setBounds(b.removeFromTop(knob).withSizeKeepingCentre(juce::jmin(W, knob + 24), knob));
    b.removeFromTop(gap);
    auto dspRow1 = b.removeFromTop(row);
    auto dspRow2 = b.removeFromTop(row);
    gate_.setBounds(dspRow1.removeFromLeft(W / 2).reduced(1));
    comp_.setBounds(dspRow1.reduced(1));
    eq_.setBounds(dspRow2.removeFromLeft(W / 2).reduced(1));
    deess_.setBounds(dspRow2.reduced(1));
    sections_[1] = b.removeFromTop(gap);

    const int panH = juce::jmax(62, knob * 85 / 100);
    pan_.setBounds(b.removeFromTop(panH).withSizeKeepingCentre(juce::jmin(W, panH + 20), panH));
    b.removeFromTop(gap);
    auto msp = b.removeFromTop(row);
    mute_.setBounds(msp.removeFromLeft(W / 3).reduced(1));
    solo_.setBounds(msp.removeFromLeft(W / 3).reduced(1));
    pfl_.setBounds(msp.reduced(1));
    sections_[2] = b.removeFromTop(gap);

    auto bottom = b.removeFromBottom(row);
    rec_.setBounds(bottom.removeFromLeft(W * 30 / 100).reduced(1));
    mon_.setBounds(bottom.removeFromLeft(W * 30 / 100).reduced(1));
    configure_.setBounds(bottom.reduced(1));
    sections_[3] = b.removeFromBottom(gap);

    auto meterArea = b.removeFromRight(juce::jmax(10, W / 7));
    meter_.setBounds(meterArea.reduced(0, 2));
    fader_.setBounds(b.reduced(2, 0));
}

void ChannelStripView::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(colours::panel);
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(colours::outline);
    g.drawRoundedRectangle(b, 5.0f, 1.0f);
    for (const auto& s : sections_)
        if (!s.isEmpty()) g.drawHorizontalLine(s.getCentreY(), static_cast<float>(s.getX() + 6), static_cast<float>(s.getRight() - 6));
}

void ChannelStripView::collectLayoutIssues(std::vector<std::string>& issues) const
{
    gain_.collectLayoutIssues(issues);
    pan_.collectLayoutIssues(issues);
    fader_.collectLayoutIssues(issues);
}

} // namespace pf8::ui
