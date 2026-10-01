#pragma once
// SOUNDBOARD dock tab: 24 cart pads (brief §14).
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "engine/EngineController.h"
#include "ui/LookAndFeel.h"
#include "ui/Widgets.h"

namespace pf8::ui {

class CartPad : public juce::Component, public juce::SettableTooltipClient
{
public:
    CartPad(EngineController& controller, int cart);
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void refresh() { repaint(); }
    std::function<void(int cart)> onLoadRequest;

private:
    void showMenu();
    EngineController& controller_;
    const int cart_;
};

class SoundboardView : public juce::Component, public LayoutSelfCheck, private juce::Timer
{
public:
    explicit SoundboardView(EngineController& controller);
    ~SoundboardView() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;
    void loadFile(int cart, const juce::File& file); // background decode, then hand to the engine

private:
    void timerCallback() override;
    EngineController& controller_;
    std::array<std::unique_ptr<CartPad>, kCartCount> pads_;
    juce::TextButton stopAll_{"STOP ALL"}, fadeAll_{"FADE ALL"};
    juce::Label hint_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace pf8::ui
