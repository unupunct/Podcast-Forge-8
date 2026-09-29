#include "ui/MainWindow.h"

namespace pf8::ui {

MixerPage::MixerPage(EngineController& controller) : mixer_(controller)
{
    addAndMakeVisible(mixer_);
    dock_.setTabBarDepth(28);
    dock_.setOutline(0);
    dock_.addTab("ROUTING", colours::background, new RoutingGridView(controller), true);
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

MainComponent::MainComponent(EngineController& controller) : topBar_(controller)
{
    addAndMakeVisible(topBar_);
    tabs_.setTabBarDepth(32);
    tabs_.setOutline(0);
    tabs_.addTab("MIXER", colours::background, new MixerPage(controller), true);
    tabs_.addTab("DEVICE MATRIX", colours::background, new DeviceMatrixView(controller), true);
    tabs_.addTab("DEVICES", colours::background, new DeviceListView(controller), true);
    addAndMakeVisible(tabs_);
    setSize(1600, 900);
}

void MainComponent::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar_.setBounds(r.removeFromTop(56));
    tabs_.setBounds(r);
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
}

MainWindow::~MainWindow()
{
    clearContentComponent();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void MainWindow::closeButtonPressed() { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

} // namespace pf8::ui
