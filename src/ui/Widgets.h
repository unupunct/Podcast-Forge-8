#pragma once
// Console widgets: Knob, Fader, Meter, ToggleLed, plus the layout self-check hook used by
// --verify-ui for components that paint their own text.
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace pf8::ui {

// Components that draw text themselves report text that doesn't fit.
class LayoutSelfCheck
{
public:
    virtual ~LayoutSelfCheck() = default;
    virtual void collectLayoutIssues(std::vector<std::string>& issues) const = 0;
};

// Rotary knob with a caption above and the formatted value below.
class Knob : public juce::Component, public LayoutSelfCheck
{
public:
    Knob(juce::String caption, double min, double max, double def, std::function<juce::String(double)> format);
    juce::Slider& slider() noexcept { return slider_; }
    void setValue(double v, juce::NotificationType n = juce::dontSendNotification) { slider_.setValue(v, n); }
    double value() const { return slider_.getValue(); }
    std::function<void(double)> onChange;

    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;

private:
    juce::String caption_;
    std::function<juce::String(double)> format_;
    juce::Slider slider_{juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox};
};

// Vertical fader in dB (−∞ at the bottom). value(): dB, gain(): linear.
class Fader : public juce::Component, public LayoutSelfCheck
{
public:
    static constexpr double kMinDb = -70.0, kMaxDb = 10.0;
    Fader();
    juce::Slider& slider() noexcept { return slider_; }
    void setGain(float linear);
    float gain() const;
    std::function<void(float gain)> onChange;

    void resized() override;
    void paint(juce::Graphics&) override;
    void collectLayoutIssues(std::vector<std::string>& issues) const override;

private:
    juce::Slider slider_{juce::Slider::LinearVertical, juce::Slider::NoTextBox};
    juce::Rectangle<int> scaleArea_, readoutArea_;
};

// Vertical meter: 1 or 2 channels, peak bar + RMS, 1.5 s peak hold, clip latch (click to reset).
class Meter : public juce::Component
{
public:
    explicit Meter(int channels = 1) : channels_(channels) {}
    void setLevels(const float* peakLinear, const float* rmsLinear); // `channels` values each
    bool clipLatched() const noexcept { return clip_; }
    void resetClip() { clip_ = false; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { resetClip(); }

private:
    int channels_;
    float peakDb_[2]{-120.f, -120.f}, rmsDb_[2]{-120.f, -120.f}, holdDb_[2]{-120.f, -120.f};
    int holdTicks_[2]{};
    bool clip_ = false;
};

// Toggle button that lights up in a function colour when on.
class ToggleLed : public juce::TextButton
{
public:
    ToggleLed(const juce::String& text, juce::Colour onColour);
    std::function<void(bool)> onToggle;
};

// Small status dot.
class StatusLed : public juce::Component
{
public:
    void setColour(juce::Colour c) { if (c != colour_) { colour_ = c; repaint(); } }
    void paint(juce::Graphics& g) override;

private:
    juce::Colour colour_{juce::Colours::grey};
};

juce::String formatDb(double db, int decimals = 1);

// Marks a label whose text may legitimately be truncated with an ellipsis (e.g. device names,
// full text in the tooltip); --verify-ui accepts truncation there.
inline void allowEllipsis(juce::Component& c) { c.getProperties().set("pf8.ellipsisOk", true); }
inline bool ellipsisAllowed(const juce::Component& c) { return c.getProperties().getWithDefault("pf8.ellipsisOk", false); }

} // namespace pf8::ui
