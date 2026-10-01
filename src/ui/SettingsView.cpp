#include "ui/SettingsView.h"

#include <cmath>

#include "core/Log.h"
#include "core/Paths.h"
#include "core/SettingsDb.h"

namespace pf8::ui {
namespace {

juce::String u8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

} // namespace

// ------------------------------------------------------------------ page framework

class SettingsPage : public juce::Component, public LayoutSelfCheck
{
public:
    explicit SettingsPage(juce::String title) : title_(std::move(title)) {}
    const juce::String& title() const noexcept { return title_; }
    virtual void refresh() {}

    int preferredHeight() const
    {
        int y = 56;
        for (const auto& r : rows_) y += r.section ? 40 : r.height + (r.help.isEmpty() ? 8 : 26);
        return y + 20;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(colours::background);
        g.setColour(colours::text);
        g.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
        g.drawText(title_, 16, 10, getWidth() - 32, 32, juce::Justification::centredLeft, true);
        for (const auto& r : rows_)
        {
            if (r.section)
            {
                g.setColour(colours::accent);
                g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
                g.drawText(r.label.toUpperCase(), r.labelArea, juce::Justification::bottomLeft, true);
                g.setColour(colours::outline);
                g.fillRect(r.labelArea.getX(), r.labelArea.getBottom() + 2, getWidth() - 2 * r.labelArea.getX(), 1);
                continue;
            }
            g.setColour(colours::text);
            g.setFont(juce::Font(juce::FontOptions(14.0f)));
            g.drawText(r.label, r.labelArea, juce::Justification::centredLeft, true);
            if (r.help.isNotEmpty())
            {
                g.setColour(colours::textDim);
                g.setFont(juce::Font(juce::FontOptions(12.0f)));
                g.drawFittedText(r.help, r.helpArea, juce::Justification::topLeft, 1, 0.9f);
            }
        }
    }

    void resized() override
    {
        const int x = 16, labelW = juce::jlimit(180, 300, getWidth() / 4);
        const int controlX = x + labelW + 12;
        const int controlW = juce::jmin(520, getWidth() - controlX - 16);
        int y = 56;
        for (auto& r : rows_)
        {
            if (r.section)
            {
                r.labelArea = {x, y + 8, getWidth() - 2 * x, 22};
                y += 40;
                continue;
            }
            r.labelArea = {x, y, labelW, r.height};
            int cx = controlX;
            for (size_t i = 0; i < r.controls.size(); ++i)
            {
                // One or two controls: the first one stretches. More (a row of knobs): equal cells.
                const int fixedTotal = static_cast<int>((r.controls.size() - 1) * 100);
                const int w = r.controls.size() > 2 ? 100 : i == 0 ? juce::jmax(80, controlW - fixedTotal) : 94;
                r.controls[i]->setBounds(cx, y, w, r.height);
                cx += w + 6;
            }
            r.helpArea = {controlX, y + r.height + 2, getWidth() - controlX - 16, 20};
            y += r.height + (r.help.isEmpty() ? 8 : 26);
        }
    }

    void collectLayoutIssues(std::vector<std::string>& issues) const override
    {
        for (const auto& r : rows_)
            for (auto* c : r.controls)
                if (auto* lc = dynamic_cast<const LayoutSelfCheck*>(c)) lc->collectLayoutIssues(issues);
    }

protected:
    void section(const juce::String& name) { rows_.push_back({name, {}, {}, true, 0, {}, {}}); }
    void row(const juce::String& label, std::vector<juce::Component*> controls, const juce::String& help = {}, int height = 28)
    {
        for (auto* c : controls) addAndMakeVisible(c);
        rows_.push_back({label, help, std::move(controls), false, height, {}, {}});
    }

    juce::ComboBox& combo(const juce::StringArray& items, int selectedIndex, std::function<void(int)> onChange)
    {
        auto* c = own(std::make_unique<juce::ComboBox>());
        c->addItemList(items, 1);
        c->setSelectedItemIndex(juce::jmax(0, selectedIndex), juce::dontSendNotification);
        c->onChange = [c, onChange] { onChange(c->getSelectedItemIndex()); };
        return *c;
    }
    ToggleLed& toggle(const juce::String& text, bool on, std::function<void(bool)> onToggle, juce::Colour colour = colours::ok)
    {
        auto* t = own(std::make_unique<ToggleLed>(text, colour));
        t->setToggleState(on, juce::dontSendNotification);
        t->onToggle = std::move(onToggle);
        return *t;
    }
    juce::TextButton& button(const juce::String& text, std::function<void()> onClick)
    {
        auto* b = own(std::make_unique<juce::TextButton>(text));
        b->onClick = std::move(onClick);
        return *b;
    }
    juce::Label& info(const juce::String& text)
    {
        auto* l = own(std::make_unique<juce::Label>());
        l->setText(text, juce::dontSendNotification);
        l->setFont(juce::FontOptions(13.0f));
        l->setColour(juce::Label::textColourId, colours::textDim);
        l->setMinimumHorizontalScale(0.7f);
        allowEllipsis(*l);
        return *l;
    }
    template <class T> T* own(std::unique_ptr<T> p)
    {
        T* raw = p.get();
        owned_.push_back(std::move(p));
        return raw;
    }

private:
    struct Row
    {
        juce::String label, help;
        std::vector<juce::Component*> controls;
        bool section = false;
        int height = 28;
        juce::Rectangle<int> labelArea, helpArea;
    };
    juce::String title_;
    std::vector<Row> rows_;
    std::vector<std::unique_ptr<juce::Component>> owned_;
};

namespace {

int indexOf(std::initializer_list<int> values, int v)
{
    int i = 0;
    for (int x : values)
    {
        if (x == v) return i;
        ++i;
    }
    return 0;
}

// ------------------------------------------------------------------ Audio & performance

class AudioPage : public SettingsPage
{
public:
    explicit AudioPage(EngineController& c) : SettingsPage("Audio & performance"), c_(c)
    {
        s_ = AppSettings::load(c.settingsDb());
        static const int rates[] = {44100, 48000, 96000};
        static const int blocks[] = {64, 128, 256, 512};
        section("Engine (applies at the next start)");
        row("Sample rate",
            {&combo({"44 100 Hz", "48 000 Hz (recommended)", "96 000 Hz"}, indexOf({44100, 48000, 96000}, s_.sampleRate),
                    [this](int i) { s_.sampleRate = rates[i]; save(); })},
            "Every device is resampled to this rate; recordings use it.");
        row("Engine block",
            {&combo({"64 samples", "128 samples (recommended)", "256 samples", "512 samples"}, indexOf({64, 128, 256, 512}, s_.blockFrames),
                    [this](int i) { s_.blockFrames = blocks[i]; save(); })},
            "Smaller = lower monitoring latency, more CPU. Raise it if the Diagnostics page shows underruns.");
        row("WASAPI mode",
            {&combo({"Shared (works with every app)", "Shared low-latency", "Exclusive (lowest latency, device not shared)"},
                    static_cast<int>(s_.mode), [this](int i) { s_.mode = static_cast<StreamMode>(i); save(); })},
            "Exclusive falls back to shared when a device refuses it.");
        row("Windows audio effects", {&toggle("BYPASS", s_.rawStreams, [this](bool on) { s_.rawStreams = on; save(); })},
            "Recommended: Windows AGC, loudness equalisation and noise suppression never touch the mics or mixes (raw streams).");
        section("Running now");
        const auto& now = c.settings();
        row("Engine", {&info(juce::String(now.sampleRate) + " Hz, " + juce::String(now.blockFrames) + " samples (" +
                             juce::String(1000.0 * now.blockFrames / now.sampleRate, 2) + " ms)")});
        row("Real-time scheduling", {&info("MMCSS \"Pro Audio\" on every audio thread; denormals flushed to zero")});
        status_ = &info("");
        row("", {status_});
    }

private:
    void save()
    {
        const bool ok = s_.save(c_.settingsDb());
        status_->setText(ok ? "Saved - restart Podcast Forge 8 to apply." : "Not saved: the settings database is unavailable.", juce::dontSendNotification);
        status_->setColour(juce::Label::textColourId, ok ? colours::warn : colours::error);
    }
    EngineController& c_;
    AppSettings s_;
    juce::Label* status_ = nullptr;
};

// ------------------------------------------------------------------ Devices

class DevicesPage : public SettingsPage
{
public:
    explicit DevicesPage(EngineController& c) : SettingsPage("Devices"), c_(c)
    {
        section("Clock");
        master_ = &combo({"Automatic"}, 0, [this](int i) {
            auto a = c_.assignments();
            a.preferredMaster = i <= 0 || static_cast<size_t>(i - 1) >= ids_.size() ? std::string() : ids_[static_cast<size_t>(i - 1)];
            c_.applyAssignments(a);
        });
        row("Master clock device", {master_},
            "Automatic picks the first assigned headphone output. Every other device is resampled to it.");
        section("Devices");
        row("Device list", {&button("RESCAN NOW", [this] { c_.rescanDevices(); })},
            "Devices are also picked up automatically when plugged in. Assign them on the DEVICE MATRIX tab.");
        row("Identity", {&info("Channels follow a device by Windows endpoint ID and USB serial, never by order.")});
    }
    void refresh() override
    {
        ids_.clear();
        master_->clear(juce::dontSendNotification);
        master_->addItem("Automatic", 1);
        const auto preferred = c_.assignments().preferredMaster;
        int sel = 1;
        for (const auto& d : c_.registry().devices())
        {
            if (d.raw.flow != Flow::Render || !d.online()) continue;
            ids_.push_back(d.raw.endpointId);
            master_->addItem(u8(d.raw.friendlyName), static_cast<int>(ids_.size()) + 1);
            if (d.raw.endpointId == preferred) sel = static_cast<int>(ids_.size()) + 1;
        }
        master_->setSelectedId(sel, juce::dontSendNotification);
    }

private:
    EngineController& c_;
    juce::ComboBox* master_ = nullptr;
    std::vector<std::string> ids_;
};

// ------------------------------------------------------------------ Routing

class RoutingPage : public SettingsPage
{
public:
    explicit RoutingPage(EngineController& c) : SettingsPage("Routing & monitoring")
    {
        auto& rp = c.engine().routing();
        section("Talkback");
        row("Talkback to the recording", {&toggle("TO PROGRAM", rp.talkbackToProgram.load(), [&rp](bool on) { rp.talkbackToProgram = on; }, colours::warn)},
            "Off (default): talkback only ever reaches the chosen headphones - never Main, Clean Feed or the files.");
        section("Monitor");
        row("Follow PFL", {&toggle("AUTO PFL", rp.monitor.autoPfl.load(), [&rp](bool on) { rp.monitor.autoPfl = on; })},
            "Pressing PFL on a channel switches the monitor to the PFL bus.");
        static const float dims[] = {-10.0f, -20.0f, -30.0f};
        const float dimDb = 20.0f * std::log10(std::max(1e-4f, rp.monitor.dimGain.get()));
        row("DIM level", {&combo({"-10 dB", "-20 dB", "-30 dB"}, dimDb > -15 ? 0 : dimDb > -25 ? 1 : 2,
                                 [&rp](int i) { rp.monitor.dimGain.set(std::pow(10.0f, dims[i] / 20.0f)); })});
        static const float ceilings[] = {-12.0f, -6.0f, -3.0f, 0.0f};
        const float mc = rp.monitor.maxCeilingDb.get();
        row("Monitor maximum", {&combo({"-12 dBFS", "-6 dBFS", "-3 dBFS (default)", "0 dBFS"}, mc <= -9 ? 0 : mc <= -4.5f ? 1 : mc <= -1.5f ? 2 : 3,
                                       [&rp](int i) { rp.monitor.maxCeilingDb.set(ceilings[i]); })},
            "A brick-wall limiter on the operator monitor: it can never play louder than this.");
        section("Headphones (all eight)");
        row("Hearing protection", {&toggle("PROTECT", rp.headphones[0].protectOn.load(), [&rp](bool on) {
                for (auto& h : rp.headphones) h.protectOn = on;
            })},
            "A limiter on every headphone feed. Leave it on: a feedback squeal or a dropped mic cannot hurt anyone.");
        static const float hpc[] = {-12.0f, -9.0f, -6.0f, -3.0f, 0.0f};
        const float hc = rp.headphones[0].protectCeilingDb.get();
        row("Headphone maximum", {&combo({"-12 dBFS", "-9 dBFS", "-6 dBFS (default)", "-3 dBFS", "0 dBFS"},
                                         hc <= -10.5f ? 0 : hc <= -7.5f ? 1 : hc <= -4.5f ? 2 : hc <= -1.5f ? 3 : 4, [&rp](int i) {
                                             for (auto& h : rp.headphones) h.protectCeilingDb.set(hpc[i]);
                                         })},
            "Per-headphone ceilings can still be set on the HEADPHONES tab.");
    }
};

// ------------------------------------------------------------------ Recording & projects

class RecordingPage : public SettingsPage
{
public:
    explicit RecordingPage(EngineController& c) : SettingsPage("Recording & projects"), c_(c)
    {
        auto& rs = c.recordingSettings();
        section("Files (saved with the project)");
        row("Format", {&combo({"WAV", "BWF (WAV + broadcast metadata)", "FLAC (lossless, smaller)"}, static_cast<int>(rs.format),
                              [this](int i) { c_.recordingSettings().format = static_cast<FileFormat>(i); })},
            "WAV switches to RF64 automatically beyond 4 GB, so long sessions never break.");
        static const BitDepth depths[] = {BitDepth::Int16, BitDepth::Int24, BitDepth::Float32};
        row("Bit depth", {&combo({"16-bit", "24-bit (recommended)", "32-bit float"}, rs.depth == BitDepth::Int16 ? 0 : rs.depth == BitDepth::Int24 ? 1 : 2,
                                 [this](int i) { c_.recordingSettings().depth = depths[i]; })});
        row("Tracks", {&toggle("MAIN MIX", rs.recordMain, [this](bool on) { c_.recordingSettings().recordMain = on; }),
                       &toggle("MUSIC", rs.recordMusic, [this](bool on) { c_.recordingSettings().recordMusic = on; })},
            "Every armed channel is always recorded as its own file (post-DSP, aligned to the Main mix).");
        section("Projects");
        folder_ = &info("");
        row("Current project folder", {folder_});
        row("New projects go to", {&info(juce::String(paths::defaultProjects().wstring().c_str()))},
            "Uninstalling never deletes projects or recordings.");
    }
    void refresh() override { folder_->setText(juce::String(c_.recordingSettings().projectDir.wstring().c_str()), juce::dontSendNotification); }

private:
    EngineController& c_;
    juce::Label* folder_ = nullptr;
};

// ------------------------------------------------------------------ DSP

class DspPage : public SettingsPage
{
public:
    explicit DspPage(EngineController& c) : SettingsPage("Master DSP")
    {
        auto& m = c.engine().masterDsp();
        section("Master limiter (Main and Clean Feed)");
        row("Limiter", {&toggle("LIMITER", m.limiterOn.load(), [&m](bool on) { m.limiterOn = on; }),
                        &toggle("TRUE PEAK", m.truePeak.load(), [&m](bool on) { m.truePeak = on; })},
            "True-peak detection (4x interpolated) keeps the output under the ceiling after conversion too.");
        static const float ceil[] = {-3.0f, -2.0f, -1.0f, -0.5f, -0.1f};
        const float cv = m.limiterCeilingDb.get();
        row("Ceiling", {&combo({"-3.0 dBFS", "-2.0 dBFS", "-1.0 dBFS (default)", "-0.5 dBFS", "-0.1 dBFS"},
                               cv <= -2.5f ? 0 : cv <= -1.5f ? 1 : cv <= -0.75f ? 2 : cv <= -0.3f ? 3 : 4, [&m](int i) { m.limiterCeilingDb.set(ceil[i]); })});
        section("Shared reverb (send per channel in its DSP editor)");
        auto knob = [this](const char* name, AtomicParam& p) {
            auto* k = own(std::make_unique<Knob>(name, 0.0, 100.0, p.get() * 100.0, [](double v) { return juce::String(v, 0) + " %"; }));
            k->setValue(p.get() * 100.0);
            k->onChange = [&p](double v) { p.set(static_cast<float>(v / 100.0)); };
            return static_cast<juce::Component*>(k);
        };
        row("Reverb", {knob("ROOM", m.reverbRoomSize), knob("DAMPING", m.reverbDamping), knob("WIDTH", m.reverbWidth), knob("RETURN", m.reverbReturn)}, {},
            90);
    }
};

// ------------------------------------------------------------------ Hotkeys

class HotkeysPage : public SettingsPage
{
public:
    explicit HotkeysPage(HotkeyAccess& access) : SettingsPage("Hotkeys"), access_(access)
    {
        local_ = HotkeyConfig::defaults();
        section("Keys (type a key such as F9, Ctrl+Shift+M, Num 1 - press Enter; empty = none)");
        static const HotkeyAction actions[] = {HotkeyAction::Record,       HotkeyAction::Stop,     HotkeyAction::Pause,          HotkeyAction::Marker,
                                               HotkeyAction::RecordToggle, HotkeyAction::Talkback, HotkeyAction::StopAllCarts,   HotkeyAction::MusicPlayPause,
                                               HotkeyAction::Cough1,       HotkeyAction::Cough2,   HotkeyAction::Cough3,         HotkeyAction::Cough4,
                                               HotkeyAction::Cough5,       HotkeyAction::Cough6,   HotkeyAction::Cough7,         HotkeyAction::Cough8};
        for (auto a : actions)
        {
            Entry e;
            e.action = a;
            e.editor = own(std::make_unique<juce::TextEditor>());
            e.editor->setFont(juce::FontOptions(14.0f));
            e.editor->setJustification(juce::Justification::centredLeft);
            e.editor->onReturnKey = [this, a] { commit(a); };
            e.editor->onFocusLost = [this, a] { commit(a); };
            e.global = &toggle("GLOBAL", false, [this, a](bool on) {
                auto c = config();
                const auto* b = c.find(a);
                c.set(a, b ? b->chord : KeyChord{}, on);
                apply(c);
            }, colours::accent);
            e.global->setTooltip("Also works while another application (OBS, a browser...) is in front.");
            row(juce::String(actionLabel(a)), {e.editor, e.global});
            entries_.push_back(e);
        }
        section("Cough buttons");
        static const char* names[] = {"Push to mute (default)", "Push to talk", "Toggle"};
        for (int ch = 0; ch < 8; ++ch)
        {
            auto& cb = combo({names[0], names[1], names[2]}, 0, [this, ch](int i) {
                auto c = config();
                c.coughModes[static_cast<size_t>(ch)] = static_cast<CoughMode>(i);
                apply(c);
            });
            modes_.push_back(&cb);
            row("Cough mode CH " + juce::String(ch + 1), {&cb});
        }
        section("Status");
        status_ = &info("");
        row("Conflicts", {status_}, "Cart hotkeys are set on each cart (SOUNDBOARD, right-click a pad).", 40);
        row("", {&button("RESET TO DEFAULTS", [this] { apply(HotkeyConfig::defaults()); })});
    }

    void refresh() override
    {
        const auto c = config();
        for (auto& e : entries_)
        {
            const auto* b = c.find(e.action);
            e.editor->setText(b ? juce::String(formatChord(b->chord)) : juce::String(), false);
            e.editor->setColour(juce::TextEditor::textColourId, colours::text);
            e.global->setToggleState(b && b->global, juce::dontSendNotification);
        }
        for (size_t ch = 0; ch < modes_.size(); ++ch) modes_[ch]->setSelectedItemIndex(static_cast<int>(c.coughModes[ch]), juce::dontSendNotification);
        juce::StringArray lines;
        for (const auto& [a, b] : c.conflicts()) lines.add(juce::String(actionLabel(a)) + " and " + juce::String(actionLabel(b)) + " share a key");
        if (access_.errors)
            for (const auto& e : access_.errors()) lines.add(u8(e));
        status_->setText(lines.isEmpty() ? juce::String("No conflicts.") : lines.joinIntoString("; "), juce::dontSendNotification);
        status_->setColour(juce::Label::textColourId, lines.isEmpty() ? colours::ok : colours::warn);
    }

private:
    struct Entry
    {
        HotkeyAction action{};
        juce::TextEditor* editor = nullptr;
        ToggleLed* global = nullptr;
    };
    HotkeyConfig config() const { return access_.get ? access_.get() : local_; }
    void apply(const HotkeyConfig& c)
    {
        if (access_.set) access_.set(c);
        else local_ = c;
        refresh();
    }
    void commit(HotkeyAction a)
    {
        for (auto& e : entries_)
            if (e.action == a)
            {
                const auto text = e.editor->getText().trim().toStdString();
                auto c = config();
                const auto* b = c.find(a);
                const bool global = b && b->global;
                if (text.empty())
                {
                    c.set(a, {}, false);
                    apply(c);
                    return;
                }
                if (auto chord = parseChord(text))
                {
                    if (b && b->chord == *chord) return;
                    c.set(a, *chord, global);
                    apply(c);
                }
                else
                    e.editor->setColour(juce::TextEditor::textColourId, colours::error), e.editor->repaint();
            }
    }

    HotkeyAccess& access_;
    HotkeyConfig local_;
    std::vector<Entry> entries_;
    std::vector<juce::ComboBox*> modes_;
    juce::Label* status_ = nullptr;
};

// ------------------------------------------------------------------ Appearance

class AppearancePage : public SettingsPage
{
public:
    explicit AppearancePage(EngineController& c) : SettingsPage("Appearance"), c_(c)
    {
        static const int scales[] = {100, 125, 150, 175, 200};
        const auto s = AppSettings::load(c.settingsDb());
        section("Display");
        row("Interface size", {&combo({"100 %", "125 %", "150 %", "175 %", "200 %"}, indexOf({100, 125, 150, 175, 200}, static_cast<int>(s.uiScale * 100 + 0.5)),
                                      [this](int i) {
                                          auto st = AppSettings::load(c_.settingsDb());
                                          st.uiScale = scales[i] / 100.0;
                                          st.save(c_.settingsDb());
                                          juce::Desktop::getInstance().setGlobalScaleFactor(static_cast<float>(st.uiScale));
                                      })},
            "On top of the Windows display scaling. Applies immediately.");
        row("Theme", {&info("Dark studio theme (high contrast, colour-coded functions: mute red, solo yellow, PFL teal)")});
    }

private:
    EngineController& c_;
};

// ------------------------------------------------------------------ Diagnostics

class DiagnosticsPage : public SettingsPage
{
public:
    explicit DiagnosticsPage(EngineController& c) : SettingsPage("Logging & privacy"), c_(c)
    {
        section("Logging");
        row("Detailed logging", {&toggle("DEBUG LOG", log::minLevel() == log::Level::Debug, [this](bool on) {
                log::setMinLevel(on ? log::Level::Debug : log::Level::Info);
                auto st = AppSettings::load(c_.settingsDb());
                st.debugLog = on;
                st.save(c_.settingsDb());
            })},
            "For troubleshooting. Logs never contain audio or keystrokes; 10 files x 10 MB, oldest removed first.");
        row("Log folder", {&info(juce::String(paths::logs().wstring().c_str()))});
        row("Reports folder", {&info(juce::String(paths::diagnostics().wstring().c_str()))});
        section("Privacy");
        row("Network", {&info("No telemetry, no network access, no accounts. Audio only leaves through the outputs you assign.")});
    }

private:
    EngineController& c_;
};

} // namespace

// ------------------------------------------------------------------ SettingsView

SettingsView::SettingsView(EngineController& controller) : controller_(controller)
{
    pages_.push_back(std::make_unique<AudioPage>(controller));
    pages_.push_back(std::make_unique<DevicesPage>(controller));
    pages_.push_back(std::make_unique<RoutingPage>(controller));
    pages_.push_back(std::make_unique<RecordingPage>(controller));
    pages_.push_back(std::make_unique<DspPage>(controller));
    hotkeyAccess_ = std::make_unique<HotkeyAccess>();
    pages_.push_back(std::make_unique<HotkeysPage>(*hotkeyAccess_));
    pages_.push_back(std::make_unique<AppearancePage>(controller));
    pages_.push_back(std::make_unique<DiagnosticsPage>(controller));
    list_.setRowHeight(34);
    list_.setColour(juce::ListBox::backgroundColourId, colours::panel);
    addAndMakeVisible(list_);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    list_.updateContent();
    list_.selectRow(0);
    showPage(0);
}

SettingsView::~SettingsView() { viewport_.setViewedComponent(nullptr, false); }

void SettingsView::setHotkeyAccess(HotkeyAccess access)
{
    *hotkeyAccess_ = std::move(access);
    for (auto& p : pages_) p->refresh();
}

int SettingsView::getNumRows() { return static_cast<int>(pages_.size()); }

void SettingsView::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= static_cast<int>(pages_.size())) return;
    g.fillAll(selected ? colours::panelRaised : colours::panel);
    if (selected)
    {
        g.setColour(colours::accent);
        g.fillRect(0, 0, 3, h);
    }
    g.setColour(selected ? colours::text : colours::textDim);
    g.setFont(juce::Font(juce::FontOptions(14.0f, selected ? juce::Font::bold : juce::Font::plain)));
    g.drawText(pages_[static_cast<size_t>(row)]->title(), 14, 0, w - 20, h, juce::Justification::centredLeft, true);
}

void SettingsView::selectedRowsChanged(int row)
{
    if (row >= 0) showPage(row);
}

void SettingsView::showPage(int index)
{
    if (index < 0 || index >= static_cast<int>(pages_.size())) return;
    current_ = index;
    auto* page = pages_[static_cast<size_t>(index)].get();
    page->refresh();
    viewport_.setViewedComponent(page, false);
    if (list_.getSelectedRow() != index) list_.selectRow(index, true, true);
    resized();
}

void SettingsView::resized()
{
    auto b = getLocalBounds();
    list_.setBounds(b.removeFromLeft(220).reduced(8));
    viewport_.setBounds(b);
    if (auto* page = pages_[static_cast<size_t>(current_)].get())
        page->setSize(viewport_.getMaximumVisibleWidth(), juce::jmax(viewport_.getHeight(), page->preferredHeight()));
}

void SettingsView::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void SettingsView::collectLayoutIssues(std::vector<std::string>& issues) const
{
    pages_[static_cast<size_t>(current_)]->collectLayoutIssues(issues);
}

} // namespace pf8::ui
