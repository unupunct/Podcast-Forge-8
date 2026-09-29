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
    setColour(juce::ComboBox::backgroundColourId, panelRaised);
    setColour(juce::ComboBox::outlineColourId, outline);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::TextEditor::backgroundColourId, panelRaised);
    setColour(juce::TextEditor::outlineColourId, outline);
    setColour(juce::TabbedButtonBar::tabOutlineColourId, outline);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, accent);
}

juce::Font LookAndFeel::labelFont(float height, bool bold) const
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return juce::Font(juce::FontOptions(juce::jlimit(10.0f, 16.0f, buttonHeight * 0.5f), juce::Font::bold));
}

float LookAndFeel::faderCapHeight(int sliderWidth) noexcept
{
    const float capW = juce::jmin(static_cast<float>(sliderWidth) * 0.9f, 46.0f);
    return juce::jmax(10.0f, capW * 0.6f);
}

int LookAndFeel::getSliderThumbRadius(juce::Slider& s)
{
    if (s.getSliderStyle() == juce::Slider::LinearVertical) return static_cast<int>(faderCapHeight(s.getWidth()) * 0.5f);
    return juce::LookAndFeel_V4::getSliderThumbRadius(s);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                   juce::Slider& s)
{
    using namespace colours;
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                               static_cast<float>(h)).reduced(2.0f);
    const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float track = juce::jmax(2.0f, r * 0.14f);

    juce::Path bg;
    bg.addCentredArc(c.x, c.y, r - track, r - track, 0.0f, start, end, true);
    g.setColour(outline);
    g.strokePath(bg, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar knobs (pan, trim) light from the centre; unipolar from the start.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? start + static_cast<float>(s.valueToProportionOfLength(0.0)) * (end - start) : start;
    juce::Path val;
    val.addCentredArc(c.x, c.y, r - track, r - track, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
    g.setColour(s.isEnabled() ? accent : textDim);
    g.strokePath(val, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = r - track * 2.2f;
    g.setColour(panelRaised.brighter(0.15f));
    g.fillEllipse(c.x - knobR, c.y - knobR, knobR * 2.0f, knobR * 2.0f);
    g.setColour(outline.brighter(0.3f));
    g.drawEllipse(c.x - knobR, c.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);
    juce::Path pointer;
    pointer.addRoundedRectangle(-track * 0.4f, -knobR, track * 0.8f, knobR * 0.55f, track * 0.4f);
    g.setColour(text);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(c.x, c.y));
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                   juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }
    using namespace colours;
    const float cx = static_cast<float>(x) + static_cast<float>(w) * 0.5f;
    const float slot = juce::jmax(4.0f, static_cast<float>(w) * 0.08f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(cx - slot * 0.5f, static_cast<float>(y), slot, static_cast<float>(h), slot * 0.5f);

    // Fader cap: a ribbed rectangle, JUCE passes `pos` as the cap centre in pixels.
    const float capW = juce::jmin(static_cast<float>(w) * 0.9f, 46.0f);
    const float capH = faderCapHeight(w);
    auto cap = juce::Rectangle<float>(cx - capW * 0.5f, pos - capH * 0.5f, capW, capH);
    g.setColour(panelRaised.brighter(0.25f));
    g.fillRoundedRectangle(cap, 3.0f);
    g.setColour(outline.brighter(0.4f));
    g.drawRoundedRectangle(cap, 3.0f, 1.0f);
    g.setColour(s.isMouseButtonDown() ? accent : text);
    g.fillRect(cap.withSizeKeepingCentre(capW * 0.8f, 2.0f));
    g.setColour(outline.brighter(0.6f));
    for (float d : {-0.28f, 0.28f})
        g.fillRect(cap.withSizeKeepingCentre(capW * 0.6f, 1.0f).translated(0.0f, capH * d));
}

} // namespace pf8::ui
