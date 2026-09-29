#include "ui/Widgets.h"

#include <cmath>

#include "ui/LookAndFeel.h"

namespace pf8::ui {
namespace {

float toDb(float lin) { return lin > 1e-6f ? 20.0f * std::log10(lin) : -120.0f; }

float textWidth(const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, s, 0.0f, 0.0f);
    return ga.getBoundingBox(0, -1, true).getWidth();
}

juce::Font captionFont(float h) { return juce::Font(juce::FontOptions(h, juce::Font::bold)); }

} // namespace

juce::String formatDb(double db, int decimals)
{
    if (db <= -69.5) return juce::String::fromUTF8("-\xe2\x88\x9e");
    return (db > 0 ? "+" : "") + juce::String(db, decimals);
}

// ---------------------------------------------------------------------------------------------
Knob::Knob(juce::String caption, double min, double max, double def, std::function<juce::String(double)> format)
    : caption_(std::move(caption)), format_(std::move(format))
{
    slider_.setRange(min, max, 0.0);
    slider_.setValue(def, juce::dontSendNotification);
    slider_.setDoubleClickReturnValue(true, def);
    slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider_.onValueChange = [this] {
        repaint();
        if (onChange) onChange(slider_.getValue());
    };
    addAndMakeVisible(slider_);
}

namespace {
int knobCaption(int h) { return juce::jlimit(11, 16, h * 16 / 100); }
} // namespace

void Knob::resized()
{
    auto b = getLocalBounds();
    const int cap = knobCaption(b.getHeight());
    b.removeFromTop(cap);
    b.removeFromBottom(cap);
    const int d = juce::jmin(b.getWidth(), b.getHeight());
    slider_.setBounds(b.withSizeKeepingCentre(d, d));
}

void Knob::paint(juce::Graphics& g)
{
    auto b = getLocalBounds();
    const int cap = knobCaption(b.getHeight());
    g.setColour(colours::textDim);
    g.setFont(captionFont(cap * 0.8f));
    g.drawText(caption_, b.removeFromTop(cap), juce::Justification::centred, false);
    g.setColour(colours::text);
    g.setFont(juce::Font(juce::FontOptions(cap * 0.8f)));
    g.drawText(format_ ? format_(slider_.getValue()) : juce::String(slider_.getValue(), 1), b.removeFromBottom(cap),
               juce::Justification::centred, false);
}

void Knob::collectLayoutIssues(std::vector<std::string>& issues) const
{
    const int cap = knobCaption(getHeight());
    if (slider_.getWidth() < 36)
        issues.push_back("Knob '" + caption_.toStdString() + "' dial only " + std::to_string(slider_.getWidth()) + " px");
    const float w = static_cast<float>(getWidth());
    if (textWidth(captionFont(cap * 0.8f), caption_) > w)
        issues.push_back("Knob caption '" + caption_.toStdString() + "' wider than " + std::to_string(getWidth()) + " px");
    const auto v = format_ ? format_(slider_.getMaximum()) : juce::String(slider_.getMaximum(), 1);
    if (textWidth(juce::Font(juce::FontOptions(cap * 0.8f)), v) > w)
        issues.push_back("Knob value '" + v.toStdString() + "' wider than " + std::to_string(getWidth()) + " px");
}

// ---------------------------------------------------------------------------------------------
Fader::Fader()
{
    slider_.setRange(kMinDb, kMaxDb, 0.0);
    slider_.setSkewFactorFromMidPoint(-15.0);
    slider_.setValue(0.0, juce::dontSendNotification);
    slider_.setDoubleClickReturnValue(true, 0.0);
    slider_.onValueChange = [this] {
        repaint(readoutArea_);
        if (onChange) onChange(gain());
    };
    addAndMakeVisible(slider_);
}

void Fader::setGain(float linear)
{
    const double db = linear <= 0.0f ? kMinDb : juce::jlimit(kMinDb, kMaxDb, 20.0 * std::log10(static_cast<double>(linear)));
    slider_.setValue(db, juce::dontSendNotification);
    repaint(readoutArea_);
}

float Fader::gain() const
{
    const double db = slider_.getValue();
    return db <= kMinDb + 0.5 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0));
}

void Fader::resized()
{
    auto b = getLocalBounds();
    readoutArea_ = b.removeFromBottom(juce::jmax(16, getHeight() / 18));
    scaleArea_ = b.removeFromLeft(b.getWidth() * 2 / 5);
    slider_.setBounds(b);
}

void Fader::paint(juce::Graphics& g)
{
    g.setColour(colours::textDim);
    const float fh = juce::jlimit(9.0f, 13.0f, static_cast<float>(getWidth()) * 0.16f);
    g.setFont(juce::Font(juce::FontOptions(fh)));
    // Scale ticks at the slider's own positions; a label that would touch the previous one is skipped.
    float lastY = -1000.0f;
    for (double db : {10.0, 5.0, 0.0, -5.0, -10.0, -20.0, -30.0, -40.0, -60.0})
    {
        const double prop = slider_.valueToProportionOfLength(db);
        const auto sb = slider_.getBounds();
        const float thumbHalf = static_cast<float>(LookAndFeel::faderCapHeight(sb.getWidth()) * 0.5f);
        const float y = static_cast<float>(sb.getBottom()) - thumbHalf -
                        static_cast<float>(prop) * (static_cast<float>(sb.getHeight()) - 2.0f * thumbHalf);
        if (y - lastY < fh * 1.15f) continue;
        lastY = y;
        g.drawText(juce::String(static_cast<int>(db)), scaleArea_.withY(static_cast<int>(y - fh * 0.6f)).withHeight(static_cast<int>(fh * 1.2f)),
                   juce::Justification::centredRight, false);
    }
    g.setColour(colours::text);
    g.setFont(juce::Font(juce::FontOptions(juce::jlimit(10.0f, 15.0f, static_cast<float>(readoutArea_.getHeight()) * 0.8f), juce::Font::bold)));
    g.drawText(formatDb(slider_.getValue()) + " dB", readoutArea_, juce::Justification::centred, false);
}

void Fader::collectLayoutIssues(std::vector<std::string>& issues) const
{
    const juce::Font f{juce::FontOptions(juce::jlimit(10.0f, 15.0f, static_cast<float>(readoutArea_.getHeight()) * 0.8f), juce::Font::bold)};
    if (textWidth(f, "-60.0 dB") > static_cast<float>(readoutArea_.getWidth()))
        issues.push_back("Fader readout wider than " + std::to_string(readoutArea_.getWidth()) + " px");
    if (slider_.getHeight() < 80) issues.push_back("Fader travel only " + std::to_string(slider_.getHeight()) + " px");
}

// ---------------------------------------------------------------------------------------------
void Meter::setLevels(const float* peak, const float* rms)
{
    for (int c = 0; c < channels_; ++c)
    {
        const float p = toDb(peak[c]), r = toDb(rms[c]);
        peakDb_[c] = p > peakDb_[c] ? p : std::max(p, peakDb_[c] - 1.5f); // ~45 dB/s at 30 Hz
        rmsDb_[c] = r > rmsDb_[c] ? r : std::max(r, rmsDb_[c] - 1.5f);
        if (p >= holdDb_[c]) { holdDb_[c] = p; holdTicks_[c] = 45; }
        else if (--holdTicks_[c] < 0) holdDb_[c] = std::max(p, holdDb_[c] - 1.5f);
        if (peak[c] >= 0.999f) clip_ = true; // sample at (or beyond) full scale
    }
    repaint();
}

void Meter::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float clipH = juce::jmax(6.0f, b.getHeight() * 0.03f);
    auto clipBox = b.removeFromTop(clipH);
    g.setColour(clip_ ? colours::error : colours::panelRaised);
    g.fillRoundedRectangle(clipBox.reduced(1.0f), 1.5f);
    b.removeFromTop(2.0f);

    const float gap = 2.0f;
    const float w = (b.getWidth() - gap * static_cast<float>(channels_ - 1)) / static_cast<float>(channels_);
    auto yFor = [&](float db) {
        const float prop = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        return b.getBottom() - prop * b.getHeight();
    };
    for (int c = 0; c < channels_; ++c)
    {
        auto col = juce::Rectangle<float>(b.getX() + static_cast<float>(c) * (w + gap), b.getY(), w, b.getHeight());
        g.setColour(colours::panelRaised);
        g.fillRect(col);
        const float top = yFor(peakDb_[c]);
        juce::ColourGradient grad(colours::ok, 0, yFor(-60.0f), colours::error, 0, yFor(0.0f), false);
        grad.addColour((yFor(-60.0f) - yFor(-18.0f)) / juce::jmax(1.0f, yFor(-60.0f) - yFor(0.0f)), colours::ok);
        grad.addColour((yFor(-60.0f) - yFor(-6.0f)) / juce::jmax(1.0f, yFor(-60.0f) - yFor(0.0f)), colours::warn);
        g.setGradientFill(grad);
        g.fillRect(col.withTop(top));
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.fillRect(juce::Rectangle<float>(col.getX(), yFor(rmsDb_[c]) - 1.0f, col.getWidth(), 2.0f));
        g.setColour(holdDb_[c] > -0.5f ? colours::error : colours::text);
        g.fillRect(juce::Rectangle<float>(col.getX(), yFor(holdDb_[c]) - 1.0f, col.getWidth(), 2.0f));
    }
    // −18 dBFS reference (speech target) and 0 dBFS lines.
    g.setColour(colours::outline);
    for (float db : {-18.0f, -6.0f})
        g.drawHorizontalLine(static_cast<int>(yFor(db)), b.getX(), b.getRight());
}

// ---------------------------------------------------------------------------------------------
ToggleLed::ToggleLed(const juce::String& text, juce::Colour onColour) : juce::TextButton(text)
{
    setClickingTogglesState(true);
    setColour(juce::TextButton::buttonOnColourId, onColour);
    setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    setColour(juce::TextButton::buttonColourId, colours::panelRaised);
    setColour(juce::TextButton::textColourOffId, colours::textDim);
    onClick = [this] {
        if (onToggle) onToggle(getToggleState());
    };
}

void StatusLed::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float d = juce::jmin(b.getWidth(), b.getHeight()) - 2.0f;
    auto dot = b.withSizeKeepingCentre(d, d);
    g.setColour(colour_.withAlpha(0.35f));
    g.fillEllipse(dot.expanded(1.5f));
    g.setColour(colour_);
    g.fillEllipse(dot);
}

} // namespace pf8::ui
