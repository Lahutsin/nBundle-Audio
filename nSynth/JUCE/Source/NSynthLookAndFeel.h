#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UiPalette.h"

class NSynthLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NSynthLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (11.5f)); }
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Label* createSliderTextBox (juce::Slider& s) override;

    static juce::Colour accentFor (const juce::Component&);
};
