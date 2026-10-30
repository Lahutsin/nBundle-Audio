#include "PresetDisplay.h"
#include "UiPalette.h"

using namespace nSynth::ui;

void PresetDisplay::setContent (const juce::String& newTag, const juce::String& newName, const juce::String& newDetail,
                                juce::Colour newAccent)
{
    if (newTag == tag && newName == name && newDetail == detail && newAccent == accent)
        return;
    tag = newTag;
    name = newName;
    detail = newDetail;
    accent = newAccent;
    repaint();
}

void PresetDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (kBackground.darker (isMouseOver() ? 0.1f : 0.3f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (isMouseOver() ? accent : kOutline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    auto area = getLocalBounds().reduced (8, 0);
    // Drop-down arrow
    auto arrowArea = area.removeFromRight (14).toFloat().withSizeKeepingCentre (9.0f, 5.0f);
    juce::Path arrow;
    arrow.addTriangle (arrowArea.getTopLeft(), arrowArea.getTopRight(), { arrowArea.getCentreX(), arrowArea.getBottom() });
    g.setColour (kTextDim);
    g.fillPath (arrow);

    g.setFont (juce::Font (juce::FontOptions (10.0f)).boldened());
    const int tagWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), tag) + 12;
    auto tagArea = area.removeFromLeft (tagWidth).withSizeKeepingCentre (tagWidth, 16).toFloat();
    g.setColour (accent.withAlpha (0.2f));
    g.fillRoundedRectangle (tagArea, 3.0f);
    g.setColour (accent);
    g.drawText (tag, tagArea, juce::Justification::centred);
    area.removeFromLeft (8);

    if (detail.isNotEmpty())
    {
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (kTextDim);
        const int w = juce::jmin (area.getWidth() / 3, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), detail) + 6);
        g.drawText (detail, area.removeFromRight (w), juce::Justification::centredRight);
    }
    g.setFont (juce::Font (juce::FontOptions (14.5f)).boldened());
    g.setColour (kText);
    g.drawFittedText (name, area, juce::Justification::centredLeft, 1, 0.75f);
}
