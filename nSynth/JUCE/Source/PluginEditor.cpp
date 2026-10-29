#include "PluginEditor.h"

using namespace nsynth;

using namespace nSynth::ui;

namespace
{

juce::Colour accentForLayer (Layer l) { return l == Layer::upper ? kUpperAccent : kLowerAccent; }

juce::String shortCaption (const juce::String& name)
{
    for (auto prefix : { "DDS Mod ", "DDS1 ", "DDS2 ", "VCF ", "VCA ", "ENV1 ", "ENV2 ", "LFO1 ", "LFO2 " })
        if (name.startsWith (prefix))
            return name.substring ((int) std::strlen (prefix));
    if (name == "Bender/LFO2 Osc Destination") return "Bend/LFO2 Dest";
    if (name == "Mod Lever (Bender Up)") return "Mod Lever";
    if (name == "Clock Sync (LFO1/Delay)") return "Clock Sync";
    if (name == "Lower Layer Detune") return "Lower Detune";
    if (name == "Performance Detune") return "Perf Detune";
    if (name == "Single Mode Layer") return "Single Layer";
    if (name == "Chorus") return "Mode";
    if (name == "Delay Division") return "Time";
    if (name == "Binaural Detune") return "Bin Detune";
    if (name == "Velocity Mode") return "Vel Mode";
    if (name == "Static Velocity") return "Static Vel";
    if (name == "Velocity Curve") return "Vel Curve";
    if (name == "Velocity Sensitivity") return "Vel Sens";
    if (name == "Comp Key") return "Key";
    if (name == "Delay Ping-Pong") return "Ping-Pong";
    if (name == "Reverb High Cut") return "High Cut";
    for (auto prefix : { "Chorus ", "Delay ", "Reverb ", "Filter 2 ", "Distortion ", "Comp ", "Phaser ", "Flanger ",
                         "EQ ", "MSEG1 ", "MSEG2 " })
        if (name.startsWith (prefix))
            return name.substring ((int) std::strlen (prefix));
    return name;
}

juce::String shortChoiceText (const juce::String& choice)
{
    if (choice == "Norm / Sub Off") return "Norm";
    if (choice == "Ring / Sub Square") return "Ring";
    if (choice == "Sync / Sub Sine") return "Sync";
    if (choice == "ENV 2") return "ENV2";
    if (choice == "Fixed 1 (Gate)") return "Gate";
    if (choice == "Fixed 2 (Gate+Rel)") return "G+Rel";
    if (choice == "Normal") return "Norm";
    if (choice == "Inverted") return "Inv";
    if (choice == "Free / Norm") return "Free";
    if (choice == "Once / DDS 1") return "Once";
    if (choice == "Reset / DDS 2") return "Reset";
    if (choice == "Manual") return "Man";
    if (choice == "LFO 1") return "LFO1";
    if (choice == "ENV 1") return "ENV1";
    if (choice == "DDS 1") return "DDS1";
    if (choice == "1 + 2") return "1+2";
    if (choice == "DDS 2") return "DDS2";
    if (choice == "AT + Trig") return "AT+Tr";
    if (choice == "On (AT->Bend)") return "On";
    if (choice == "Serial") return "Ser";
    if (choice == "Parallel") return "Par";
    return choice;
}

// Overlay pages (mod matrix, wave bank): a framed panel with an accent strip, a title row on
// top and a centred CLOSE button at the bottom.
constexpr int kPageHeader = 32, kPageFooter = 40, kPageTitleWidth = 320;

juce::Rectangle<int> pageContent (juce::Rectangle<int> bounds)
{
    return bounds.reduced (12).reduced (16, 14);
}

void paintPageFrame (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour accent)
{
    g.fillAll (kBackground);
    const auto panel = bounds.reduced (12).toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (kOutline);
    g.drawRoundedRectangle (panel.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (accent);
    g.fillRect (panel.withHeight (3.0f).reduced (8.0f, 0.0f));
}

// Page name followed by a tag chip (e.g. the edited layer).
void paintPageTitle (juce::Graphics& g, juce::Rectangle<int> header, const juce::String& name,
                     const juce::String& tag, juce::Colour accent)
{
    auto area = header.withWidth (kPageTitleWidth);
    g.setFont (juce::Font (juce::FontOptions (19.0f)).boldened());
    g.setColour (kText);
    const int nameWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name);
    g.drawText (name, area.removeFromLeft (nameWidth + 12), juce::Justification::centredLeft);

    g.setFont (juce::Font (juce::FontOptions (11.0f)).boldened());
    const int tagWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), tag) + 14;
    const auto chip = area.removeFromLeft (tagWidth).withSizeKeepingCentre (tagWidth, 20).toFloat();
    g.setColour (accent.withAlpha (0.2f));
    g.fillRoundedRectangle (chip, 4.0f);
    g.setColour (accent);
    g.drawText (tag, chip, juce::Justification::centred);
}

// One waveform cycle scaled into the area, over a zero line.
void paintCycle (juce::Graphics& g, const std::vector<float>& cycle, juce::Rectangle<float> area,
                 juce::Colour colour, float thickness, int points)
{
    g.setColour (kOutline);
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    if (cycle.empty() || area.getHeight() < 4.0f)
        return;

    juce::Path path;
    for (int i = 0; i < points; ++i)
    {
        const float normX = (float) i / (float) (points - 1);
        const size_t idx = (size_t) juce::jlimit (0, (int) cycle.size() - 1, (int) (normX * (float) (cycle.size() - 1)));
        const float x = area.getX() + normX * area.getWidth();
        const float y = area.getCentreY() - juce::jlimit (-1.0f, 1.0f, cycle[idx]) * (area.getHeight() * 0.48f);
        if (i == 0) path.startNewSubPath (x, y);
        else        path.lineTo (x, y);
    }
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace

//==============================================================================
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

//==============================================================================
ParamControl::ParamControl (juce::AudioProcessorValueTreeState& state, const juce::String& id,
                            const ParamSpec& spec, juce::Colour accent, const juce::String& captionOverride)
    : paramId (id),
      captionText (captionOverride.isNotEmpty() ? captionOverride : shortCaption (spec.name))
{
    caption.setText (captionText, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setColour (juce::Label::textColourId, kTextDim);
    caption.setMinimumHorizontalScale (0.55f);
    caption.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (caption);

    jassert (state.getParameter (id) != nullptr);
    setTooltip (juce::String (spec.name) + (spec.notes[0] != 0 ? " - " + juce::String (spec.notes) : juce::String()));
    addMouseListener (this, true);

    switch (spec.kind)
    {
        case Kind::choiceParam:
            combo = std::make_unique<juce::ComboBox>();
            combo->addItemList (spec.choices, 1);
            combo->getProperties().set ("accent", (juce::int64) accent.getARGB());
            addChildComponent (*combo);
            comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, *combo);
            // Three-way switches, and two-way switches with short labels, are segmented buttons.
            if (spec.choices.size() == 3
                || (spec.choices.size() == 2 && spec.choices[0].length() <= 7 && spec.choices[1].length() <= 7))
            {
                for (int i = 0; i < spec.choices.size(); ++i)
                {
                    auto button = std::make_unique<juce::TextButton> (shortChoiceText (spec.choices[i]));
                    button->setTooltip (spec.choices[i]);
                    button->setClickingTogglesState (true);
                    button->onClick = [this, i]
                    {
                        combo->setSelectedId (i + 1, juce::sendNotificationSync);
                    };
                    choiceButtons.push_back (std::move (button));
                    addAndMakeVisible (*choiceButtons.back());
                }
                startTimerHz (20);
            }
            else
            {
                combo->setVisible (true);
            }
            break;

        case Kind::boolParam:
            toggle = std::make_unique<juce::ToggleButton> ("");
            toggle->setColour (juce::ToggleButton::tickColourId, accent);
            addAndMakeVisible (*toggle);
            buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id, *toggle);
            break;

        case Kind::intParam:
        case Kind::floatParam:
        {
            const bool ribbonLike = juce::String (spec.suffix) == pid::ribbon;
            slider = std::make_unique<juce::Slider> (ribbonLike ? juce::Slider::LinearHorizontal : juce::Slider::RotaryHorizontalVerticalDrag,
                                                     juce::Slider::TextBoxBelow);
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 14);
            slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider->setColour (juce::Slider::textBoxTextColourId, kText);
            slider->getProperties().set ("accent", (juce::int64) accent.getARGB());
            addAndMakeVisible (*slider);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, *slider);
            slider->setDoubleClickReturnValue (true, spec.defaultValue);

            // The ribbon is relative: it springs back to the centre on release.
            if (ribbonLike)
                slider->onDragEnd = [s = slider.get()] { s->setValue (0.0, juce::sendNotificationSync); };
            break;
        }
    }
    if (combo != nullptr && ! choiceButtons.empty())
    {
        const int selected = combo->getSelectedId() - 1;
        for (int i = 0; i < (int) choiceButtons.size(); ++i)
            choiceButtons[(size_t) i]->setToggleState (i == selected, juce::dontSendNotification);
    }
    startTimerHz (5);
}

void ParamControl::setMidiLearnHandlers (std::function<bool()> learn, std::function<void()> clear,
                                         std::function<int()> assignedCC)
{
    learnMidiCC = std::move (learn);
    clearMidiCC = std::move (clear);
    getAssignedMidiCC = std::move (assignedCC);
}

void ParamControl::mouseDown (const juce::MouseEvent& event)
{
    if (! event.mods.isRightButtonDown())
        return;
    juce::PopupMenu menu;
    menu.addItem (1, "Learn MIDI CC");
    const int assigned = getAssignedMidiCC != nullptr ? getAssignedMidiCC() : -1;
    menu.addItem (2, assigned >= 0 ? "Clear MIDI CC " + juce::String (assigned) : "Clear MIDI CC", assigned >= 0);
    if (modulationHost != nullptr && parameterIndex >= 0 && modulationHost->canModulate (layer, parameterIndex))
    {
        menu.addSeparator();
        modulationHost->addModulationMenuItems (menu, layer, parameterIndex);
    }
    juce::Component::SafePointer<ParamControl> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safeThis] (int result)
    {
        if (safeThis == nullptr)
            return;
        if (result == 1 && safeThis->learnMidiCC != nullptr)
        {
            if (safeThis->learnMidiCC())
                safeThis->setTooltip ("Move a MIDI controller to assign it to " + safeThis->paramId + ".");
        }
        else if (result == 2 && safeThis->clearMidiCC != nullptr)
            safeThis->clearMidiCC();
        else if (result >= ModulationHost::kModulationMenuBase && safeThis->modulationHost != nullptr)
            safeThis->modulationHost->handleModulationMenu (result, safeThis->layer, safeThis->parameterIndex);
    });
}

void ParamControl::setModulationTarget (ModulationHost* host, Layer targetLayer, int index)
{
    modulationHost = host;
    layer = targetLayer;
    parameterIndex = index;
}

void ParamControl::setModulationDisplay (bool modulated, float rangeLow, float rangeHigh, float live)
{
    if (slider == nullptr || slider->getSliderStyle() != juce::Slider::RotaryHorizontalVerticalDrag)
        return;
    auto& props = slider->getProperties();
    const bool was = props.getWithDefault ("modOn", false);
    if (! modulated && ! was)
        return;
    const float oldLow = props.getWithDefault ("modLow", 0.0f), oldHigh = props.getWithDefault ("modHigh", 0.0f),
                oldLive = props.getWithDefault ("modLive", 0.0f);
    if (was == modulated && std::abs (oldLow - rangeLow) < 1.0e-4f && std::abs (oldHigh - rangeHigh) < 1.0e-4f
        && std::abs (oldLive - live) < 2.0e-3f)
        return;
    props.set ("modOn", modulated);
    props.set ("modLow", rangeLow);
    props.set ("modHigh", rangeHigh);
    props.set ("modLive", live);
    slider->repaint();
}

void ParamControl::setCaptionVisible (bool shouldShow)
{
    captionVisible = shouldShow;
    caption.setVisible (shouldShow);
    resized();
}

void ParamControl::setEditableCaption (std::function<juce::String()> getText, std::function<void (const juce::String&)> setText)
{
    getCaptionText = std::move (getText);
    caption.setEditable (false, true, false);
    caption.setTooltip ("Double-click to rename");
    caption.onTextChange = [this, setText = std::move (setText)]
    {
        setText (caption.getText());
        captionText = getCaptionText();
    };
    captionText = getCaptionText();
    caption.setText (captionText, juce::dontSendNotification);
}

bool ParamControl::isInterestedInDragSource (const SourceDetails& details)
{
    return modulationHost != nullptr && parameterIndex >= 0
        && ModulationHost::sourceFromDrag (details.description) != ModSource::count
        && modulationHost->canModulate (layer, parameterIndex);
}

void ParamControl::itemDropped (const SourceDetails& details)
{
    dragHover = false;
    repaint();
    modulationHost->dropModulationSource (ModulationHost::sourceFromDrag (details.description), layer, parameterIndex);
}

void ParamControl::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover)
        return;
    g.setColour (kModAccent);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f, 2.0f);
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    if (captionVisible)
        caption.setBounds (r.removeFromTop (13));
    if (slider != nullptr)
        slider->setBounds (r);
    else if (! choiceButtons.empty())
    {
        const int buttonWidth = r.getWidth() / (int) choiceButtons.size();
        for (int i = 0; i < (int) choiceButtons.size(); ++i)
            choiceButtons[(size_t) i]->setBounds (r.getX() + i * buttonWidth, r.getY() + 1,
                                                  i == (int) choiceButtons.size() - 1 ? r.getRight() - (r.getX() + i * buttonWidth) : buttonWidth,
                                                  juce::jmin (22, r.getHeight() - 2));
    }
    else if (combo != nullptr)
        combo->setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, juce::jmin (22, r.getHeight())));
    else if (toggle != nullptr)
        toggle->setBounds (r.withSizeKeepingCentre (20, juce::jmin (20, r.getHeight())));
}

void ParamControl::timerCallback()
{
    if (combo != nullptr)
    {
        const int selected = combo->getSelectedId() - 1;
        for (int i = 0; i < (int) choiceButtons.size(); ++i)
            choiceButtons[(size_t) i]->setToggleState (i == selected, juce::dontSendNotification);
    }
    const int assigned = getAssignedMidiCC != nullptr ? getAssignedMidiCC() : -1;
    if (getCaptionText != nullptr && ! caption.isBeingEdited())
        captionText = getCaptionText();
    if (! caption.isBeingEdited())
        caption.setText (captionText + (assigned >= 0 ? " [CC " + juce::String (assigned) + "]" : ""),
                         juce::dontSendNotification);
}

//==============================================================================
ModSource ModulationHost::sourceFromDrag (const juce::var& description)
{
    const auto text = description.toString();
    if (! text.startsWith ("modsrc:"))
        return ModSource::count;
    const int index = text.fromFirstOccurrenceOf (":", false, false).getIntValue();
    return index >= 0 && index < (int) ModSource::count ? (ModSource) index : ModSource::count;
}

ModSourceChip::ModSourceChip (ModSource s, juce::Colour a)
    : source (s), accent (a), label (ModMatrix::getSourceShortName (s))
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip (juce::String (ModMatrix::getSourceName (s)) + ": drag onto a knob to modulate it");
}

int ModSourceChip::getPreferredWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (10.5f)).boldened(), label) + 22;
}

void ModSourceChip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool hot = isMouseOver() || dragging;
    g.setColour (inUse ? kModAccent.withAlpha (hot ? 0.35f : 0.22f) : kBackground.withAlpha (hot ? 1.0f : 0.7f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (inUse ? kModAccent : hot ? kTextDim : kOutline);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    // Grip dots: this is something to drag.
    g.setColour (inUse ? kModAccent : kTextDim);
    for (int i = 0; i < 3; ++i)
        g.fillEllipse (r.getX() + 7.0f, r.getCentreY() - 4.5f + 3.5f * (float) i, 2.2f, 2.2f);
    g.setColour (inUse || hot ? kText : kTextDim);
    g.setFont (juce::Font (juce::FontOptions (10.5f)).boldened());
    g.drawText (label, r.withTrimmedLeft (13.0f), juce::Justification::centred);
}

void ModSourceChip::mouseDown (const juce::MouseEvent&)
{
    dragging = false;
}

void ModSourceChip::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging || event.getDistanceFromDragStart() < 4)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        container->startDragging (ModulationHost::dragDescription (source), this);
        repaint();
    }
}

void ModSourceChip::mouseUp (const juce::MouseEvent& event)
{
    if (! dragging && ! event.mouseWasDraggedSinceMouseDown() && onClick != nullptr)
        onClick();
    dragging = false;
    repaint();
}

//==============================================================================
SectionPanel::SectionPanel (juce::String t, juce::Colour a, int cols) : title (std::move (t)), accent (a), columns (juce::jmax (1, cols)) {}

void SectionPanel::add (std::unique_ptr<ParamControl> control, bool sharesPreviousCell)
{
    addAndMakeVisible (*control);
    if (sharesPreviousCell && ! cells.empty())
        cells.back().push_back (control.get());
    else
        cells.push_back ({ control.get() });
    controls.push_back (std::move (control));
}

void SectionPanel::addHeaderComponent (std::unique_ptr<juce::Component> component)
{
    addAndMakeVisible (*component);
    headerComponents.push_back (std::move (component));
}

void SectionPanel::paintFrame (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour accent, const juce::String& title)
{
    auto r = bounds.toFloat().reduced (0.5f);
    g.setColour (kPanel);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (kOutline);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (accent);
    g.fillRect (r.removeFromTop (2.5f).reduced (5.0f, 0.0f));
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
    g.drawText (title.toUpperCase(), bounds.reduced (8, 3).removeFromTop (15), juce::Justification::centredLeft);
}

void SectionPanel::paint (juce::Graphics& g)
{
    paintFrame (g, getLocalBounds(), accent, title);
}

void SectionPanel::resized()
{
    auto area = getLocalBounds().reduced (4, 3);
    auto header = area.removeFromTop (kTitleHeight).withTrimmedTop (1).withTrimmedRight (4);
    for (auto it = headerComponents.rbegin(); it != headerComponents.rend(); ++it)
    {
        auto* chip = dynamic_cast<ModSourceChip*> (it->get());
        (*it)->setBounds (header.removeFromRight (chip != nullptr ? chip->getPreferredWidth() : 60));
        header.removeFromRight (4);
    }
    const int n = (int) cells.size();
    const int rows = juce::jmax (1, (n + columns - 1) / columns);
    const int cw = area.getWidth() / columns, rh = area.getHeight() / rows;
    for (int i = 0; i < n; ++i)
        for (auto* control : cells[(size_t) i])
            control->setBounds (area.getX() + (i % columns) * cw, area.getY() + (i / columns) * rh, cw, rh);
}

//==============================================================================
WaveSlotGrid::WaveSlotGrid (SynthEngine& e)
    : engine (e)
{
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<int> WaveSlotGrid::tileBounds (int slot) const noexcept
{
    const int rows = (WaveBank::kNumAltWaves + kColumns - 1) / kColumns;
    const float w = (float) getWidth() / (float) kColumns, h = (float) getHeight() / (float) rows;
    return juce::Rectangle<float> ((float) (slot % kColumns) * w, (float) (slot / kColumns) * h, w, h).toNearestInt().reduced (3);
}

void WaveSlotGrid::paint (juce::Graphics& g)
{
    const auto& bank = engine.getWaveBank();
    const auto& names = WaveBank::altWaveNames();
    const auto mouse = isMouseOver() ? getMouseXYRelative() : juce::Point<int> (-100, -100);
    for (int slot = 0; slot < WaveBank::kNumAltWaves; ++slot)
    {
        const auto r = tileBounds (slot);
        const bool isSelected = slot == selected, hot = r.contains (mouse);
        g.setColour (isSelected ? kPerfAccent.withAlpha (0.14f) : kBackground.withAlpha (hot ? 0.9f : 0.5f));
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (isSelected ? kPerfAccent : hot ? kTextDim : kOutline);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, isSelected ? 1.5f : 1.0f);

        auto inner = r.reduced (9, 6);
        auto top = inner.removeFromTop (16);
        g.setFont (juce::Font (juce::FontOptions (11.0f)).boldened());
        g.setColour (isSelected ? kPerfAccent : kTextDim);
        g.drawText (juce::String (slot + 1).paddedLeft ('0', 2), top.removeFromLeft (22), juce::Justification::centredLeft);
        g.setFont (juce::Font (juce::FontOptions (12.5f)).withStyle (isSelected ? juce::Font::bold : juce::Font::plain));
        g.setColour (kText);
        g.drawFittedText (names[slot], top, juce::Justification::centredLeft, 1, 0.8f);
        paintCycle (g, bank.altCycle (slot), inner.toFloat().reduced (0.0f, 4.0f),
                    isSelected ? kPerfAccent : kTextDim, 1.5f, 128);
    }
}

void WaveSlotGrid::mouseDown (const juce::MouseEvent& event)
{
    for (int slot = 0; slot < WaveBank::kNumAltWaves; ++slot)
        if (tileBounds (slot).contains (event.getPosition()))
        {
            selected = slot;
            repaint();
            if (onSelect != nullptr)
                onSelect (slot);
            return;
        }
}

//==============================================================================
WaveBrowserPage::WaveBrowserPage (SynthEngine& e)
    : engine (e), slots (e)
{
    setOpaque (true);
    slots.onSelect = [this] (int) { repaint(); };
    status.setText ("Pick a slot, then import one waveform cycle (.ws6, .wav, .aif) into it or export it as .ws6.",
                    juce::dontSendNotification);
    status.setJustificationType (juce::Justification::topLeft);
    status.setColour (juce::Label::textColourId, kTextDim);
    status.setFont (juce::Font (juce::FontOptions (12.5f)));
    importButton.setTooltip ("Replace the selected slot with one cycle from a .ws6, .wav or .aif file");
    exportButton.setTooltip ("Save the selected slot as a .ws6 file");
    for (juce::Component* component : std::array<juce::Component*, 5> {
             &slots, &importButton, &exportButton, &closeButton, &status })
        addAndMakeVisible (*component);

    importButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Import alternative waveform", juce::File(),
                                                       "*.ws6;*.wav;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            juce::String error;
            if (! engine.requestWaveImport (slots.getSelected(), file, &error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Wave import failed", error);
            else
                status.setText ("Building mip tables off the audio thread...", juce::dontSendNotification);
        });
    };
    exportButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Export alternative waveform", juce::File(),
                                                       "*.ws6");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            juce::String error;
            if (! engine.exportWave (slots.getSelected(), file, &error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Wave export failed", error);
        });
    };
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    startTimerHz (5);
}

void WaveBrowserPage::timerCallback()
{
    switch (engine.getWaveImportState())
    {
        case SynthEngine::WaveImportState::idle: break;
        case SynthEngine::WaveImportState::building:
            status.setText ("Building mip tables off the audio thread...", juce::dontSendNotification);
            break;
        case SynthEngine::WaveImportState::ready:
            status.setText ("Import complete. The new table is active.", juce::dontSendNotification);
            repaint();
            break;
        case SynthEngine::WaveImportState::failed:
            status.setText ("Import failed: " + engine.getWaveImportError(), juce::dontSendNotification);
            break;
    }
}

void WaveBrowserPage::paint (juce::Graphics& g)
{
    paintPageFrame (g, getLocalBounds(), kPerfAccent);
    auto header = pageContent (getLocalBounds()).removeFromTop (kPageHeader);
    paintPageTitle (g, header, "WAVE BANK", juce::String (WaveBank::kNumAltWaves) + " SLOTS", kPerfAccent);
    g.setColour (kTextDim);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText ("Alternative waveforms for DDS1 (Alt Wave A / B), shared by both layers",
                header.withTrimmedLeft (kPageTitleWidth), juce::Justification::centredRight);

    if (preview.isEmpty())
        return;
    const int slot = slots.getSelected();
    auto r = preview;
    auto nameRow = r.removeFromTop (30);
    g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
    g.setColour (kPerfAccent);
    g.drawText ("SLOT " + juce::String (slot + 1).paddedLeft ('0', 2), nameRow.removeFromLeft (64), juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions (20.0f)).boldened());
    g.setColour (kText);
    g.drawFittedText (WaveBank::altWaveNames()[slot], nameRow, juce::Justification::centredLeft, 1, 0.8f);

    r.removeFromTop (8);
    const auto box = r.toFloat();
    g.setColour (kBackground);
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (kOutline);
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
    paintCycle (g, engine.getWaveBank().altCycle (slot), box.reduced (18.0f, 24.0f), kPerfAccent, 2.5f, 512);
}

void WaveBrowserPage::resized()
{
    auto area = pageContent (getLocalBounds());
    area.removeFromTop (kPageHeader + 12);
    closeButton.setBounds (area.removeFromBottom (kPageFooter).withSizeKeepingCentre (140, 32));
    area.removeFromBottom (8);

    slots.setBounds (area.removeFromLeft (area.getWidth() * 3 / 5));
    area.removeFromLeft (24);
    preview = area.removeFromTop (30 + 8 + 340);
    area.removeFromTop (16);
    auto buttons = area.removeFromTop (32);
    importButton.setBounds (buttons.removeFromLeft (150));
    buttons.removeFromLeft (8);
    exportButton.setBounds (buttons.removeFromLeft (170));
    area.removeFromTop (10);
    status.setBounds (area.removeFromTop (44));
}

//==============================================================================
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

//==============================================================================
NSynthAudioProcessorEditor::NSynthAudioProcessorEditor (NSynthAudioProcessor& p)
    : AudioProcessorEditor (p), synth (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.setBounds (0, 0, kDesignWidth, kDesignHeight);

    title.setText ("nSynth", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (24.0f)).boldened());
    content.addAndMakeVisible (title);

    content.addAndMakeVisible (presetDisplay);
    presetDisplay.setTooltip ("Click to browse patches and performances");
    presetDisplay.onClick = [this]
    {
        if (presetBrowser->isOpen())
            presetBrowser->close();
        else
            openPresetBrowser (presetBrowser->getKind());
    };
    for (auto* b : { &prevPreset, &nextPreset, &savePreset, &upperSelect, &lowerSelect, &initLayer, &copyUpToLow, &copyLowToUp,
                     &loadPatch, &savePatch, &comparePatch, &synthTab, &modTab, &fxTab, &waveBrowserButton })
        content.addAndMakeVisible (*b);

    prevPreset.setTooltip ("Previous preset in the browser list");
    nextPreset.setTooltip ("Next preset in the browser list");
    prevPreset.onClick = [this] { presetBrowser->step (-1); };
    nextPreset.onClick = [this] { presetBrowser->step (1); };
    savePreset.setTooltip ("Save the shown patch or performance to the user folder");
    savePreset.onClick = [this] { savePresetAs (presetBrowser->getKind()); };
    loadPatch.setButtonText ("BROWSE");
    loadPatch.setTooltip ("Browse patches for the selected layer");
    loadPatch.onClick = [this] { openPresetBrowser (nSynth::PresetKind::patch); };
    savePatch.onClick = [this] { savePresetAs (nSynth::PresetKind::patch); };
    comparePatch.setClickingTogglesState (true);
    comparePatch.onClick = [this]
    {
        if (! synth.setPatchCompare (bottomLayer, comparePatch.getToggleState()))
            comparePatch.setToggleState (synth.isPatchCompareActive (bottomLayer), juce::dontSendNotification);
    };
    waveBrowserButton.setTooltip ("The 32 alternative waveforms of DDS1: import and export");
    waveBrowserButton.onClick = [this] { waveBrowserPage->setVisible (true); };

    // Page tabs: one segmented control.
    synthTab.setTooltip ("Oscillators, filters, envelopes, LFOs, voicing and macros");
    modTab.setTooltip ("MSEGs and the modulation slots");
    fxTab.setTooltip ("The effect chain");
    for (auto [button, target] : { std::pair { &synthTab, Page::synth }, std::pair { &modTab, Page::mod }, std::pair { &fxTab, Page::fx } })
    {
        button->setRadioGroupId (0x5a6e);
        button->setClickingTogglesState (true);
        button->setColour (juce::TextButton::buttonOnColourId, kOutline.brighter (0.25f));
        button->onClick = [this, target = target] { showPage (target); };
    }
    synthTab.setConnectedEdges (juce::Button::ConnectedOnRight);
    modTab.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    fxTab.setConnectedEdges (juce::Button::ConnectedOnLeft);

    upperSelect.setClickingTogglesState (false);
    upperSelect.onClick = [this] { selectBottomLayer (Layer::upper); };
    lowerSelect.onClick = [this] { selectBottomLayer (Layer::lower); };
    initLayer.onClick = [this] { synth.initLayer (bottomLayer); };
    copyUpToLow.onClick = [this] { synth.copyLayer (Layer::upper, Layer::lower); };
    copyLowToUp.onClick = [this] { synth.copyLayer (Layer::lower, Layer::upper); };

    voiceMeter.setJustificationType (juce::Justification::centredRight);
    voiceMeter.setColour (juce::Label::textColourId, kTextDim);
    content.addAndMakeVisible (voiceMeter);

    // SYNTH page: three rows in signal-flow order (column counts set the section widths).
    const std::vector<std::pair<Section, int>> rowLayout[3] = {
        { { Section::dds1, 2 }, { Section::dds2, 2 }, { Section::mixer, 1 }, { Section::ddsMod, 3 }, { Section::vcf, 4 }, { Section::vca, 2 } },
        { { Section::env1, 4 }, { Section::env2, 3 }, { Section::lfo1, 3 }, { Section::perfControl, 5 } },
        { { Section::voiceAssign, 5 }, { Section::filter2, 5 }, { Section::macros, 4 } } };

    const SwitchedCellRegistrar registerSwitched = [this] (ParamControl* whenOff, ParamControl* whenOn, const char* selector)
    {
        switchedCells.push_back ({ whenOff, whenOn, synth.getState().getRawParameterValue (layerParamId (whenOff->getLayer(), selector)) });
    };
    const ControlFactory factory = [this] (Layer layer, const char* suffix) { return makeControl (layer, suffix); };

    for (auto layer : { Layer::upper, Layer::lower })
    {
        auto& ui = layers[(size_t) toInt (layer)];
        const auto accent = accentForLayer (layer);
        for (size_t row = 0; row < 3; ++row)
            for (auto [section, cols] : rowLayout[row])
            {
                ui.rows[row].push_back (makeSection (layer, section, cols, accent));
                content.addChildComponent (*ui.rows[row].back());
            }
        ui.modPage = std::make_unique<ModPage> (synth.getEngine(), layer, accent, factory, registerSwitched);
        content.addChildComponent (*ui.modPage);
        ui.fxPage = std::make_unique<FxPage> (synth.getState(), synth.getEngine(), layer, accent, factory, registerSwitched);
        content.addChildComponent (*ui.fxPage);
    }

    upperSelect.setTooltip ("Edit the UPPER layer (the patch name is shown on the tab)");
    lowerSelect.setTooltip ("Edit the LOWER layer (used by Dual and Split keyboard modes)");
    upperSelect.setColour (juce::TextButton::buttonOnColourId, kUpperAccent.darker (0.45f));
    lowerSelect.setColour (juce::TextButton::buttonOnColourId, kLowerAccent.darker (0.45f));
    initLayer.setTooltip ("Reset the selected layer to the init patch");
    comparePatch.setTooltip ("Toggle between the edited and the saved version of the patch");
    copyUpToLow.setTooltip ("Copy the UPPER layer into LOWER");
    copyLowToUp.setTooltip ("Copy the LOWER layer into UPPER");
    savePatch.setTooltip ("Save the selected layer as a patch in the user folder");
    layerHint.setColour (juce::Label::textColourId, kTextDim);
    layerHint.setFont (juce::Font (juce::FontOptions (11.0f)));
    layerHint.setJustificationType (juce::Justification::centredLeft);
    content.addAndMakeVisible (layerHint);

    // Performance settings and the live controllers (ribbon, mod lever, expression) share one strip.
    performancePanel = std::make_unique<SectionPanel> ("Performance / Controllers", kPerfAccent,
                                                       (int) getPerformanceParamSpecs().size());
    for (auto& spec : getPerformanceParamSpecs())
    {
        auto control = std::make_unique<ParamControl> (synth.getState(), perfParamId (spec.suffix), spec, kPerfAccent);
        configureMidiLearn (*control);
        performancePanel->add (std::move (control));
    }
    content.addAndMakeVisible (*performancePanel);
    waveBrowserPage = std::make_unique<WaveBrowserPage> (synth.getEngine());
    waveBrowserPage->onClose = [this] { waveBrowserPage->setVisible (false); };
    content.addAndMakeVisible (*waveBrowserPage);
    waveBrowserPage->setVisible (false);
    presetBrowser = std::make_unique<PresetBrowser> (synth);
    presetBrowser->onTargetLayerChanged = [this] (Layer layer) { selectBottomLayer (layer); };
    presetBrowser->onSaveRequested = [this] (nSynth::PresetKind kind) { savePresetAs (kind); };
    presetBrowser->onClosed = [this] { refreshDisplay(); };
    presetBrowser->onPresetLoaded = [this] { refreshDisplay(); };
    content.addChildComponent (*presetBrowser);

    for (int pct : { 50, 75, 100, 125, 150, 200 })
        sizeBox.addItem (juce::String (pct) + " %", pct);
    sizeBox.onChange = [this] { if (sizeBox.getSelectedId() > 0) setScale ((float) sizeBox.getSelectedId() / 100.0f); };
    content.addAndMakeVisible (sizeBox);
    // The full-size page covers the header, including the size box added after it.
    waveBrowserPage->toFront (false);

    synthTab.setToggleState (true, juce::dontSendNotification);
    selectBottomLayer (Layer::upper);
    updateSwitchedControls();
    layoutContent();

    // Read before setResizeLimits(): that call resizes the editor and resized() stores the scale.
    const float savedScale = synth.getSavedEditorScale();

    // Uniform scaling with a fixed aspect ratio keeps knobs round at every size.
    setResizable (true, true);
    setResizeLimits (kDesignWidth / 2, kDesignHeight / 2, kDesignWidth * 2, kDesignHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kDesignWidth / (double) kDesignHeight);
    setScale (savedScale);

    refreshDisplay();
    startTimerHz (30);
}

NSynthAudioProcessorEditor::~NSynthAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

std::unique_ptr<ParamControl> NSynthAudioProcessorEditor::makeControl (Layer layer, const char* suffix)
{
    const int index = findLayerSpecIndex (suffix);
    jassert (index >= 0);
    const auto& spec = getLayerParamSpecs()[(size_t) index];
    auto control = std::make_unique<ParamControl> (synth.getState(), layerParamId (layer, spec.suffix), spec, accentForLayer (layer));
    configureMidiLearn (*control);
    control->setModulationTarget (this, layer, index);
    layers[(size_t) toInt (layer)].controls.push_back (control.get());
    return control;
}

std::unique_ptr<SectionPanel> NSynthAudioProcessorEditor::makeSection (Layer layer, Section section, int columns, juce::Colour accent)
{
    // Two controls in one cell; the selector parameter (on = value > 0.5) decides which one is shown.
    struct SharedCell { const char* first; const char* second; const char* selector; bool secondShownWhenOn; };
    static constexpr SharedCell sharedCells[] = {
        { pid::velFixed, pid::velCurve, pid::velMode, false },      // Static: fixed velocity, Dynamic: curve
    };

    auto title = sectionName (section);
    if (section == Section::perfControl) title = "LFO 2 / Bend / Glide";
    if (section == Section::ddsMod)      title = "Osc Modulation";
    if (section == Section::vcf)         title = "Filter";
    if (section == Section::vca)         title = "Amp";
    auto panel = std::make_unique<SectionPanel> (title, accent, columns);
    ParamControl* previous = nullptr;
    for (auto& spec : getLayerParamSpecs())
        if (spec.section == section)
        {
            auto control = makeControl (layer, spec.suffix);
            bool shares = false;
            for (const auto& cell : sharedCells)
                if (juce::String (spec.suffix) == cell.second && previous != nullptr
                    && previous->getParamId() == layerParamId (layer, cell.first))
                {
                    shares = true;
                    auto* condition = synth.getState().getRawParameterValue (layerParamId (layer, cell.selector));
                    if (cell.secondShownWhenOn)
                        switchedCells.push_back ({ previous, control.get(), condition });
                    else
                        switchedCells.push_back ({ control.get(), previous, condition });
                }
            previous = control.get();
            panel->add (std::move (control), shares);
        }

    // Modulation sources of the page: drag their chips onto any knob.
    auto addChip = [&] (ModSource source)
    {
        auto chip = std::make_unique<ModSourceChip> (source, accent);
        panel->addHeaderComponent (std::move (chip));
    };
    if (section == Section::env1)        addChip (ModSource::env1);
    if (section == Section::env2)        addChip (ModSource::env2);
    if (section == Section::lfo1)        addChip (ModSource::lfo1);
    if (section == Section::perfControl) addChip (ModSource::lfo2);
    if (section == Section::macros)
    {
        for (int m = 0; m < ModMatrixData::kMacros; ++m)
            addChip ((ModSource) ((int) ModSource::macro1 + m));
        // Macro knobs show their (editable) names.
        int m = 0;
        for (auto& control : panel->getControls())
        {
            control->setEditableCaption ([this, layer, m] { return synth.getEngine().modMatrix (layer).getMacroName (m); },
                                         [this, layer, m] (const juce::String& name) { synth.getEngine().modMatrix (layer).setMacroName (m, name); });
            ++m;
        }
    }
    return panel;
}

void NSynthAudioProcessorEditor::configureMidiLearn (ParamControl& control)
{
    const auto id = control.getParamId();
    control.setMidiLearnHandlers (
        [this, id] { return synth.getEngine().requestMidiLearn (id); },
        [this, id] { synth.getEngine().clearMidiLearn (id); },
        [this, id] { return synth.getEngine().getMidiLearnCC (id); });
}

void NSynthAudioProcessorEditor::showPage (Page newPage)
{
    page = newPage;
    synthTab.setToggleState (page == Page::synth, juce::dontSendNotification);
    modTab.setToggleState (page == Page::mod, juce::dontSendNotification);
    fxTab.setToggleState (page == Page::fx, juce::dontSendNotification);
    updatePageVisibility();
}

void NSynthAudioProcessorEditor::updatePageVisibility()
{
    for (auto l : { Layer::upper, Layer::lower })
    {
        auto& ui = layers[(size_t) toInt (l)];
        const bool shown = l == bottomLayer;
        for (auto& row : ui.rows)
            for (auto& s : row)
                s->setVisible (shown && page == Page::synth);
        ui.modPage->setVisible (shown && page == Page::mod);
        ui.fxPage->setVisible (shown && page == Page::fx);
    }
    updateModulationDisplay();
}

void NSynthAudioProcessorEditor::selectBottomLayer (Layer layer)
{
    bottomLayer = layer;
    if (presetBrowser != nullptr && presetBrowser->getTargetLayer() != layer)
        presetBrowser->setTargetLayer (layer);
    updatePageVisibility();
    upperSelect.setToggleState (layer == Layer::upper, juce::dontSendNotification);
    lowerSelect.setToggleState (layer == Layer::lower, juce::dontSendNotification);
}

//==============================================================================
bool NSynthAudioProcessorEditor::canModulate (Layer, int parameterIndex) const
{
    return ModMatrix::isModulatable (parameterIndex);
}

void NSynthAudioProcessorEditor::addModulationMenuItems (juce::PopupMenu& menu, Layer layer, int parameterIndex)
{
    // ids: base + source = add; base + 100 + slot = remove; base + 200 = remove all; base + 300 = MOD page.
    auto& matrix = synth.getEngine().modMatrix (layer);
    juce::PopupMenu add;
    for (int source = 0; source < (int) ModSource::count; ++source)
    {
        const auto name = source >= (int) ModSource::macro1 ? matrix.getMacroName (source - (int) ModSource::macro1)
                                                            : juce::String (ModMatrix::getSourceName ((ModSource) source));
        add.addItem (kModulationMenuBase + source, name);
    }
    menu.addSubMenu ("Modulate with", add);

    int routes = 0;
    for (int i = 0; i < ModMatrixData::kMaxSlots; ++i)
    {
        const auto slot = matrix.getSlot (i);
        if (slot.destination != parameterIndex || slot.source < 0)
            continue;
        const auto sourceName = slot.source >= (int) ModSource::macro1 ? matrix.getMacroName (slot.source - (int) ModSource::macro1)
                                                                        : juce::String (ModMatrix::getSourceName ((ModSource) slot.source));
        menu.addItem (kModulationMenuBase + 100 + i, "Remove " + sourceName + " ("
                      + (slot.amount > 0.0f ? "+" : "") + juce::String (juce::roundToInt (slot.amount * 100.0f)) + " %)");
        ++routes;
    }
    if (routes > 1)
        menu.addItem (kModulationMenuBase + 200, "Remove all modulation");
    menu.addItem (kModulationMenuBase + 300, "Open the MOD page");
}

void NSynthAudioProcessorEditor::handleModulationMenu (int result, Layer layer, int parameterIndex)
{
    auto& matrix = synth.getEngine().modMatrix (layer);
    const int id = result - kModulationMenuBase;
    if (id >= 0 && id < (int) ModSource::count)
        dropModulationSource ((ModSource) id, layer, parameterIndex);
    else if (id >= 100 && id < 100 + ModMatrixData::kMaxSlots)
        matrix.removeSlot (id - 100);
    else if (id == 200)
        matrix.clearDestination (parameterIndex);
    else if (id == 300)
        showPage (Page::mod);
    updateModulationDisplay();
}

void NSynthAudioProcessorEditor::dropModulationSource (ModSource source, Layer layer, int parameterIndex)
{
    // LFO-like sources swing around the knob; the others push it one way.
    const bool centred = source == ModSource::lfo1 || source == ModSource::lfo2 || source == ModSource::dds2
                      || source == ModSource::perNotePitch || source == ModSource::ribbon;
    auto& matrix = synth.getEngine().modMatrix (layer);
    for (int i = 0; i < ModMatrixData::kMaxSlots; ++i)
        if (matrix.getSlot (i).source == (int) source && matrix.getSlot (i).destination == parameterIndex)
            return;   // already routed: keep its amount
    matrix.addRoute (source, parameterIndex, centred ? 0.25f : 0.35f, centred);
    updateModulationDisplay();
}

void NSynthAudioProcessorEditor::updateModulationDisplay()
{
    auto& layerUi = layers[(size_t) toInt (bottomLayer)];
    const auto& engineLayer = synth.getEngine().layer (bottomLayer);
    const auto data = synth.getEngine().modMatrix (bottomLayer).snapshot();

    // Reachable range per parameter (sum of the slots' swings).
    std::array<float, kLayerParamCount> low {}, high {};
    for (const auto& slot : data->slots)
    {
        if (! slot.isActive())
            continue;
        auto& lo = low[(size_t) slot.destination];
        auto& hi = high[(size_t) slot.destination];
        if (slot.bipolar)
        {
            lo -= std::abs (slot.amount);
            hi += std::abs (slot.amount);
        }
        else if (slot.amount > 0.0f) hi += slot.amount;
        else                         lo += slot.amount;
    }
    for (auto* control : layerUi.controls)
    {
        const int index = control->getParameterIndex();
        if (index < 0)
            continue;
        bool shown = true;   // visible on the current page (isShowing() also needs a window)
        for (const juce::Component* c = control; c != nullptr && c != &content && shown; c = c->getParentComponent())
            shown = c->isVisible();
        if (! shown)
            continue;
        const bool modulated = data->modulated[(size_t) index];
        control->setModulationDisplay (modulated, low[(size_t) index], high[(size_t) index],
                                       modulated ? engineLayer.getModulationOffset (index) : 0.0f);
    }
}

void NSynthAudioProcessorEditor::setScale (float newScale)
{
    newScale = juce::jlimit (0.5f, 2.0f, newScale);
    setSize (juce::roundToInt (kDesignWidth * newScale), juce::roundToInt (kDesignHeight * newScale));
}

void NSynthAudioProcessorEditor::layoutContent()
{
    auto r = juce::Rectangle<int> (0, 0, kDesignWidth, kDesignHeight).reduced (6);

    auto header = r.removeFromTop (40);
    title.setBounds (header.removeFromLeft (100));
    for (auto* tab : { &synthTab, &modTab, &fxTab })
        tab->setBounds (header.removeFromLeft (68).withSizeKeepingCentre (68, 28));
    header.removeFromLeft (12);
    sizeBox.setBounds (header.removeFromRight (84).withSizeKeepingCentre (84, 26));
    header.removeFromRight (8);
    waveBrowserButton.setBounds (header.removeFromRight (94).withSizeKeepingCentre (94, 28));
    header.removeFromRight (8);
    voiceMeter.setBounds (header.removeFromRight (140));
    auto bar = header.withSizeKeepingCentre (juce::jmin (580, header.getWidth() - 16), 30);
    prevPreset.setBounds (bar.removeFromLeft (30));
    savePreset.setBounds (bar.removeFromRight (62));
    bar.removeFromRight (6);
    nextPreset.setBounds (bar.removeFromRight (30));
    presetDisplay.setBounds (bar.reduced (4, 0));
    waveBrowserPage->setBounds (juce::Rectangle<int> (0, 0, kDesignWidth, kDesignHeight));
    presetBrowser->setBounds (juce::Rectangle<int> (0, 0, kDesignWidth, kDesignHeight));
    r.removeFromTop (4);

    performancePanel->setBounds (r.removeFromTop (84));
    r.removeFromTop (6);

    // Layer tabs with the loaded patch names, then the layer actions.
    auto tabs = r.removeFromTop (34);
    upperSelect.setBounds (tabs.removeFromLeft (290));
    tabs.removeFromLeft (4);
    lowerSelect.setBounds (tabs.removeFromLeft (290));
    tabs.removeFromLeft (12);
    for (auto* b : { &savePatch, &loadPatch, &copyLowToUp, &copyUpToLow, &comparePatch, &initLayer })
    {
        b->setBounds (tabs.removeFromRight (b == &savePatch ? 104 : 92).reduced (0, 3));
        tabs.removeFromRight (5);
    }
    layerHint.setBounds (tabs);
    r.removeFromTop (5);

    auto layoutRow = [] (juce::Rectangle<int> area, std::vector<SectionPanel*> sections)
    {
        int totalCols = 0;
        for (auto* s : sections)
            totalCols += s->getColumns();
        const float colW = (float) area.getWidth() / (float) totalCols;
        float x = (float) area.getX();
        for (auto* s : sections)
        {
            const float w = colW * (float) s->getColumns();
            s->setBounds (juce::Rectangle<float> (x, (float) area.getY(), w, (float) area.getHeight()).toNearestInt().reduced (2));
            x += w;
        }
    };

    const int rowHeights[] = { 222, 184, 0 };
    for (auto layer : { Layer::upper, Layer::lower })
    {
        auto area = r;
        auto& ui = layers[(size_t) toInt (layer)];
        ui.modPage->setBounds (r);
        ui.fxPage->setBounds (r);
        for (size_t row = 0; row < 3; ++row)
        {
            auto rowArea = row < 2 ? area.removeFromTop (rowHeights[row]) : area;
            area.removeFromTop (row < 2 ? 4 : 0);
            std::vector<SectionPanel*> sections;
            for (auto& s : ui.rows[row])
                sections.push_back (s.get());
            layoutRow (rowArea, sections);
        }
    }
}

void NSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
}

void NSynthAudioProcessorEditor::resized()
{
    scale = (float) getWidth() / (float) kDesignWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    synth.setSavedEditorScale (scale);

    const int pct = juce::roundToInt (scale * 100.0f);
    sizeBox.setSelectedId (sizeBox.indexOfItemId (pct) >= 0 ? pct : 0, juce::dontSendNotification);
}

void NSynthAudioProcessorEditor::openPresetBrowser (nSynth::PresetKind kind)
{
    waveBrowserPage->setVisible (false);
    presetBrowser->open (kind, bottomLayer);
}

void NSynthAudioProcessorEditor::savePresetAs (nSynth::PresetKind kind)
{
    const bool patch = kind == nSynth::PresetKind::patch;
    const auto directory = synth.getPresetLibrary().getUserDirectory (kind);
    directory.createDirectory();
    const auto suggested = patch ? synth.getPatchName (bottomLayer) : synth.getCurrentPresetName();
    chooser = std::make_unique<juce::FileChooser> (patch ? "Save layer patch" : "Save performance",
                                                   directory.getChildFile (juce::File::createLegalFileName (suggested)),
                                                   patch ? "*.nsynthpatch" : "*.nsynthperf");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, patch] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        juce::String error;
        const bool ok = patch ? synth.savePatch (bottomLayer, file, &error)
                              : synth.savePerformance (file.withFileExtension ("nsynthperf"), &error);
        if (! ok)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save failed", error);
        if (presetBrowser->isOpen())
            presetBrowser->refresh();
        refreshDisplay();
    });
}

void NSynthAudioProcessorEditor::refreshDisplay() noexcept
{
    const auto patchDirty = synth.isPatchDirty (bottomLayer);
    const auto patchName = synth.getPatchName (bottomLayer) + (patchDirty ? " *" : "");
    if (presetBrowser->getKind() == nSynth::PresetKind::patch)
        presetDisplay.setContent (bottomLayer == Layer::upper ? "UPPER" : "LOWER", patchName,
                                  synth.getPatchCategory (bottomLayer), accentForLayer (bottomLayer));
    else
        presetDisplay.setContent ("PERF", synth.getCurrentPresetName(),
                                  "U: " + synth.getPatchName (Layer::upper) + "  L: " + synth.getPatchName (Layer::lower),
                                  kPerfAccent);
    comparePatch.setToggleState (synth.isPatchCompareActive (bottomLayer), juce::dontSendNotification);

    for (auto [button, layer] : { std::pair { &upperSelect, Layer::upper }, std::pair { &lowerSelect, Layer::lower } })
    {
        const auto text = juce::String (layer == Layer::upper ? "UPPER" : "LOWER") + "   " + synth.getPatchName (layer)
                        + (synth.isPatchDirty (layer) ? " *" : "");
        if (button->getButtonText() != text)
            button->setButtonText (text);
    }
    auto& state = synth.getState();
    const int keyboardMode = (int) state.getRawParameterValue (perfParamId (pid::kbdMode))->load();
    const int singleLayer = (int) state.getRawParameterValue (perfParamId (pid::singleLayer))->load();
    juce::String hint;
    if (keyboardMode == toInt (KeyboardMode::single))
        hint = juce::String ("Single: only ") + (singleLayer == 0 ? "UPPER" : "LOWER") + " plays";
    else if (keyboardMode == toInt (KeyboardMode::dual))
        hint = "Dual: both layers play";
    else
        hint = "Split at " + juce::MidiMessage::getMidiNoteName ((int) state.getRawParameterValue (perfParamId (pid::splitPoint))->load(), true, true, 4)
             + ": LOWER below, UPPER above";
    layerHint.setText (hint, juce::dontSendNotification);
    voiceMeter.setText ("Voices  U " + juce::String (synth.getActiveVoices (Layer::upper))
                        + "  /  L " + juce::String (synth.getActiveVoices (Layer::lower)),
                        juce::dontSendNotification);
}

void NSynthAudioProcessorEditor::updateSwitchedControls()
{
    for (const auto& cell : switchedCells)
    {
        const bool on = cell.condition != nullptr && cell.condition->load() > 0.5f;
        cell.whenOn->setVisible (on);
        cell.whenOff->setVisible (! on);
    }
}

void NSynthAudioProcessorEditor::timerCallback()
{
    // Modulation rings at the full timer rate, everything else at 10 Hz.
    updateModulationDisplay();
    if (++timerTicks % 3 != 0)
        return;
    updateSwitchedControls();
    const auto dropped = synth.getEngine().getDroppedMidiParameterChanges();
    if (dropped > lastMidiParameterDrops)
    {
        lastMidiParameterDrops = dropped;
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "MIDI control overflow",
            "Some MIDI-mapped parameter updates were dropped because the control queue was full.");
    }
    refreshDisplay();
}
