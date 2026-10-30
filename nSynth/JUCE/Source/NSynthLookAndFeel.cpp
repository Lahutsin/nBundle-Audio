#include "NSynthLookAndFeel.h"

using namespace nSynth::ui;

NSynthLookAndFeel::NSynthLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kText);
    setColour (juce::ComboBox::backgroundColourId, kBackground);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, kOutline);
    setColour (juce::ComboBox::arrowColourId, kTextDim);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::ToggleButton::textColourId, kText);
    setColour (juce::ToggleButton::tickColourId, kUpperAccent);
    setColour (juce::TextButton::buttonColourId, kPanel);
    setColour (juce::TextButton::buttonOnColourId, kUpperAccent.darker (0.4f));
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, kText);
    setColour (juce::ResizableWindow::backgroundColourId, kBackground);
}

juce::Colour NSynthLookAndFeel::accentFor (const juce::Component& c)
{
    const auto v = c.getProperties()["accent"];
    return v.isVoid() ? kUpperAccent : juce::Colour ((juce::uint32) (juce::int64) v);
}

juce::Label* NSynthLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::Font (juce::FontOptions (11.0f)));
    l->setMinimumHorizontalScale (0.65f);
    l->setBorderSize (juce::BorderSize<int> (0, 1, 0, 1));
    return l;
}

void NSynthLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const float fontSize = juce::jlimit (8.5f, 11.0f, (float) button.getHeight() * 0.48f);
    g.setFont (juce::Font (juce::FontOptions (fontSize)).boldened());
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                       .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    const auto textBounds = button.getLocalBounds().reduced (2, 1);
    g.drawFittedText (button.getButtonText(), textBounds, juce::Justification::centred, 1, 0.55f);
}

void NSynthLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (1.5f);
    // Room for the modulation ring outside the value arc.
    const float outer = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float ringW = juce::jmax (1.5f, outer * 0.07f);
    const float radius = outer - ringW - 1.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (1.8f, radius * 0.17f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = start + pos * (end - start);
    const auto accent = accentFor (s);

    const auto& props = s.getProperties();
    if ((bool) props.getWithDefault ("modOn", false))
    {
        // Modulation ring: the range the slots can reach, and a dot at the live value.
        const float ringR = outer - ringW * 0.5f;
        auto angleAt = [&] (float p) { return start + juce::jlimit (0.0f, 1.0f, p) * (end - start); };
        const float low = pos + (float) props.getWithDefault ("modLow", 0.0f);
        const float high = pos + (float) props.getWithDefault ("modHigh", 0.0f);
        juce::Path ring;
        ring.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, angleAt (low), angleAt (high), true);
        g.setColour (kModAccent.withAlpha (0.85f));
        g.strokePath (ring, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float liveAngle = angleAt (pos + (float) props.getWithDefault ("modLive", 0.0f));
        const juce::Point<float> dot (centre.x + ringR * std::sin (liveAngle), centre.y - ringR * std::cos (liveAngle));
        g.setColour (kText);
        g.fillEllipse (juce::Rectangle<float> (ringW * 2.4f, ringW * 2.4f).withCentre (dot));
    }

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (kOutline);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar parameters draw their arc from the centre.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (kPanel.brighter (0.15f));
    g.fillEllipse (juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (centre));
    juce::Path pointer;
    pointer.addRoundedRectangle (-lineW * 0.4f, -radius * 0.62f, lineW * 0.8f, radius * 0.45f, lineW * 0.4f);
    g.setColour (kText);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void NSynthLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                          juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    const bool horizontal = style == juce::Slider::LinearHorizontal;
    const auto track = horizontal ? r.withSizeKeepingCentre (r.getWidth(), 8.0f) : r.withSizeKeepingCentre (8.0f, r.getHeight());
    g.setColour (kOutline);
    g.fillRoundedRectangle (track, 4.0f);

    const float centrePos = horizontal ? r.getCentreX() : r.getCentreY();
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    g.setColour (accentFor (s));
    if (horizontal)
    {
        const float from = bipolar ? centrePos : r.getX();
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, pos), track.getY(), juce::jmax (from, pos), track.getBottom()), 4.0f);
        g.setColour (kText);
        g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, r.getHeight() * 0.8f).withCentre ({ pos, r.getCentreY() }), 2.0f);
    }
    else
    {
        const float from = bipolar ? centrePos : r.getBottom();
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (track.getX(), juce::jmin (from, pos), track.getRight(), juce::jmax (from, pos)), 4.0f);
        g.setColour (kText);
        g.fillRoundedRectangle (juce::Rectangle<float> (r.getWidth() * 0.8f, 6.0f).withCentre ({ r.getCentreX(), pos }), 2.0f);
    }
}
