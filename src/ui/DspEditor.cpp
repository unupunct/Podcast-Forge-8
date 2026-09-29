#include "ui/DspEditor.h"

#include <cmath>

#include "dsp/Biquad.h"
#include "dsp/Presets.h"

namespace pf8::ui {
namespace {

juce::String hz(double v) { return v >= 1000.0 ? juce::String(v / 1000.0, v >= 10000.0 ? 1 : 2) + "k" : juce::String(juce::roundToInt(v)); }
juce::String dbs(double v) { return formatDb(v) + " dB"; }
juce::String ms(double v) { return v < 10.0 ? juce::String(v, 1) + " ms" : juce::String(juce::roundToInt(v)) + " ms"; }
juce::String ratio(double v) { return juce::String(v, v < 10 ? 1 : 0) + ":1"; }

} // namespace

// ---------------------------------------------------------------------------------------------
void EqCurve::paint(juce::Graphics& g)
{
    using namespace colours;
    auto b = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(panelRaised);
    g.fillRoundedRectangle(b, 4.0f);
    const double fs = 48000.0;
    auto xFor = [&](double f) { return b.getX() + static_cast<float>(std::log(f / 20.0) / std::log(1000.0)) * b.getWidth(); };
    auto yFor = [&](double db) { return b.getCentreY() - static_cast<float>(db / 18.0) * b.getHeight() * 0.45f; };
    g.setColour(outline);
    for (double f : {100.0, 1000.0, 10000.0}) g.drawVerticalLine(static_cast<int>(xFor(f)), b.getY(), b.getBottom());
    g.drawHorizontalLine(static_cast<int>(yFor(0.0)), b.getX(), b.getRight());
    g.setColour(textDim);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    for (auto [f, t] : {std::pair{100.0, "100"}, std::pair{1000.0, "1k"}, std::pair{10000.0, "10k"}})
        g.drawText(t, static_cast<int>(xFor(f)) + 2, static_cast<int>(b.getBottom()) - 13, 30, 12, juce::Justification::left, false);

    const bool eqOn = p_.eqOn.load(), hpfOn = p_.hpfOn.load();
    std::array<dsp::BiquadCoeffs, 4> bands;
    for (size_t i = 0; i < 4; ++i)
    {
        const auto& bp = p_.eq[i];
        const auto t = bp.type.load();
        bands[i] = t == EqBandType::LowShelf ? dsp::BiquadCoeffs::lowShelf(fs, bp.freq.get(), bp.q.get(), bp.gainDb.get())
                 : t == EqBandType::HighShelf ? dsp::BiquadCoeffs::highShelf(fs, bp.freq.get(), bp.q.get(), bp.gainDb.get())
                                              : dsp::BiquadCoeffs::peak(fs, bp.freq.get(), bp.q.get(), bp.gainDb.get());
    }
    const auto hp0 = dsp::BiquadCoeffs::highPass(fs, p_.hpfHz.get(), p_.hpf24dB.load() ? 0.5411961 : 0.7071068);
    const auto hp1 = dsp::BiquadCoeffs::highPass(fs, p_.hpfHz.get(), 1.3065630);
    juce::Path path;
    for (int px = 0; px <= static_cast<int>(b.getWidth()); px += 2)
    {
        const double f = 20.0 * std::pow(1000.0, px / static_cast<double>(b.getWidth()));
        double m = 1.0;
        if (eqOn)
            for (size_t i = 0; i < 4; ++i)
                if (p_.eq[i].on.load()) m *= bands[i].magnitude(fs, f);
        if (hpfOn) m *= hp0.magnitude(fs, f) * (p_.hpf24dB.load() ? hp1.magnitude(fs, f) : 1.0);
        const float y = juce::jlimit(b.getY(), b.getBottom(), yFor(20.0 * std::log10(std::max(m, 1e-6))));
        if (px == 0) path.startNewSubPath(b.getX(), y);
        else path.lineTo(b.getX() + static_cast<float>(px), y);
    }
    g.setColour(eqOn || hpfOn ? accent : textDim);
    g.strokePath(path, juce::PathStrokeType(2.0f));
}

// ---------------------------------------------------------------------------------------------
DspSection::DspSection(juce::String title, std::atomic<bool>* enable)
    : title_(std::move(title)), enable_(enable), toggle_(title_, colours::dsp)
{
    if (enable_)
    {
        toggle_.onToggle = [this](bool on) { *enable_ = on; };
        addAndMakeVisible(toggle_);
    }
    sync();
}

void DspSection::addKnob(Knob& k)
{
    knobs_.push_back(&k);
    addAndMakeVisible(k);
}

void DspSection::sync()
{
    if (enable_) toggle_.setToggleState(enable_->load(), juce::dontSendNotification);
    repaint(activity_);
}

void DspSection::resized()
{
    auto b = getLocalBounds().reduced(6);
    auto head = b.removeFromTop(26);
    if (enable_) toggle_.setBounds(head.removeFromLeft(juce::jmin(110, head.getWidth())).reduced(0, 1));
    activity_ = head.withTrimmedLeft(8);
    b.removeFromTop(4);
    const int n = static_cast<int>(knobs_.size());
    const int total = n + extraWeight;
    if (total == 0) return;
    const float w = static_cast<float>(b.getWidth()) / static_cast<float>(total);
    for (int i = 0; i < n; ++i)
        knobs_[static_cast<size_t>(i)]->setBounds(juce::Rectangle<int>(b.getX() + static_cast<int>(w * static_cast<float>(i)), b.getY(),
                                                                       static_cast<int>(w), b.getHeight()).reduced(2));
    if (extra)
    {
        auto slot = juce::Rectangle<int>(b.getX() + static_cast<int>(w * static_cast<float>(n)), b.getY(),
                                         b.getRight() - (b.getX() + static_cast<int>(w * static_cast<float>(n))), b.getHeight()).reduced(2);
        // Buttons get a normal button size; other extras (the EQ curve) fill their slot.
        if (dynamic_cast<juce::Button*>(extra)) slot = slot.withSizeKeepingCentre(juce::jmin(slot.getWidth(), 110), juce::jmin(slot.getHeight(), 30));
        extra->setBounds(slot);
    }
}

void DspSection::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(colours::panel);
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(colours::outline);
    g.drawRoundedRectangle(b, 5.0f, 1.0f);
    if (!enable_)
    {
        g.setColour(colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(title_, getLocalBounds().reduced(10, 6).removeFromTop(26), juce::Justification::centredLeft, false);
    }
    if (drawActivity) drawActivity(g, activity_);
}

// ---------------------------------------------------------------------------------------------
Knob& DspEditor::knob(const char* caption, double min, double max, double def, std::function<juce::String(double)> fmt,
                      AtomicParam& param, double skewMid)
{
    auto k = std::make_unique<Knob>(caption, min, max, def, std::move(fmt));
    if (skewMid > 0.0) k->slider().setSkewFactorFromMidPoint(skewMid);
    k->setValue(param.get());
    k->onChange = [&param, this](double v) {
        param.set(static_cast<float>(v));
        curve_.repaint();
    };
    bindings_.emplace_back(k.get(), &param);
    knobs_.push_back(std::move(k));
    return *knobs_.back();
}

DspEditor::DspEditor(EngineController& controller, int channel)
    : controller_(controller), channel_(channel), p_(controller.engine().dsp(channel)), curve_(controller.engine().dsp(channel))
{
    const auto a = controller_.assignments();
    title_.setText("CHANNEL " + juce::String(channel_ + 1) + "  -  " + juce::String::fromUTF8(a.ch[static_cast<size_t>(channel_)].name.c_str()),
                   juce::dontSendNotification);
    title_.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    allowEllipsis(title_);
    addAndMakeVisible(title_);

    compPreset_.addItem("Compressor preset...", 1);
    for (size_t i = 0; i < dsp::kCompPresets.size(); ++i)
        compPreset_.addItem(juce::String(std::string(dsp::name(dsp::kCompPresets[i]))), static_cast<int>(i) + 2);
    compPreset_.setSelectedId(1, juce::dontSendNotification);
    compPreset_.onChange = [this] {
        const int id = compPreset_.getSelectedId();
        if (id >= 2) dsp::apply(dsp::kCompPresets[static_cast<size_t>(id - 2)], p_);
        compPreset_.setSelectedId(1, juce::dontSendNotification);
        syncFromParams();
    };
    eqPreset_.addItem("EQ preset...", 1);
    for (size_t i = 0; i < dsp::kEqPresets.size(); ++i)
        eqPreset_.addItem(juce::String(std::string(dsp::name(dsp::kEqPresets[i]))), static_cast<int>(i) + 2);
    eqPreset_.setSelectedId(1, juce::dontSendNotification);
    eqPreset_.onChange = [this] {
        const int id = eqPreset_.getSelectedId();
        if (id >= 2) dsp::apply(dsp::kEqPresets[static_cast<size_t>(id - 2)], p_);
        eqPreset_.setSelectedId(1, juce::dontSendNotification);
        syncFromParams();
        curve_.repaint();
    };
    wizard_.onClick = [this] {
        if (onRunWizard) onRunWizard(channel_);
    };
    wizard_.setTooltip("Measure noise floor and speech level, then recommend gain and dynamics. Never changes hardware gain.");
    for (juce::Component* c : std::initializer_list<juce::Component*>{&compPreset_, &eqPreset_, &wizard_}) addAndMakeVisible(c);

    input_ = std::make_unique<DspSection>("INPUT", nullptr);
    input_->addKnob(knob("TRIM", -24, 24, 0, dbs, p_.inputTrimDb));
    polarity_.onToggle = [this](bool on) { p_.polarityInvert = on; };
    input_->extra = &polarity_;
    input_->extraWeight = 1;
    input_->addChildComponent(polarity_);
    polarity_.setVisible(true);

    hpf_ = std::make_unique<DspSection>("HIGH-PASS", &p_.hpfOn);
    hpf_->addKnob(knob("FREQ", 20, 300, 80, [](double v) { return hz(v) + " Hz"; }, p_.hpfHz));
    hpf24_.onToggle = [this](bool on) {
        p_.hpf24dB = on;
        curve_.repaint();
    };
    hpf_->extra = &hpf24_;
    hpf_->extraWeight = 1;
    hpf_->addAndMakeVisible(hpf24_);

    gate_ = std::make_unique<DspSection>("GATE", &p_.gateOn);
    gate_->addKnob(knob("THRESH", -80, 0, -50, dbs, p_.gateThresholdDb));
    gate_->addKnob(knob("RANGE", -80, 0, -40, dbs, p_.gateRangeDb));
    gate_->addKnob(knob("ATTACK", 0.1, 50, 2, ms, p_.gateAttackMs, 5.0));
    gate_->addKnob(knob("HOLD", 0, 500, 80, ms, p_.gateHoldMs, 80.0));
    gate_->addKnob(knob("RELEASE", 5, 2000, 150, ms, p_.gateReleaseMs, 150.0));
    gate_->drawActivity = [this](juce::Graphics& g, juce::Rectangle<int> r) {
        const bool open = meters_.gateGain > 0.9f;
        g.setColour(p_.gateOn.load() ? (open ? colours::ok : colours::warn) : colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText(p_.gateOn.load() ? (open ? "OPEN" : "CLOSED") : "", r, juce::Justification::centredRight, false);
    };

    eq_ = std::make_unique<DspSection>("EQ", &p_.eqOn);
    const char* bandNames[4] = {"LOW", "LO-MID", "HI-MID", "HIGH"};
    for (size_t b = 0; b < 4; ++b)
    {
        auto& bp = p_.eq[b];
        eq_->addKnob(knob((std::string(bandNames[b]) + " F").c_str(), 20, 20000, bp.freq.get(), [](double v) { return hz(v); }, bp.freq, 1000.0));
        eq_->addKnob(knob((std::string(bandNames[b]) + " G").c_str(), -18, 18, 0, dbs, bp.gainDb));
        eq_->addKnob(knob((std::string(bandNames[b]) + " Q").c_str(), 0.1, 10, bp.q.get(), [](double v) { return juce::String(v, 2); }, bp.q, 1.0));
        auto& tc = bandType_[b];
        tc.addItem("Peak", 1);
        tc.addItem("Low shelf", 2);
        tc.addItem("High shelf", 3);
        tc.onChange = [this, b] {
            p_.eq[b].type = static_cast<EqBandType>(bandType_[b].getSelectedId() - 1);
            curve_.repaint();
        };
        addAndMakeVisible(tc);
    }

    deess_ = std::make_unique<DspSection>("DE-ESSER", &p_.deesserOn);
    deess_->addKnob(knob("FREQ", 3000, 12000, 6500, [](double v) { return hz(v); }, p_.deessFreq));
    deess_->addKnob(knob("THRESH", -60, 0, -30, dbs, p_.deessThresholdDb));
    deess_->addKnob(knob("AMOUNT", 0, 18, 6, dbs, p_.deessAmountDb));
    deess_->drawActivity = [this](juce::Graphics& g, juce::Rectangle<int> r) {
        g.setColour(colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText(meters_.deessDb > 0.05f ? "-" + juce::String(meters_.deessDb, 1) + " dB" : juce::String("0.0 dB"), r,
                   juce::Justification::centredRight, false);
    };

    comp_ = std::make_unique<DspSection>("COMPRESSOR", &p_.compOn);
    comp_->addKnob(knob("THRESH", -60, 0, -20, dbs, p_.compThresholdDb));
    comp_->addKnob(knob("RATIO", 1, 20, 4, ratio, p_.compRatio, 4.0));
    comp_->addKnob(knob("ATTACK", 0.1, 100, 5, ms, p_.compAttackMs, 10.0));
    comp_->addKnob(knob("RELEASE", 10, 2000, 100, ms, p_.compReleaseMs, 150.0));
    comp_->addKnob(knob("KNEE", 0, 12, 6, dbs, p_.compKneeDb));
    comp_->addKnob(knob("MAKEUP", 0, 24, 6, dbs, p_.compMakeupDb));
    autoMakeup_.onToggle = [this](bool on) { p_.compAutoMakeup = on; };
    comp_->extra = &autoMakeup_;
    comp_->extraWeight = 1;
    comp_->addAndMakeVisible(autoMakeup_);
    comp_->drawActivity = [this](juce::Graphics& g, juce::Rectangle<int> r) {
        const float gr = juce::jlimit(0.0f, 24.0f, std::abs(meters_.compGrDb));
        auto bar = r.removeFromRight(juce::jmin(160, r.getWidth())).reduced(0, 7).toFloat();
        g.setColour(colours::panelRaised);
        g.fillRect(bar);
        g.setColour(colours::warn);
        g.fillRect(bar.withWidth(bar.getWidth() * gr / 24.0f).withX(bar.getRight() - bar.getWidth() * gr / 24.0f));
        g.setColour(colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("GR " + juce::String(gr < 0.05f ? 0.0f : gr, 1) + " dB", r.withTrimmedRight(juce::jmin(160, r.getWidth()) + 6),
                   juce::Justification::centredRight, false);
    };

    limiter_ = std::make_unique<DspSection>("LIMITER", &p_.limiterOn);
    limiter_->addKnob(knob("CEILING", -12, 0, -1, dbs, p_.limiterCeilingDb));
    reverb_ = std::make_unique<DspSection>("REVERB", nullptr);
    reverb_->addKnob(knob("SEND", 0, 1, 0, [](double v) { return juce::String(juce::roundToInt(v * 100)) + " %"; }, p_.reverbSend));

    for (auto* s : {input_.get(), hpf_.get(), gate_.get(), eq_.get(), deess_.get(), comp_.get(), limiter_.get(), reverb_.get()})
        addAndMakeVisible(*s);
    addAndMakeVisible(curve_);
    syncFromParams();
    startTimerHz(20);
}

DspEditor::~DspEditor() { stopTimer(); }

void DspEditor::syncFromParams()
{
    for (auto& [k, p] : bindings_)
        if (!k->slider().isMouseButtonDown()) k->setValue(p->get());
    polarity_.setToggleState(p_.polarityInvert.load(), juce::dontSendNotification);
    hpf24_.setToggleState(p_.hpf24dB.load(), juce::dontSendNotification);
    autoMakeup_.setToggleState(p_.compAutoMakeup.load(), juce::dontSendNotification);
    for (size_t b = 0; b < 4; ++b) bandType_[b].setSelectedId(static_cast<int>(p_.eq[b].type.load()) + 1, juce::dontSendNotification);
    for (auto* s : {hpf_.get(), gate_.get(), eq_.get(), deess_.get(), comp_.get(), limiter_.get()}) s->sync();
}

void DspEditor::timerCallback()
{
    meters_ = controller_.meters().strip[static_cast<size_t>(channel_)];
    syncFromParams();
    for (auto* s : {gate_.get(), deess_.get(), comp_.get()}) s->repaint();
    curve_.repaint();
}

void DspEditor::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void DspEditor::resized()
{
    auto b = getLocalBounds().reduced(12);
    auto head = b.removeFromTop(34);
    wizard_.setBounds(head.removeFromRight(190).reduced(0, 2));
    head.removeFromRight(8);
    eqPreset_.setBounds(head.removeFromRight(170).reduced(0, 3));
    head.removeFromRight(8);
    compPreset_.setBounds(head.removeFromRight(200).reduced(0, 3));
    title_.setBounds(head);
    b.removeFromTop(8);

    const int rowH = (b.getHeight() - 16) / 3;
    auto r1 = b.removeFromTop(rowH);
    b.removeFromTop(8);
    auto r2 = b.removeFromTop(rowH);
    b.removeFromTop(8);
    auto r3 = b;

    const float W = static_cast<float>(r1.getWidth());
    input_->setBounds(r1.removeFromLeft(static_cast<int>(W * 0.18f)).reduced(3));
    hpf_->setBounds(r1.removeFromLeft(static_cast<int>(W * 0.20f)).reduced(3));
    gate_->setBounds(r1.reduced(3));

    // EQ row: 12 knobs + type selectors, with the curve on the right.
    auto curveArea = r2.removeFromRight(static_cast<int>(W * 0.26f)).reduced(3);
    curve_.setBounds(curveArea);
    auto eqArea = r2.reduced(3);
    auto types = eqArea.removeFromBottom(26);
    eq_->setBounds(eqArea);
    const float bw = static_cast<float>(types.getWidth() - 12) / 4.0f;
    for (size_t i = 0; i < 4; ++i)
        bandType_[i].setBounds(juce::Rectangle<int>(types.getX() + 6 + static_cast<int>(bw * static_cast<float>(i)), types.getY(),
                                                    static_cast<int>(bw) - 6, types.getHeight()));

    deess_->setBounds(r3.removeFromLeft(static_cast<int>(W * 0.24f)).reduced(3));
    reverb_->setBounds(r3.removeFromRight(static_cast<int>(W * 0.10f)).reduced(3));
    limiter_->setBounds(r3.removeFromRight(static_cast<int>(W * 0.11f)).reduced(3));
    comp_->setBounds(r3.reduced(3));
}

void DspEditor::collectLayoutIssues(std::vector<std::string>& issues) const
{
    for (const auto& k : knobs_) k->collectLayoutIssues(issues);
}

} // namespace pf8::ui
