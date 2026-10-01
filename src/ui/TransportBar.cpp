#include "ui/TransportBar.h"

#include "record/Markers.h"

namespace pf8::ui {
namespace {

juce::String duration(double seconds)
{
    if (seconds > 99 * 3600) return "> 99 h";
    const int s = static_cast<int>(seconds);
    return s >= 3600 ? juce::String(s / 3600) + " h " + juce::String((s / 60) % 60).paddedLeft('0', 2) + " min"
                     : juce::String(s / 60) + " min";
}

} // namespace

TransportBar::TransportBar(EngineController& controller) : controller_(controller)
{
    rec_.setColour(juce::TextButton::buttonColourId, colours::record.darker(0.6f));
    rec_.setColour(juce::TextButton::buttonOnColourId, colours::record);
    rec_.setTooltip("Start recording (F9)");
    pause_.setTooltip("Pause / resume (F11) - the same files continue");
    stop_.setTooltip("Stop and finalise every file (F10)");
    marker_.setTooltip("Add a marker at the current position (F12)");
    elsewhere_.setTooltip("The disk refuses writes: keep the audio recorded so far and continue on another drive");
    rec_.onClick = [this] { record(); };
    pause_.onClick = [this] { pause(); };
    stop_.onClick = [this] { stop(); };
    marker_.onClick = [this] { marker(); };
    elsewhere_.onClick = [this] { continueElsewhere(); };
    time_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
    time_.setJustificationType(juce::Justification::centred);
    info_.setFont(juce::FontOptions(11.5f));
    info_.setJustificationType(juce::Justification::centredLeft);
    info_.setMinimumHorizontalScale(0.8f);
    allowEllipsis(info_);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&rec_, &pause_, &stop_, &marker_, &time_, &info_})
        addAndMakeVisible(c);
    addChildComponent(elsewhere_);
    timerCallback();
    startTimerHz(10);
}

TransportBar::~TransportBar() { stopTimer(); }

void TransportBar::record()
{
    auto& rec = controller_.recorder();
    if (rec.state() == Recorder::State::Paused)
    {
        rec.resume();
        return;
    }
    if (rec.state() != Recorder::State::Idle) return;
    const auto settings = controller_.recorderSettings();
    const auto est = DiskGuard::estimate(settings.projectDir, rec.bytesPerSecond(settings));
    if (est.level != DiskLevel::Ok)
    {
        // Warn, never refuse.
        auto* w = new juce::AlertWindow("Low disk space",
                                        "Only about " + duration(est.secondsRemaining) + " of recording fits on this drive (" +
                                            juce::String(static_cast<double>(est.freeBytes) / 1073741824.0, 1) + " GB free).\n\n"
                                            "Record anyway?",
                                        juce::MessageBoxIconType::WarningIcon, this);
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey), juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Record anyway", 1);
        w->enterModalState(true, juce::ModalCallbackFunction::create([this](int r) {
                               if (r == 1) beginRecording();
                           }),
                           true);
        return;
    }
    beginRecording();
}

void TransportBar::beginRecording()
{
    std::string error;
    if (!controller_.startRecording(error))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Recording not started",
                                               juce::String::fromUTF8(error.c_str()));
    timerCallback();
}

void TransportBar::pause()
{
    auto& rec = controller_.recorder();
    if (rec.state() == Recorder::State::Recording) rec.pause();
    else if (rec.state() == Recorder::State::Paused) rec.resume();
    timerCallback();
}

void TransportBar::stop()
{
    if (controller_.recorder().state() == Recorder::State::Idle) return;
    auto* w = new juce::AlertWindow("Stop recording?", "All files will be finalised.", juce::MessageBoxIconType::QuestionIcon, this);
    w->addButton("Keep recording", 0, juce::KeyPress(juce::KeyPress::escapeKey), juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Stop", 1);
    w->enterModalState(true, juce::ModalCallbackFunction::create([this](int r) {
                           if (r != 1) return;
                           controller_.recorder().stop();
                           timerCallback();
                       }),
                       true);
}

void TransportBar::marker()
{
    if (controller_.recorder().state() != Recorder::State::Idle) controller_.recorder().addMarker();
}

void TransportBar::continueElsewhere()
{
    chooser_ = std::make_unique<juce::FileChooser>("Continue the recording in folder...",
                                                   juce::File::getSpecialLocation(juce::File::userDocumentsDirectory));
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
        const auto dir = fc.getResult();
        if (dir == juce::File()) return;
        std::string error;
        if (!controller_.recorder().continueElsewhere(std::filesystem::path(dir.getFullPathName().toWideCharPointer()), error))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Could not continue",
                                                   juce::String::fromUTF8(error.c_str()));
    });
}

void TransportBar::timerCallback()
{
    status_ = controller_.recorder().status();
    ++blink_;
    const auto st = status_.state;
    rec_.setToggleState(st == Recorder::State::Recording, juce::dontSendNotification);
    rec_.setButtonText(st == Recorder::State::Paused ? "RESUME" : "REC");
    pause_.setEnabled(st != Recorder::State::Idle);
    stop_.setEnabled(st != Recorder::State::Idle);
    marker_.setEnabled(st != Recorder::State::Idle);
    const uint64_t frames = st == Recorder::State::Idle ? 0 : status_.frames;
    time_.setText(juce::String(timecode(frames, controller_.settings().sampleRate, false)), juce::dontSendNotification);
    time_.setColour(juce::Label::textColourId, st == Recorder::State::Recording ? colours::record
                                               : st == Recorder::State::Paused ? colours::warn : colours::textDim);

    juce::String info;
    juce::Colour colour = colours::textDim;
    if (st == Recorder::State::Idle)
        info = "Ready  -  " + juce::String(controller_.recordingSettings().format == FileFormat::Flac ? "FLAC" : "WAV") + " " +
               juce::String(static_cast<int>(controller_.recordingSettings().depth)) + "-bit";
    else
    {
        info << juce::String(status_.tracks) << " tracks  -  " << duration(status_.disk.secondsRemaining) << " left";
        if (status_.markerCount) info << "  -  " << status_.markerCount << " markers";
        if (status_.disk.level == DiskLevel::Amber) colour = colours::warn;
        if (status_.disk.level == DiskLevel::Red) colour = (blink_ / 5) % 2 ? colours::error : colours::warn;
    }
    if (status_.droppedFrames > 0)
    {
        info << "  -  DROPOUT " << juce::String(static_cast<double>(status_.droppedFrames) / controller_.settings().sampleRate, 2) << " s";
        colour = colours::error;
    }
    if (status_.writeError)
    {
        info = "WRITE ERROR (" + juce::String::fromUTF8(status_.writeErrorText.c_str()) + ") - audio held in memory, retrying";
        colour = (blink_ / 5) % 2 ? colours::error : colours::warn;
    }
    info_.setText(info, juce::dontSendNotification);
    info_.setColour(juce::Label::textColourId, colour);
    const bool showElsewhere = status_.writeError && status_.writeErrorSeconds > 10.0;
    if (elsewhere_.isVisible() != showElsewhere)
    {
        elsewhere_.setVisible(showElsewhere);
        resized();
    }
    repaint();
}

void TransportBar::resized()
{
    auto b = getLocalBounds().reduced(2, 3);
    const int bw = juce::jmin(78, b.getWidth() / 10);
    rec_.setBounds(b.removeFromLeft(bw).reduced(2, 0));
    pause_.setBounds(b.removeFromLeft(bw).reduced(2, 0));
    stop_.setBounds(b.removeFromLeft(bw).reduced(2, 0));
    marker_.setBounds(b.removeFromLeft(bw + 10).reduced(2, 0));
    time_.setBounds(b.removeFromLeft(110));
    if (elsewhere_.isVisible()) elsewhere_.setBounds(b.removeFromRight(190).reduced(2, 0));
    info_.setBounds(b.withTrimmedLeft(6));
}

void TransportBar::paint(juce::Graphics& g)
{
    g.fillAll(colours::panel);
    if (status_.state == Recorder::State::Recording)
    {
        g.setColour(colours::record.withAlpha(0.15f + 0.1f * static_cast<float>((blink_ / 5) % 2)));
        g.fillRect(getLocalBounds());
    }
}

void TransportBar::collectLayoutIssues(std::vector<std::string>& issues) const
{
    if (info_.getWidth() < 140) issues.push_back("transport info area under 140 px");
}

} // namespace pf8::ui
