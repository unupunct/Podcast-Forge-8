#include "ui/MainWindow.h"

namespace pf8::ui {

MainComponent::MainComponent(EngineController& controller) : topBar_(controller), devices_(controller)
{
    addAndMakeVisible(topBar_);
    addAndMakeVisible(devices_);
    setSize(1600, 900);
}

void MainComponent::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar_.setBounds(r.removeFromTop(56));
    devices_.setBounds(r);
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
