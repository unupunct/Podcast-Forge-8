#include "ui/LookAndFeel.h"

namespace pf8::ui {

LookAndFeel::LookAndFeel()
{
    using namespace colours;
    auto scheme = juce::LookAndFeel_V4::ColourScheme(background, panel, panelRaised, outline, text, accent,
                                                     juce::Colours::white, panelRaised, text);
    setColourScheme(scheme);
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::TableHeaderComponent::backgroundColourId, panelRaised);
    setColour(juce::TableHeaderComponent::textColourId, textDim);
    setColour(juce::TableHeaderComponent::outlineColourId, outline);
    setColour(juce::ListBox::backgroundColourId, panel);
    setColour(juce::ListBox::outlineColourId, outline);
    setColour(juce::Label::textColourId, text);
    setColour(juce::TextButton::buttonColourId, panelRaised);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::ScrollBar::thumbColourId, outline);
}

juce::Font LookAndFeel::labelFont(float height, bool bold) const
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

} // namespace pf8::ui
