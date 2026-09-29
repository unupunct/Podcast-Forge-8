#include "ui/ChannelsView.h"

#include <algorithm>
#include <cmath>

#include "ui/LookAndFeel.h"

namespace pf8::ui {
namespace {

juce::String u8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

float toDb(float lin) { return lin > 1e-6f ? 20.0f * std::log10(lin) : -120.0f; }

juce::Colour stateColour(EndpointState s)
{
    switch (s)
    {
        case EndpointState::Ok: return colours::ok;
        case EndpointState::None: return colours::textDim;
        case EndpointState::PossibleMatch: return colours::accent;
        case EndpointState::Offline:
        case EndpointState::Disconnected: return colours::warn;
        default: return colours::error;
    }
}

juce::Colour syncColour(SyncStatus s)
{
    switch (s)
    {
        case SyncStatus::Locked:
        case SyncStatus::Native: return colours::ok;
        case SyncStatus::Converging:
        case SyncStatus::Priming: return colours::warn;
        case SyncStatus::Unstable: return colours::error;
        default: return colours::textDim;
    }
}

} // namespace

void LevelMeter::setLevel(float peakLinear, float rmsLinear)
{
    const float p = toDb(peakLinear), r = toDb(rmsLinear);
    // Fast attack, ~20 dB/s release at a 20 Hz refresh.
    peakDb_ = p > peakDb_ ? p : std::max(p, peakDb_ - 1.0f);
    rmsDb_ = r > rmsDb_ ? r : std::max(r, rmsDb_ - 1.0f);
    if (p >= holdDb_) { holdDb_ = p; holdFrames_ = 30; }
    else if (--holdFrames_ < 0) holdDb_ = std::max(p, holdDb_ - 1.0f);
    repaint();
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(colours::panelRaised);
    g.fillRoundedRectangle(b, 2.0f);
    auto pos = [&](float db) { return b.getX() + b.getWidth() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    const float x = pos(peakDb_);
    juce::ColourGradient grad(colours::ok, b.getX(), 0, colours::error, b.getRight(), 0, false);
    grad.addColour(pos(-18.0f) / std::max(1.0f, b.getRight()), colours::ok);
    grad.addColour(pos(-6.0f) / std::max(1.0f, b.getRight()), colours::warn);
    g.setGradientFill(grad);
    g.fillRect(juce::Rectangle<float>(b.getX(), b.getY() + 2, x - b.getX(), b.getHeight() - 4));
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.fillRect(juce::Rectangle<float>(b.getX(), b.getCentreY() - 1, pos(rmsDb_) - b.getX(), 2.0f));
    g.setColour(holdDb_ > -0.5f ? colours::error : colours::text);
    g.fillRect(juce::Rectangle<float>(pos(holdDb_) - 1, b.getY(), 2, b.getHeight()));
}

ChannelsView::ChannelsView(EngineController& controller) : controller_(controller)
{
    const char* heads[] = {"CH", "NAME", "MICROPHONE", "INPUT", "HEADPHONES", "SYNC", "LEVEL"};
    for (int i = 0; i < 7; ++i)
    {
        header_[i].setText(heads[i], juce::dontSendNotification);
        header_[i].setColour(juce::Label::textColourId, colours::textDim);
        header_[i].setFont(juce::FontOptions(12.0f, juce::Font::bold));
        addAndMakeVisible(header_[i]);
    }

    for (int i = 0; i < kNumChannels; ++i)
    {
        auto r = std::make_unique<Row>();
        r->number.setText(juce::String(i + 1), juce::dontSendNotification);
        r->number.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        r->number.setJustificationType(juce::Justification::centred);
        r->name.setColour(juce::TextEditor::backgroundColourId, colours::panelRaised);
        r->name.setColour(juce::TextEditor::outlineColourId, colours::outline);
        r->name.onFocusLost = r->name.onReturnKey = [this, i] {
            controller_.setChannelName(i, rows_[static_cast<size_t>(i)]->name.getText().toStdString());
        };
        r->mic.onChange = [this, i] {
            auto& row = *rows_[static_cast<size_t>(i)];
            const int id = row.mic.getSelectedId();
            const int micCh = row.micChannel.getSelectedId() - 2; // id 1 = Mix (-1)
            if (id == 1) controller_.assignMic(i, std::nullopt, micCh);
            else if (id >= 2 && static_cast<size_t>(id - 2) < row.micIds.size())
                controller_.assignMic(i, row.micIds[static_cast<size_t>(id - 2)], micCh);
        };
        r->micChannel.addItem("Mix", 1);
        for (int c = 0; c < 8; ++c) r->micChannel.addItem("In " + juce::String(c + 1), c + 2);
        r->micChannel.setSelectedId(1, juce::dontSendNotification);
        r->micChannel.onChange = r->mic.onChange;
        r->headphones.onChange = [this, i] {
            auto& row = *rows_[static_cast<size_t>(i)];
            const int id = row.headphones.getSelectedId();
            if (id == 1) controller_.assignHeadphones(i, std::nullopt, 0);
            else if (id >= 2 && static_cast<size_t>(id - 2) < row.hpIds.size())
                controller_.assignHeadphones(i, row.hpIds[static_cast<size_t>(id - 2)], 0);
        };
        r->acceptMic.onClick = [this, i] { controller_.acceptPossibleMatch(i, true); };
        r->acceptHp.onClick = [this, i] { controller_.acceptPossibleMatch(i, false); };
        for (auto* l : {&r->micStatus, &r->hpStatus, &r->sync}) l->setFont(juce::FontOptions(12.0f));

        for (juce::Component* c : std::initializer_list<juce::Component*>{&r->number, &r->name, &r->mic, &r->micChannel,
                                                                          &r->headphones, &r->micStatus, &r->hpStatus,
                                                                          &r->sync, &r->meter, &r->acceptMic, &r->acceptHp})
            addAndMakeVisible(c);
        r->acceptMic.setVisible(false);
        r->acceptHp.setVisible(false);
        rows_[static_cast<size_t>(i)] = std::move(r);
    }
    timerCallback();
    startTimerHz(20);
}

ChannelsView::~ChannelsView() { stopTimer(); }

void ChannelsView::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    const int rowH = rows_[0]->number.getHeight() + 16;
    for (int i = 0; i < kNumChannels; ++i)
    {
        auto top = rows_[static_cast<size_t>(i)]->number.getY() - 8;
        g.setColour(i % 2 ? colours::panel : colours::background.brighter(0.02f));
        g.fillRect(0, top, getWidth(), rowH);
    }
}

void ChannelsView::resized()
{
    auto area = getLocalBounds().reduced(12);
    const float W = static_cast<float>(area.getWidth());
    const float cols[] = {0.04f, 0.13f, 0.25f, 0.07f, 0.23f, 0.12f, 0.16f};
    auto headRow = area.removeFromTop(24);
    {
        int x = headRow.getX();
        for (int c = 0; c < 7; ++c)
        {
            const int w = static_cast<int>(W * cols[c]);
            header_[c].setBounds(x, headRow.getY(), w, headRow.getHeight());
            x += w;
        }
    }
    const int rowH = std::max(64, area.getHeight() / kNumChannels);
    for (int i = 0; i < kNumChannels; ++i)
    {
        auto& r = *rows_[static_cast<size_t>(i)];
        auto row = area.removeFromTop(rowH).reduced(0, 8);
        int x = row.getX();
        auto cell = [&](int c) {
            const int w = static_cast<int>(W * cols[c]);
            juce::Rectangle<int> b(x, row.getY(), w - 8, row.getHeight());
            x += w;
            return b;
        };
        r.number.setBounds(cell(0));
        r.name.setBounds(cell(1).withSizeKeepingCentre(static_cast<int>(W * cols[1]) - 8, 28));
        auto m = cell(2);
        r.mic.setBounds(m.removeFromTop(28));
        auto ms = m.removeFromTop(20);
        r.acceptMic.setBounds(ms.removeFromRight(48));
        r.micStatus.setBounds(ms);
        r.micChannel.setBounds(cell(3).removeFromTop(28));
        auto h = cell(4);
        r.headphones.setBounds(h.removeFromTop(28));
        auto hs = h.removeFromTop(20);
        r.acceptHp.setBounds(hs.removeFromRight(48));
        r.hpStatus.setBounds(hs);
        r.sync.setBounds(cell(5));
        r.meter.setBounds(cell(6).withSizeKeepingCentre(static_cast<int>(W * cols[6]) - 8, 14));
    }
}

void ChannelsView::rebuildDeviceLists()
{
    const auto devices = controller_.registry().devices();
    const auto a = controller_.assignments();
    for (int i = 0; i < kNumChannels; ++i)
    {
        auto& r = *rows_[static_cast<size_t>(i)];
        const auto& ca = a.ch[static_cast<size_t>(i)];
        auto fill = [&](juce::ComboBox& box, std::vector<std::string>& ids, Flow flow,
                        const std::optional<DeviceIdentity>& assigned) {
            box.clear(juce::dontSendNotification);
            ids.clear();
            box.addItem("-- none --", 1);
            std::vector<const DeviceInfo*> list;
            for (const auto& d : devices)
                if (d.raw.flow == flow && (d.online() || (assigned && assigned->endpointId == d.raw.endpointId)))
                    list.push_back(&d);
            std::stable_sort(list.begin(), list.end(), [](const DeviceInfo* x, const DeviceInfo* y) {
                const bool ux = x->usb.has_value(), uy = y->usb.has_value();
                if (ux != uy) return ux; // USB devices first
                return x->raw.friendlyName < y->raw.friendlyName;
            });
            int selected = 1;
            for (const auto* d : list)
            {
                ids.push_back(d->raw.endpointId);
                const int id = static_cast<int>(ids.size()) + 1;
                box.addItem(u8(d->displayName()) + "   (" + toString(d->kind) + ")", id);
                if (assigned && assigned->endpointId == d->raw.endpointId) selected = id;
            }
            if (assigned && selected == 1)
            {
                // Assigned device isn't enumerated at all: still show it, offline.
                ids.push_back(assigned->endpointId);
                const int id = static_cast<int>(ids.size()) + 1;
                box.addItem(u8(assigned->friendlyName) + " [OFFLINE]", id);
                selected = id;
            }
            box.setSelectedId(selected, juce::dontSendNotification);
        };
        fill(r.mic, r.micIds, Flow::Capture, ca.mic);
        fill(r.headphones, r.hpIds, Flow::Render, ca.headphones);
        r.micChannel.setSelectedId(ca.micChannel + 2, juce::dontSendNotification);
        if (!r.name.hasKeyboardFocus(true)) r.name.setText(u8(ca.name), false);
    }
}

void ChannelsView::refreshStatus()
{
    const auto s = controller_.status();
    const auto m = controller_.meters();
    for (int i = 0; i < kNumChannels; ++i)
    {
        auto& r = *rows_[static_cast<size_t>(i)];
        const auto& c = s.channels[static_cast<size_t>(i)];
        auto describe = [](const EndpointView& v) {
            juce::String t = toString(v.state);
            if (v.state == EndpointState::Ok)
                t << "  " << v.deviceRate / 1000.0 << "k " << v.deviceChannels << "ch " << v.periodFrames << "smp"
                  << (v.master ? "  MASTER" : "");
            else if (v.state == EndpointState::PossibleMatch)
                t << ": " << u8(v.detail) << "?";
            else if (!v.detail.empty())
                t << ": " << u8(v.detail);
            return t;
        };
        r.micStatus.setText(describe(c.mic), juce::dontSendNotification);
        r.micStatus.setColour(juce::Label::textColourId, stateColour(c.mic.state));
        r.hpStatus.setText(describe(c.headphones), juce::dontSendNotification);
        r.hpStatus.setColour(juce::Label::textColourId, stateColour(c.headphones.state));
        r.acceptMic.setVisible(c.mic.state == EndpointState::PossibleMatch);
        r.acceptHp.setVisible(c.headphones.state == EndpointState::PossibleMatch);

        juce::String sync;
        auto syncOf = [](const EndpointView& v) {
            if (v.state != EndpointState::Ok) return juce::String("-");
            juce::String t = toString(v.bridge.status);
            if (v.bridge.status != SyncStatus::Native) t << " " << juce::String(v.bridge.ppm, 1) << " ppm";
            if (v.bridge.underruns) t << " xr" << static_cast<int>(v.bridge.underruns);
            return t;
        };
        sync << "in  " << syncOf(c.mic) << "\nout " << syncOf(c.headphones);
        r.sync.setText(sync, juce::dontSendNotification);
        const SyncStatus worst = c.mic.state == EndpointState::Ok ? c.mic.bridge.status : c.headphones.bridge.status;
        r.sync.setColour(juce::Label::textColourId, syncColour(worst));
        r.meter.setLevel(m.peak[static_cast<size_t>(i)], m.rms[static_cast<size_t>(i)]);
    }
}

void ChannelsView::timerCallback()
{
    const auto gen = controller_.registry().generation();
    const auto snapshot = toJson(controller_.assignments());
    if (gen != registryGeneration_ || snapshot != assignmentsSnapshot_)
    {
        registryGeneration_ = gen;
        assignmentsSnapshot_ = snapshot;
        rebuildDeviceLists();
    }
    refreshStatus();
}

} // namespace pf8::ui
