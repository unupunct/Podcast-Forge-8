#include "ui/MusicView.h"

#include <cmath>

#include "media/AudioFileLoader.h"

namespace pf8::ui {
namespace {
juce::String mmss(double s)
{
    const int t = static_cast<int>(s);
    return juce::String(t / 60) + ":" + juce::String(t % 60).paddedLeft('0', 2);
}
juce::String dbs(double v) { return formatDb(v, 0) + " dB"; }
} // namespace

MusicView::MusicView(EngineController& controller)
    : controller_(controller),
      threshold_("THRESH", -60, -10, -35, dbs),
      depth_("DEPTH", -30, 0, -15, dbs),
      release_("RELEASE", 200, 5000, 1500, [](double v) { return juce::String(v / 1000.0, 1) + " s"; })
{
    auto& mp = controller_.engine().music();
    auto& dp = controller_.engine().ducker();
    list_.setRowHeight(24);
    list_.setColour(juce::ListBox::backgroundColourId, colours::panel);
    add_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Add music", juce::File(), supportedAudioWildcard());
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::canSelectMultipleItems,
                              [this](const juce::FileChooser& fc) {
                                  for (const auto& f : fc.getResults())
                                      controller_.engine().music().add(std::filesystem::path(f.getFullPathName().toWideCharPointer()));
                              });
    };
    remove_.onClick = [this, &mp] {
        if (const int r = list_.getSelectedRow(); r >= 0) mp.remove(r);
    };
    play_.onClick = [this, &mp] {
        const auto st = mp.status();
        if (st.playing) mp.pause();
        else if (st.paused && st.index >= 0) mp.resume();
        else mp.play(std::max(0, list_.getSelectedRow()));
    };
    next_.onClick = [&mp] { mp.next(); };
    fade_.onClick = [&mp] { mp.fadeOut(2000.0f); };
    stop_.onClick = [&mp] { mp.stop(); };
    auto_.setToggleState(mp.autoAdvance().load(), juce::dontSendNotification);
    auto_.onToggle = [&mp](bool on) { mp.autoAdvance() = on; };
    record_.setToggleState(controller_.recordingSettings().recordMusic, juce::dontSendNotification);
    record_.onToggle = [this](bool on) { controller_.recordingSettings().recordMusic = on; };
    record_.setTooltip("Also record the music as its own track (Audio/Music.wav)");
    duck_.setToggleState(dp.on.load(), juce::dontSendNotification);
    duck_.onToggle = [&dp](bool on) { dp.on = on; };
    duck_.setTooltip("Music goes down automatically while people talk, and returns gently after.");
    volume_.setRange(-60.0, 6.0, 0.1);
    volume_.setSkewFactorFromMidPoint(-12.0);
    volume_.setValue(20.0 * std::log10(std::max(1e-3f, mp.volume().get())), juce::dontSendNotification);
    volume_.onValueChange = [this, &mp] {
        const double v = volume_.getValue();
        mp.volume().set(v <= -59.5 ? 0.0f : static_cast<float>(std::pow(10.0, v / 20.0)));
        volLabel_.setText("VOL " + formatDb(v, 1) + " dB", juce::dontSendNotification);
    };
    volLabel_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    volLabel_.setColour(juce::Label::textColourId, colours::textDim);
    volume_.onValueChange();
    threshold_.setValue(dp.thresholdDb.get());
    depth_.setValue(dp.depthDb.get());
    release_.setValue(dp.releaseMs.get());
    threshold_.onChange = [&dp](double v) { dp.thresholdDb.set(static_cast<float>(v)); };
    depth_.onChange = [&dp](double v) { dp.depthDb.set(static_cast<float>(v)); };
    release_.onChange = [&dp](double v) { dp.releaseMs.set(static_cast<float>(v)); };
    position_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold));
    allowEllipsis(position_);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&list_, &add_, &remove_, &play_, &next_, &fade_, &stop_, &auto_, &record_,
                                                                      &duck_, &volume_, &position_, &volLabel_, &threshold_, &depth_, &release_})
        addAndMakeVisible(c);
    startTimerHz(10);
}

MusicView::~MusicView() { stopTimer(); }

int MusicView::getNumRows() { return static_cast<int>(rows_.size()); }

void MusicView::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    const auto& t = rows_[static_cast<size_t>(row)];
    const bool current = controller_.engine().music().status().index == row;
    g.fillAll(selected ? colours::panelRaised : colours::panel);
    g.setColour(current ? colours::ok : colours::text);
    g.setFont(juce::Font(juce::FontOptions(14.0f, current ? juce::Font::bold : juce::Font::plain)));
    g.drawText((current ? juce::String::fromUTF8("\xe2\x96\xb6  ") : juce::String()) + juce::String::fromUTF8(t.title.c_str()), 8, 0, w - 80, h,
               juce::Justification::centredLeft, true);
    g.setColour(t.error.empty() ? colours::textDim : colours::error);
    g.drawText(t.error.empty() ? (t.seconds > 0 ? mmss(t.seconds) : juce::String()) : juce::String::fromUTF8(t.error.c_str()), w - 76, 0, 70, h,
               juce::Justification::centredRight, true);
}

void MusicView::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) { controller_.engine().music().play(row); }

void MusicView::timerCallback()
{
    auto& mp = controller_.engine().music();
    auto now = mp.playlist();
    if (now.size() != rows_.size() ||
        !std::equal(now.begin(), now.end(), rows_.begin(), [](const auto& a, const auto& b) { return a.path == b.path && a.seconds == b.seconds && a.error == b.error; }))
    {
        rows_ = std::move(now);
        list_.updateContent();
    }
    list_.repaint();
    const auto st = mp.status();
    play_.setButtonText(st.playing ? "PAUSE" : "PLAY");
    position_.setText(st.index >= 0 ? mmss(st.position) + " / " + mmss(st.duration) : juce::String("--:-- / --:--"), juce::dontSendNotification);
    duckDb_ = controller_.meters().musicDuckDb;
    repaint(duckMeter_.expanded(2));
}

void MusicView::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    if (duckMeter_.isEmpty()) return;
    auto r = duckMeter_.toFloat();
    g.setColour(colours::panelRaised);
    g.fillRoundedRectangle(r, 2.0f);
    const float frac = juce::jlimit(0.0f, 1.0f, -duckDb_ / 30.0f);
    g.setColour(colours::warn);
    g.fillRoundedRectangle(r.withWidth(r.getWidth() * frac), 2.0f);
    g.setColour(colours::text);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText("DUCK " + juce::String(duckDb_, 1) + " dB", duckMeter_.reduced(4, 0), juce::Justification::centredLeft, false);
}

void MusicView::paintOverChildren(juce::Graphics& g)
{
    if (!rows_.empty()) return;
    g.setColour(colours::textDim);
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText("Playlist is empty - ADD... music files (WAV, FLAC, MP3)", list_.getBounds(), juce::Justification::centred, true);
}

void MusicView::resized()
{
    auto b = getLocalBounds().reduced(8);
    auto right = b.removeFromRight(juce::jlimit(320, 520, b.getWidth() * 38 / 100));
    b.removeFromRight(10);
    auto top = b.removeFromTop(28);
    for (auto* btn : {&add_, &remove_})
    {
        btn->setBounds(top.removeFromLeft(86));
        top.removeFromLeft(6);
    }
    position_.setBounds(top);
    b.removeFromTop(6);
    list_.setBounds(b);

    auto t1 = right.removeFromTop(28);
    const int bw = t1.getWidth() / 4;
    for (auto* btn : {&play_, &next_, &fade_, &stop_}) btn->setBounds(t1.removeFromLeft(bw).reduced(2, 0));
    right.removeFromTop(6);
    auto t2 = right.removeFromTop(24);
    volLabel_.setBounds(t2.removeFromLeft(110));
    volume_.setBounds(t2);
    right.removeFromTop(6);
    auto t3 = right.removeFromTop(24);
    auto_.setBounds(t3.removeFromLeft(t3.getWidth() / 3).reduced(2, 0));
    record_.setBounds(t3.removeFromLeft(t3.getWidth() / 2).reduced(2, 0));
    duck_.setBounds(t3.reduced(2, 0));
    right.removeFromTop(6);
    duckMeter_ = right.removeFromTop(18);
    right.removeFromTop(4);
    const int kw = right.getWidth() / 3;
    threshold_.setBounds(right.removeFromLeft(kw));
    depth_.setBounds(right.removeFromLeft(kw));
    release_.setBounds(right);
}

void MusicView::collectLayoutIssues(std::vector<std::string>& issues) const
{
    threshold_.collectLayoutIssues(issues);
    depth_.collectLayoutIssues(issues);
    release_.collectLayoutIssues(issues);
}

} // namespace pf8::ui
