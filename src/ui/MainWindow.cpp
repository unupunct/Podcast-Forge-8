#include "ui/MainWindow.h"

namespace pf8::ui {

MainComponent::MainComponent(EngineController& controller) : topBar_(controller)
{
    addAndMakeVisible(topBar_);
    tabs_.setTabBarDepth(34);
    tabs_.setOutline(0);
    tabs_.addTab("CHANNELS", colours::background, new ChannelsView(controller), true);
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
