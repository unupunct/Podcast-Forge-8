#include "ui/TalkbackView.h"

#include <cmath>

namespace pf8::ui {
namespace {
juce::String u8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }
juce::String dbs(double v) { return formatDb(v, 0) + " dB"; }
float dbToGain(double db) { return static_cast<float>(std::pow(10.0, db / 20.0)); }
double gainToDbD(float g) { return g > 1e-6f ? 20.0 * std::log10(g) : -60.0; }

const char* stateText(EndpointState s)
{
    switch (s)
    {
        case EndpointState::Ok: return "OK";
        case EndpointState::Offline: return "OFFLINE";
        case EndpointState::Disconnected: return "DISCONNECTED";
        case EndpointState::PossibleMatch: return "POSSIBLE MATCH - confirm in DEVICES";
        case EndpointState::InUse: return "IN USE by another app";
        case EndpointState::Failed: return "FAILED";
        case EndpointState::None: break;
    }
    return "";
}
} // namespace

// ----- TalkButton -----

void TalkButton::paint(juce::Graphics& g)
{
    const auto& k = controller_.talkbackKey();
    const bool on = k.active();
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(on ? colours::warn : colours::panelRaised);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(on ? colours::warn.brighter(0.4f) : colours::outline);
    g.drawRoundedRectangle(r, 6.0f, 2.0f);
    g.setColour(on ? colours::background : colours::text);
    g.setFont(juce::Font(juce::FontOptions(juce::jmin(26.0f, r.getHeight() * 0.32f), juce::Font::bold)));
    g.drawText("TALK", r.removeFromTop(r.getHeight() * 0.7f), juce::Justification::centredBottom, false);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    bool anyTarget = false;
    for (const auto& t : controller_.engine().routing().talkbackTarget) anyTarget = anyTarget || t.load();
    g.setColour(on || anyTarget ? (on ? colours::background : colours::textDim) : colours::warn);
    g.drawText(!anyTarget ? "no TALK TO targets selected"
                          : (k.latched() ? "LATCHED - press to release" : (on ? "TALKING TO HEADPHONES" : "hold / tap to latch")),
               r,
               juce::Justification::centredTop, true);
}

void TalkButton::mouseDown(const juce::MouseEvent&)
{
    controller_.talkbackPress();
    repaint();
}

void TalkButton::mouseUp(const juce::MouseEvent&)
{
    controller_.talkbackRelease();
    repaint();
}

// ----- TalkbackView -----

TalkbackView::TalkbackView(EngineController& controller)
    : controller_(controller), talk_(controller),
      trim_("MIC TRIM", -12, 24, 0, dbs),
      level_("LEVEL", -30, 6, 0, dbs)
{
    auto& rp = controller_.engine().routing();
    auto label = [this](juce::Label& l, const char* text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        l.setColour(juce::Label::textColourId, colours::textDim);
        addAndMakeVisible(l);
    };
    label(modeLabel_, "KEY");
    label(sourceLabel_, "SOURCE");
    label(deviceLabel_, "TALKBACK MIC");
    label(targetsLabel_, "TALK TO");
    deviceState_.setFont(juce::FontOptions(12.0f));
    allowEllipsis(deviceState_);
    addAndMakeVisible(deviceState_);

    mode_.addItem("Hold / tap to latch", 1);
    mode_.addItem("Momentary (hold)", 2);
    mode_.addItem("Latch (toggle)", 3);
    mode_.setSelectedId(static_cast<int>(controller_.talkbackKey().mode()) + 1, juce::dontSendNotification);
    mode_.onChange = [this] {
        controller_.talkbackKey().setMode(static_cast<TalkbackMode>(mode_.getSelectedId() - 1));
        controller_.engine().routing().talkbackActive = controller_.talkbackKey().active();
    };

    source_.addItem("Dedicated talkback mic", 1);
    for (int c = 0; c < kNumChannels; ++c) source_.addItem("Mic of CH " + juce::String(c + 1), c + 2);
    source_.setSelectedId(rp.talkbackSource.load() + 2, juce::dontSendNotification);
    source_.onChange = [this] {
        controller_.engine().routing().talkbackSource = source_.getSelectedId() - 2;
        refreshSource();
    };
    device_.onClick = [this] { showDeviceMenu(); };
    inputChannel_.onChange = [this] {
        const auto s = controller_.status();
        const int ch = inputChannel_.getSelectedId() - 2;
        if (ch != s.talkbackChannel && !s.talkback.endpointId.empty())
            controller_.assignTalkbackMic(s.talkback.endpointId, ch);
    };

    trim_.setValue(gainToDbD(rp.talkbackMicGain.get()));
    trim_.onChange = [&rp](double v) { rp.talkbackMicGain.set(dbToGain(v)); };
    level_.setValue(gainToDbD(rp.talkbackLevel.get()));
    level_.onChange = [&rp](double v) { rp.talkbackLevel.set(v <= -29.5 ? 0.0f : dbToGain(v)); };

    for (int h = 0; h < kNumChannels; ++h)
    {
        auto& t = targets_[static_cast<size_t>(h)];
        t.setToggleState(rp.talkbackTarget[static_cast<size_t>(h)].load(), juce::dontSendNotification);
        t.onToggle = [&rp, h](bool on) { rp.talkbackTarget[static_cast<size_t>(h)] = on; };
        addAndMakeVisible(t);
    }
    auto setAll = [this, &rp](bool on) {
        for (int h = 0; h < kNumChannels; ++h)
        {
            rp.talkbackTarget[static_cast<size_t>(h)] = on;
            targets_[static_cast<size_t>(h)].setToggleState(on, juce::dontSendNotification);
        }
    };
    all_.onClick = [setAll] { setAll(true); };
    none_.onClick = [setAll] { setAll(false); };
    dim_.setToggleState(rp.talkbackDim.load(), juce::dontSendNotification);
    dim_.onToggle = [&rp](bool on) { rp.talkbackDim = on; };
    dim_.setTooltip("Lower the rest of the target headphone mix while talking back");
    toProgram_.setToggleState(rp.talkbackToProgram.load(), juce::dontSendNotification);
    toProgram_.onToggle = [&rp](bool on) { rp.talkbackToProgram = on; };
    toProgram_.setTooltip("Off (default): talkback never reaches Main, Clean Feed or the recording.");

    for (juce::Component* c : std::initializer_list<juce::Component*>{&talk_, &mode_, &source_, &device_, &inputChannel_, &trim_, &level_,
                                                                      &all_, &none_, &dim_, &toProgram_})
        addAndMakeVisible(c);
    refreshSource();
    timerCallback();
    startTimerHz(15);
}

TalkbackView::~TalkbackView() { stopTimer(); }

void TalkbackView::refreshSource()
{
    const bool dedicated = source_.getSelectedId() == 1;
    device_.setEnabled(dedicated);
    inputChannel_.setEnabled(dedicated && controller_.status().talkback.state != EndpointState::None);
    trim_.setEnabled(dedicated);
}

void TalkbackView::showDeviceMenu()
{
    juce::PopupMenu m;
    std::vector<std::string> ids;
    const auto current = controller_.status().talkback.endpointId;
    m.addItem(1, "-- none --", true, current.empty());
    for (const auto& d : controller_.registry().devices())
    {
        if (d.raw.flow != Flow::Capture || !d.online()) continue;
        ids.push_back(d.raw.endpointId);
        m.addItem(static_cast<int>(ids.size()) + 1, u8(d.raw.friendlyName) + "   (" + toString(d.kind) + ")", true, d.raw.endpointId == current);
    }
    juce::Component::SafePointer<TalkbackView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&device_), [safe, ids](int r) {
        if (!safe || r == 0) return;
        if (r == 1) safe->controller_.assignTalkbackMic(std::nullopt);
        else if (static_cast<size_t>(r - 2) < ids.size()) safe->controller_.assignTalkbackMic(ids[static_cast<size_t>(r - 2)]);
    });
}

void TalkbackView::timerCallback()
{
    const auto s = controller_.status();
    const auto& tb = s.talkback;
    device_.setButtonText(tb.state == EndpointState::None ? juce::String("-- no talkback mic --") : u8(tb.assignedName));
    deviceState_.setText(stateText(tb.state), juce::dontSendNotification);
    deviceState_.setColour(juce::Label::textColourId, tb.state == EndpointState::Ok ? colours::ok : colours::warn);
    const int channels = tb.state == EndpointState::Ok ? tb.deviceChannels : 0;
    if (channels != lastChannels_)
    {
        lastChannels_ = channels;
        inputChannel_.clear(juce::dontSendNotification);
        inputChannel_.addItem("Mix of all inputs", 1);
        for (int c = 0; c < std::max(channels, s.talkbackChannel + 1); ++c) inputChannel_.addItem("In " + juce::String(c + 1), c + 2);
    }
    inputChannel_.setSelectedId(s.talkbackChannel + 2, juce::dontSendNotification);
    inputChannel_.setEnabled(source_.getSelectedId() == 1 && tb.state != EndpointState::None);
    const float pk = controller_.meters().talkbackPeak;
    peak_ = std::max(pk, peak_ * 0.85f);
    talk_.repaint();
    repaint(meter_.expanded(2));
}

void TalkbackView::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    if (!meter_.isEmpty())
    {
        auto r = meter_.toFloat();
        g.setColour(colours::panelRaised);
        g.fillRoundedRectangle(r, 2.0f);
        const double db = gainToDbD(peak_);
        const float frac = static_cast<float>(juce::jlimit(0.0, 1.0, (db + 60.0) / 60.0));
        g.setColour(db > -3.0 ? colours::error : (db > -12.0 ? colours::warn : colours::ok));
        g.fillRoundedRectangle(r.withWidth(r.getWidth() * frac), 2.0f);
    }
    if (!lockNote_.isEmpty())
    {
        const bool open = controller_.engine().routing().talkbackToProgram.load();
        g.setColour(open ? colours::warn : colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawFittedText(open ? "Talkback is ALSO sent to Main, Clean Feed and the recording."
                              : "LOCKED: talkback never reaches Main, Clean Feed or the recording.",
                         lockNote_, juce::Justification::centredLeft, 2);
    }
}

void TalkbackView::resized()
{
    auto b = getLocalBounds().reduced(8);
    // Left: the key.
    auto left = b.removeFromLeft(juce::jlimit(180, 260, b.getWidth() / 5));
    b.removeFromLeft(12);
    auto keyRow = left.removeFromBottom(26);
    modeLabel_.setBounds(keyRow.removeFromLeft(36));
    mode_.setBounds(keyRow);
    left.removeFromBottom(6);
    meter_ = left.removeFromBottom(10);
    left.removeFromBottom(6);
    talk_.setBounds(left);

    // Middle: the mic.
    auto mid = b.removeFromLeft(b.getWidth() / 2);
    b.removeFromLeft(12);
    auto row = [&](juce::Rectangle<int>& area, juce::Label& l, juce::Component& c) {
        auto r = area.removeFromTop(26);
        l.setBounds(r.removeFromLeft(110));
        c.setBounds(r);
        area.removeFromTop(6);
    };
    row(mid, sourceLabel_, source_);
    row(mid, deviceLabel_, device_);
    {
        auto r = mid.removeFromTop(26);
        deviceState_.setBounds(r.removeFromLeft(r.getWidth() / 2));
        r.removeFromLeft(6);
        inputChannel_.setBounds(r);
        mid.removeFromTop(6);
    }
    const int kw = juce::jmin(110, mid.getWidth() / 2);
    trim_.setBounds(mid.removeFromLeft(kw));
    level_.setBounds(mid.removeFromLeft(kw));

    // Right: targets and the program lock.
    auto top = b.removeFromTop(26);
    targetsLabel_.setBounds(top.removeFromLeft(70));
    none_.setBounds(top.removeFromRight(64));
    top.removeFromRight(6);
    all_.setBounds(top.removeFromRight(64));
    b.removeFromTop(6);
    auto grid = b.removeFromTop(60);
    const int cw = grid.getWidth() / 4;
    for (int h = 0; h < kNumChannels; ++h)
    {
        const int rr = h / 4, cc = h % 4;
        targets_[static_cast<size_t>(h)].setBounds(grid.getX() + cc * cw + 2, grid.getY() + rr * 30 + 2, cw - 4, 26);
    }
    b.removeFromTop(8);
    auto toggles = b.removeFromTop(26);
    dim_.setBounds(toggles.removeFromLeft(toggles.getWidth() / 2).reduced(2, 0));
    toProgram_.setBounds(toggles.reduced(2, 0));
    b.removeFromTop(4);
    lockNote_ = b.removeFromTop(36);
}

void TalkbackView::collectLayoutIssues(std::vector<std::string>& issues) const
{
    trim_.collectLayoutIssues(issues);
    level_.collectLayoutIssues(issues);
    if (talk_.getHeight() < 60) issues.push_back("talkback: TALK button too small");
}

} // namespace pf8::ui
