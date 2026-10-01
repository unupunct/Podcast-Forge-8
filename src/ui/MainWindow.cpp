#include "ui/MainWindow.h"

#include "record/Recovery.h"

namespace pf8::ui {

MixerPage::MixerPage(EngineController& controller) : mixer_(controller)
{
    addAndMakeVisible(mixer_);
    dock_.setTabBarDepth(28);
    dock_.setOutline(0);
    dock_.addTab("ROUTING", colours::background, new RoutingGridView(controller), true);
    dock_.addTab("HEADPHONES", colours::background, new HeadphonesView(controller), true);
    dock_.addTab("SOUNDBOARD", colours::background, new SoundboardView(controller), true);
    dock_.addTab("MUSIC", colours::background, new MusicView(controller), true);
    dock_.addTab("TALKBACK", colours::background, new TalkbackView(controller), true);
    dock_.addTab("MARKERS", colours::background, new MarkersView(controller), true);
    addAndMakeVisible(dock_);
}

void MixerPage::resized()
{
    auto r = getLocalBounds();
    // The dock gets about 30 % of the height, never less than the routing grid needs.
    const int dockH = juce::jlimit(230, 420, r.getHeight() * 30 / 100);
    dock_.setBounds(r.removeFromBottom(dockH));
    mixer_.setBounds(r);
}

ChannelWindows::~ChannelWindows()
{
    for (auto* arr : {&dsp_, &wizard_})
        for (auto& w : *arr)
            if (w) delete w.getComponent();
}

void ChannelWindows::openDspEditor(int channel)
{
    auto& slot = dsp_[static_cast<size_t>(channel)];
    if (slot)
    {
        slot->toFront(true);
        return;
    }
    auto* editor = new DspEditor(controller_, channel);
    editor->setSize(1280, 760);
    editor->onRunWizard = [this](int ch) { openWizard(ch); };
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(editor);
    o.dialogTitle = "Channel " + juce::String(channel + 1) + " processing";
    o.dialogBackgroundColour = colours::background;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    slot = o.launchAsync();
    if (slot) slot->setResizeLimits(1100, 680, 4000, 3000);
}

void ChannelWindows::openWizard(int channel)
{
    auto& slot = wizard_[static_cast<size_t>(channel)];
    if (slot)
    {
        slot->toFront(true);
        return;
    }
    auto* wiz = new MicWizard(controller_, channel);
    wiz->setSize(860, 600);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(wiz);
    o.dialogTitle = "Microphone setup - channel " + juce::String(channel + 1);
    o.dialogBackgroundColour = colours::background;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    slot = o.launchAsync();
    juce::Component::SafePointer<juce::DialogWindow> safe = slot;
    wiz->onClose = [safe] {
        if (safe) safe->exitModalState(0), delete safe.getComponent();
    };
}

MainComponent::MainComponent(EngineController& controller)
    : controller_(controller), topBar_(controller), transport_(controller), windows_(controller)
{
    addAndMakeVisible(topBar_);
    addAndMakeVisible(transport_);
    tabs_.setTabBarDepth(32);
    tabs_.setOutline(0);
    auto* page = new MixerPage(controller);
    page->mixer().onConfigureChannel = [this](int ch) { windows_.openDspEditor(ch); };
    tabs_.addTab("MIXER", colours::background, page, true);
    tabs_.addTab("DEVICE MATRIX", colours::background, new DeviceMatrixView(controller), true);
    tabs_.addTab("DEVICES", colours::background, new DeviceListView(controller), true);
    addAndMakeVisible(tabs_);
    setSize(1600, 900);
}

void MainComponent::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void MainComponent::offerRecovery()
{
    const auto sessions = findUnfinishedSessions(controller_.recordingSettings().projectDir);
    if (sessions.empty()) return;
    juce::String list;
    for (const auto& s : sessions) list << "  " << juce::String(s.filename().wstring().c_str()) << "\n";
    auto* w = new juce::AlertWindow("Interrupted recording found",
                                    "These sessions were not finalised (the app or PC stopped while recording):\n\n" + list +
                                        "\nRecovery rewrites only the file headers from the audio that is on disk. No audio is changed or deleted.",
                                    juce::MessageBoxIconType::WarningIcon, this);
    w->addButton("Recover now", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Later", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([sessions](int r) {
                           if (r != 1) return;
                           juce::String result;
                           for (const auto& s : sessions)
                           {
                               const auto rep = recoverSession(s);
                               result << juce::String(s.filename().wstring().c_str()) << ": " << (rep.ok() ? "recovered" : "partly recovered - see Recovery.log")
                                      << " (" << static_cast<int>(rep.tracks.size()) << " tracks)\n";
                           }
                           juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Recovery", result);
                       }),
                       true);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar_.setBounds(r.removeFromTop(56));
    tabs_.setBounds(r);
    // The transport sits in the tab-bar row, right of the tabs: no vertical space is used.
    auto& bar = tabs_.getTabbedButtonBar();
    int tabsRight = 0;
    for (int i = 0; i < bar.getNumTabs(); ++i)
        if (auto* b = bar.getTabButton(i)) tabsRight = std::max(tabsRight, b->getRight());
    const int depth = tabs_.getTabBarDepth();
    transport_.setBounds(r.getX() + tabsRight + 16, r.getY(), r.getWidth() - tabsRight - 16, depth);
    transport_.toFront(false);
}

MainWindow::MainWindow(const juce::String& title, EngineController& controller)
    : juce::DocumentWindow(title, colours::background, juce::DocumentWindow::allButtons)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);
    setUsingNativeTitleBar(true);
    setContentOwned(new MainComponent(controller), true);
    setResizable(true, true);
    setResizeLimits(1280, 720, 8192, 8192);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    if (auto* main = dynamic_cast<MainComponent*>(getContentComponent())) main->offerRecovery();
}

MainWindow::~MainWindow()
{
    clearContentComponent();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void MainWindow::closeButtonPressed() { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

} // namespace pf8::ui
