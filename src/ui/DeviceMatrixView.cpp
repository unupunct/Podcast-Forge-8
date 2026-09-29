#include "ui/DeviceMatrixView.h"

#include <set>

#include "devices/AutoAssign.h"
#include "ui/LookAndFeel.h"

namespace pf8::ui {
namespace {

juce::String u8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

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

juce::String syncText(const EndpointView& v)
{
    if (v.state != EndpointState::Ok) return "-";
    juce::String t = toString(v.bridge.status);
    if (v.bridge.status != SyncStatus::Native) t << " " << juce::String(v.bridge.ppm, 1) << " ppm";
    if (v.bridge.underruns) t << "  xr " << static_cast<int>(v.bridge.underruns);
    return t;
}

} // namespace

// ---------------------------------------------------------------------------------------------
DeviceCell::DeviceCell(DeviceMatrixView& owner, Kind kind, int index) : owner_(owner), kind_(kind), index_(index)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void DeviceCell::setContent(const juce::String& name, EndpointState state, const juce::String& detail, const std::string& id)
{
    if (name == name_ && state == state_ && detail == detail_ && id == endpointId_) return;
    name_ = name;
    state_ = state;
    detail_ = detail;
    endpointId_ = id;
    setTooltip(detail_.isEmpty() ? name_ : name_ + "\n" + detail_);
    repaint();
}

void DeviceCell::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(1.5f);
    g.setColour(hover_ ? colours::accent.withAlpha(0.25f) : colours::panelRaised);
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(hover_ ? colours::accent : stateColour(state_).withAlpha(state_ == EndpointState::None ? 0.3f : 0.8f));
    g.drawRoundedRectangle(b, 4.0f, hover_ ? 2.0f : 1.0f);
    auto t = b.reduced(8.0f, 2.0f);
    const float fs = juce::jlimit(11.0f, 15.0f, b.getHeight() * 0.36f);
    g.setFont(juce::Font(juce::FontOptions(fs)));
    g.setColour(state_ == EndpointState::None ? colours::textDim : colours::text);
    const bool twoLines = state_ != EndpointState::None && state_ != EndpointState::Ok && b.getHeight() > 30;
    g.drawText(name_.isEmpty() ? juce::String("drop a device here") : name_, t.removeFromTop(twoLines ? t.getHeight() * 0.55f : t.getHeight()),
               juce::Justification::centredLeft, true);
    if (twoLines)
    {
        g.setFont(juce::Font(juce::FontOptions(fs * 0.85f, juce::Font::bold)));
        g.setColour(stateColour(state_));
        g.drawText(juce::String(toString(state_)) + (detail_.isNotEmpty() ? ": " + detail_ : juce::String()), t,
                   juce::Justification::centredLeft, true);
    }
}

void DeviceCell::mouseDown(const juce::MouseEvent&) { dragStarted_ = false; }

void DeviceCell::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStarted_ || endpointId_.empty() || e.getDistanceFromDragStart() < 6) return;
    dragStarted_ = true;
    owner_.startDragging(owner_.describeCell(*this), this);
}

void DeviceCell::mouseUp(const juce::MouseEvent& e)
{
    if (!dragStarted_ && e.mouseWasClicked()) owner_.showDeviceMenu(*this);
}

bool DeviceCell::isInterestedInDragSource(const SourceDetails& d)
{
    const auto flow = static_cast<int>(d.description["flow"]);
    return flow == static_cast<int>(this->flow()) && d.sourceComponent != this;
}

void DeviceCell::itemDropped(const SourceDetails& d)
{
    hover_ = false;
    repaint();
    owner_.dropOnto(*this, d.description);
}

// ---------------------------------------------------------------------------------------------
void DevicePool::setDevices(std::vector<DeviceInfo> devices)
{
    devices_ = std::move(devices);
    rowToDevice_.clear();
    for (Flow f : {Flow::Capture, Flow::Render})
    {
        rowToDevice_.push_back(-1 - static_cast<int>(f)); // header
        for (size_t i = 0; i < devices_.size(); ++i)
            if (devices_[i].raw.flow == f) rowToDevice_.push_back(static_cast<int>(i));
    }
    repaint();
}

int DevicePool::rowAt(int y) const
{
    const int r = y / rowHeight();
    return r >= 0 && r < static_cast<int>(rowToDevice_.size()) ? r : -1;
}

void DevicePool::paint(juce::Graphics& g)
{
    g.fillAll(colours::panel);
    const int h = rowHeight();
    for (size_t r = 0; r < rowToDevice_.size(); ++r)
    {
        auto row = juce::Rectangle<int>(0, static_cast<int>(r) * h, getWidth(), h);
        const int di = rowToDevice_[r];
        if (di < 0)
        {
            g.setColour(colours::textDim);
            g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
            g.drawText(di == -1 ? "INPUTS" : "OUTPUTS", row.reduced(8, 0), juce::Justification::bottomLeft, false);
            continue;
        }
        const auto& d = devices_[static_cast<size_t>(di)];
        g.setColour(colours::panelRaised);
        g.fillRoundedRectangle(row.reduced(4, 2).toFloat(), 3.0f);
        g.setColour(colours::text);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        auto t = row.reduced(12, 0);
        g.drawText(u8(d.raw.friendlyName), t.removeFromLeft(t.getWidth() * 2 / 3), juce::Justification::centredLeft, true);
        g.setColour(colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(toString(d.kind), t, juce::Justification::centredRight, true);
    }
    if (devices_.empty())
    {
        g.setColour(colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText("Every online device is assigned.", getLocalBounds().reduced(10), juce::Justification::centredTop, true);
    }
}

void DevicePool::mouseDrag(const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 6) { dragging_ = false; return; }
    if (dragging_) return;
    const int r = rowAt(e.getMouseDownY());
    if (r < 0 || rowToDevice_[static_cast<size_t>(r)] < 0) return;
    dragging_ = true;
    owner_.startDragging(DeviceMatrixView::describeDevice(devices_[static_cast<size_t>(rowToDevice_[static_cast<size_t>(r)])]), this);
}

// ---------------------------------------------------------------------------------------------
DeviceMatrixView::DeviceMatrixView(EngineController& controller) : controller_(controller)
{
    const char* heads[] = {"CH", "NAME", "MICROPHONE", "INPUT", "HEADPHONES"};
    for (int i = 0; i < 5; ++i)
    {
        headers_[i].setText(heads[i], juce::dontSendNotification);
        headers_[i].setColour(juce::Label::textColourId, colours::textDim);
        headers_[i].setFont(juce::FontOptions(12.0f, juce::Font::bold));
        addAndMakeVisible(headers_[i]);
    }
    for (int i = 0; i < kNumChannels; ++i)
    {
        auto& r = rows_[static_cast<size_t>(i)];
        r.number.setText(juce::String(i + 1), juce::dontSendNotification);
        r.number.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        r.number.setJustificationType(juce::Justification::centred);
        r.name.setEditable(false, true);
        r.name.setColour(juce::Label::backgroundColourId, colours::panelRaised);
        r.name.onTextChange = [this, i] { controller_.setChannelName(i, rows_[static_cast<size_t>(i)].name.getText().toStdString()); };
        r.mic = std::make_unique<DeviceCell>(*this, DeviceCell::Kind::Mic, i);
        r.hp = std::make_unique<DeviceCell>(*this, DeviceCell::Kind::Headphones, i);
        r.input.addItem("Mix", 1);
        for (int c = 0; c < 8; ++c) r.input.addItem("In " + juce::String(c + 1), c + 2);
        r.input.onChange = [this, i] {
            auto& row = rows_[static_cast<size_t>(i)];
            if (!row.mic->endpointId().empty())
                controller_.assignMic(i, row.mic->endpointId(), row.input.getSelectedId() - 2);
        };
        r.input.setTooltip("Which device input feeds this channel (Mix = average of all device channels)");
        r.status.setFont(juce::FontOptions(12.0f));
        r.status.setMinimumHorizontalScale(0.8f);
        for (juce::Component* c : std::initializer_list<juce::Component*>{&r.number, &r.name, r.mic.get(), r.hp.get(), &r.input, &r.status, &r.meter})
            addAndMakeVisible(c);
    }
    for (int i = 0; i < kOutputRoles; ++i)
    {
        auto& o = outRows_[static_cast<size_t>(i)];
        o.title.setText(toString(static_cast<OutputRole>(i)), juce::dontSendNotification);
        o.title.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        o.title.setColour(juce::Label::textColourId, colours::textDim);
        o.cell = std::make_unique<DeviceCell>(*this, DeviceCell::Kind::Output, i);
        o.status.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(o.title);
        addAndMakeVisible(*o.cell);
        addAndMakeVisible(o.status);
    }
    poolTitle_.setText("UNASSIGNED DEVICES  (drag onto a cell)", juce::dontSendNotification);
    poolTitle_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    poolTitle_.setColour(juce::Label::textColourId, colours::textDim);
    hint_.setText("Channels are bound to devices by Windows endpoint ID and USB serial, never by order. "
                  "A device that disappears keeps its channel and shows OFFLINE.",
                  juce::dontSendNotification);
    hint_.setFont(juce::FontOptions(12.0f));
    hint_.setColour(juce::Label::textColourId, colours::textDim);
    hint_.setMinimumHorizontalScale(0.75f);
    autoAssign_.setTooltip("Fill EMPTY cells with unassigned USB devices. Shows every change and asks first.");
    autoAssign_.onClick = [this] { runAutoAssign(); };
    rescan_.onClick = [this] { controller_.rescanDevices(); };
    for (juce::Component* c : std::initializer_list<juce::Component*>{&poolTitle_, &pool_, &autoAssign_, &rescan_, &hint_}) addAndMakeVisible(c);
    refresh();
    startTimerHz(10);
}

DeviceMatrixView::~DeviceMatrixView() { stopTimer(); }

juce::var DeviceMatrixView::describeDevice(const DeviceInfo& d)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("endpointId", u8(d.raw.endpointId));
    o->setProperty("flow", static_cast<int>(d.raw.flow));
    o->setProperty("fromKind", -1);
    o->setProperty("fromIndex", -1);
    return juce::var(o);
}

juce::var DeviceMatrixView::describeCell(const DeviceCell& cell) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("endpointId", u8(cell.endpointId()));
    o->setProperty("flow", static_cast<int>(cell.flow()));
    o->setProperty("fromKind", static_cast<int>(cell.kind()));
    o->setProperty("fromIndex", cell.index());
    return juce::var(o);
}

void DeviceMatrixView::assign(DeviceCell::Kind kind, int index, std::optional<std::string> id)
{
    const auto a = controller_.assignments();
    switch (kind)
    {
        case DeviceCell::Kind::Mic: controller_.assignMic(index, id, a.ch[static_cast<size_t>(index)].micChannel); break;
        case DeviceCell::Kind::Headphones: controller_.assignHeadphones(index, id, a.ch[static_cast<size_t>(index)].hpPair); break;
        case DeviceCell::Kind::Output: controller_.assignOutput(static_cast<OutputRole>(index), id); break;
    }
}

void DeviceMatrixView::dropOnto(DeviceCell& target, const juce::var& d)
{
    const std::string id = d["endpointId"].toString().toStdString();
    const int fromKind = static_cast<int>(d["fromKind"]);
    const int fromIndex = static_cast<int>(d["fromIndex"]);
    const std::string previous = target.endpointId();
    assign(target.kind(), target.index(), id);
    // Dragged from another cell: that cell takes the target's previous device (swap), or empties.
    if (fromKind >= 0)
        assign(static_cast<DeviceCell::Kind>(fromKind), fromIndex, previous.empty() ? std::nullopt : std::optional<std::string>(previous));
}

void DeviceMatrixView::showDeviceMenu(DeviceCell& cell)
{
    juce::PopupMenu m;
    std::vector<std::string> ids;
    m.addItem(1, "-- none --", true, cell.endpointId().empty());
    for (const auto& d : controller_.registry().devices())
    {
        if (d.raw.flow != cell.flow() || !d.online()) continue;
        ids.push_back(d.raw.endpointId);
        m.addItem(static_cast<int>(ids.size()) + 1, u8(d.raw.friendlyName) + "   (" + toString(d.kind) + ")", true,
                  d.raw.endpointId == cell.endpointId());
    }
    juce::Component::SafePointer<DeviceCell> safe(&cell);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&cell), [this, safe, ids](int r) {
        if (!safe || r == 0) return;
        if (r == 1) assign(safe->kind(), safe->index(), std::nullopt);
        else if (static_cast<size_t>(r - 2) < ids.size()) assign(safe->kind(), safe->index(), ids[static_cast<size_t>(r - 2)]);
    });
}

void DeviceMatrixView::runAutoAssign()
{
    const auto current = controller_.assignments();
    const auto devices = controller_.registry().devices();
    const auto proposal = proposeAutoAssign(current, devices);
    if (proposal.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Auto Assign",
                                               "Nothing to assign: there is no unassigned USB device for the empty cells.\n"
                                               "Existing assignments are never changed by Auto Assign.");
        return;
    }
    juce::String msg = "Auto Assign will fill these EMPTY cells. No existing assignment changes.\n\n";
    for (const auto& c : proposal.changes)
        msg << "Channel " << (c.channel + 1) << (c.mic ? "  microphone  <-  " : "  headphones  <-  ") << u8(c.deviceName) << "\n";
    auto* w = new juce::AlertWindow("Auto Assign - confirm", msg, juce::MessageBoxIconType::QuestionIcon, this);
    // Return and Escape both cancel: applying needs a deliberate click.
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey), juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Apply", 1);
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, proposal](int result) {
                           if (result != 1) return;
                           const auto now = controller_.assignments();
                           controller_.applyAssignments(applyProposal(now, proposal, controller_.registry().devices()));
                       }),
                       true);
}

void DeviceMatrixView::refresh()
{
    const auto s = controller_.status();
    const auto a = controller_.assignments();
    const auto m = controller_.meters();
    for (int i = 0; i < kNumChannels; ++i)
    {
        auto& r = rows_[static_cast<size_t>(i)];
        const auto& c = s.channels[static_cast<size_t>(i)];
        const auto& ca = a.ch[static_cast<size_t>(i)];
        if (!r.name.isBeingEdited()) r.name.setText(u8(ca.name), juce::dontSendNotification);
        auto nameOf = [](const std::optional<DeviceIdentity>& id) { return id ? u8(id->friendlyName) : juce::String(); };
        r.mic->setContent(nameOf(ca.mic), c.mic.state, c.mic.state == EndpointState::PossibleMatch ? u8(c.mic.detail) + "? (click to choose)" : u8(c.mic.detail),
                          ca.mic ? ca.mic->endpointId : std::string());
        r.hp->setContent(nameOf(ca.headphones), c.headphones.state, u8(c.headphones.detail),
                         ca.headphones ? ca.headphones->endpointId : std::string());
        r.input.setSelectedId(ca.micChannel + 2, juce::dontSendNotification);
        r.input.setEnabled(ca.mic.has_value());
        r.status.setText("in: " + syncText(c.mic) + "\nout: " + syncText(c.headphones), juce::dontSendNotification);
        r.meter.setLevels(&m.peak[static_cast<size_t>(i)], &m.rms[static_cast<size_t>(i)]);
    }
    for (int i = 0; i < kOutputRoles; ++i)
    {
        auto& o = outRows_[static_cast<size_t>(i)];
        const auto& oa = a.outputs[static_cast<size_t>(i)];
        const auto& v = s.outputs[static_cast<size_t>(i)];
        o.cell->setContent(oa.device ? u8(oa.device->friendlyName) : juce::String(), v.state, u8(v.detail),
                           oa.device ? oa.device->endpointId : std::string());
        o.status.setText(syncText(v), juce::dontSendNotification);
    }
    // Pool: online devices not used anywhere.
    const auto gen = controller_.registry().generation();
    const auto snap = toJson(a);
    if (gen != registryGeneration_ || snap != snapshot_)
    {
        registryGeneration_ = gen;
        snapshot_ = snap;
        std::set<std::string> used;
        for (const auto& c : a.ch)
        {
            if (c.mic) used.insert(c.mic->endpointId);
            if (c.headphones) used.insert(c.headphones->endpointId);
        }
        for (const auto& o : a.outputs)
            if (o.device) used.insert(o.device->endpointId);
        std::vector<DeviceInfo> free;
        for (const auto& d : controller_.registry().devices())
            if (d.online() && !used.count(d.raw.endpointId)) free.push_back(d);
        std::stable_sort(free.begin(), free.end(), [](const DeviceInfo& x, const DeviceInfo& y) {
            if (x.usb.has_value() != y.usb.has_value()) return x.usb.has_value();
            return x.raw.friendlyName < y.raw.friendlyName;
        });
        pool_.setDevices(std::move(free));
    }
}

void DeviceMatrixView::timerCallback() { refresh(); }

void DeviceMatrixView::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void DeviceMatrixView::resized()
{
    auto b = getLocalBounds().reduced(12);
    auto bottom = b.removeFromBottom(30);
    rescan_.setBounds(bottom.removeFromRight(110));
    bottom.removeFromRight(8);
    autoAssign_.setBounds(bottom.removeFromRight(150));
    hint_.setBounds(bottom);
    b.removeFromBottom(8);

    auto poolArea = b.removeFromRight(juce::jlimit(240, 420, b.getWidth() / 4));
    poolTitle_.setBounds(poolArea.removeFromTop(24));
    pool_.setBounds(poolArea);
    b.removeFromRight(12);

    const float W = static_cast<float>(b.getWidth());
    const float cols[] = {0.05f, 0.14f, 0.30f, 0.08f, 0.30f, 0.13f};
    auto head = b.removeFromTop(22);
    {
        int x = head.getX();
        for (int c = 0; c < 5; ++c)
        {
            const int w = static_cast<int>(W * cols[c]);
            headers_[c].setBounds(x, head.getY(), w, head.getHeight());
            x += w;
        }
    }
    const int outBlock = kOutputRoles * 34 + 12;
    auto outArea = b.removeFromBottom(outBlock);
    const int rowH = juce::jmax(36, b.getHeight() / kNumChannels);
    for (int i = 0; i < kNumChannels; ++i)
    {
        auto& r = rows_[static_cast<size_t>(i)];
        auto row = b.removeFromTop(rowH).reduced(0, 3);
        int x = row.getX();
        auto cell = [&](int c) {
            const int w = static_cast<int>(W * cols[c]);
            juce::Rectangle<int> rc(x, row.getY(), w - 6, row.getHeight());
            x += w;
            return rc;
        };
        r.number.setBounds(cell(0));
        r.name.setBounds(cell(1));
        r.mic->setBounds(cell(2));
        r.input.setBounds(cell(3).withSizeKeepingCentre(static_cast<int>(W * cols[3]) - 6, juce::jmin(28, row.getHeight())));
        r.hp->setBounds(cell(4));
        auto last = cell(5);
        r.meter.setBounds(last.removeFromRight(10));
        r.status.setBounds(last);
    }
    outArea.removeFromTop(12);
    for (int i = 0; i < kOutputRoles; ++i)
    {
        auto& o = outRows_[static_cast<size_t>(i)];
        auto row = outArea.removeFromTop(34).reduced(0, 3);
        const int left = static_cast<int>(W * (cols[0] + cols[1]));
        o.title.setBounds(row.removeFromLeft(left - 6));
        row.removeFromLeft(6);
        o.cell->setBounds(row.removeFromLeft(static_cast<int>(W * (cols[2] + cols[3] + cols[4])) - 6));
        o.status.setBounds(row.withTrimmedLeft(6));
    }
}

void DeviceMatrixView::collectLayoutIssues(std::vector<std::string>& issues) const
{
    if (rows_[0].mic->getHeight() < 28) issues.push_back("Device Matrix rows below 28 px");
    if (pool_.getWidth() < 200) issues.push_back("Device pool narrower than 200 px");
}

} // namespace pf8::ui
