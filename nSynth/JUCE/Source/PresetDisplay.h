#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Header preset display: shows what is loaded (performance or the selected layer's patch) and
// opens the preset browser when clicked.
class PresetDisplay : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    void setContent (const juce::String& tag, const juce::String& name, const juce::String& detail, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void()> onClick;
    juce::String getPresetName() const { return name; }

private:
    juce::String tag, name, detail;
    juce::Colour accent;
};
