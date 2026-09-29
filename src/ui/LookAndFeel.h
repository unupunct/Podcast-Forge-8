#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace pf8::ui {

// Dark console palette. Every colour used by the UI comes from here.
namespace colours {
inline const juce::Colour background {0xff111317};
inline const juce::Colour panel      {0xff1a1d23};
inline const juce::Colour panelRaised{0xff23272f};
inline const juce::Colour outline    {0xff30353f};
inline const juce::Colour text       {0xffe6e8eb};
inline const juce::Colour textDim    {0xff8b929e};
inline const juce::Colour accent     {0xff3fa9f5};
inline const juce::Colour ok         {0xff3ecf6e};
inline const juce::Colour warn       {0xfff2b134};
inline const juce::Colour error      {0xfff0524f};
inline const juce::Colour record     {0xffe53935};
// Function colours (hardware-console convention).
inline const juce::Colour mute       {0xffe5484d};
inline const juce::Colour solo       {0xfff5c518};
inline const juce::Colour pfl        {0xff35c7a6};
inline const juce::Colour dsp        {0xff5aa9ff};
inline const juce::Colour mon        {0xff7c8cff};
} // namespace colours

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    juce::Font labelFont(float height, bool bold = false) const;

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    int getSliderThumbRadius(juce::Slider&) override;
    static float faderCapHeight(int sliderWidth) noexcept;
};

} // namespace pf8::ui
