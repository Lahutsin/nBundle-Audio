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
    for (auto prefix : { "Chorus ", "Delay ", "Reverb " })
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

void paintClearButton (juce::Graphics& g, juce::Rectangle<int> r, bool hot)
{
    const auto c = r.toFloat();
    g.setColour (hot ? kNegative : kOutline);
    g.fillEllipse (c);
    const auto x = c.reduced (c.getWidth() * 0.32f);
    g.setColour (kText);
    g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, 1.5f);
    g.drawLine ({ x.getBottomLeft(), x.getTopRight() }, 1.5f);
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
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (1.8f, radius * 0.17f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = start + pos * (end - start);
    const auto accent = accentFor (s);

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
    });
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
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
    caption.setText (captionText + (assigned >= 0 ? " [CC " + juce::String (assigned) + "]" : ""),
                     juce::dontSendNotification);
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

void SectionPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (kPanel);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (kOutline);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (accent);
    g.fillRect (r.removeFromTop (2.5f).reduced (5.0f, 0.0f));
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
    g.drawText (title.toUpperCase(), getLocalBounds().reduced (8, 3).removeFromTop (15), juce::Justification::centredLeft);
}

void SectionPanel::resized()
{
    auto area = getLocalBounds().reduced (4, 3);
    area.removeFromTop (17);
    const int n = (int) cells.size();
    const int rows = juce::jmax (1, (n + columns - 1) / columns);
    const int cw = area.getWidth() / columns, rh = area.getHeight() / rows;
    for (int i = 0; i < n; ++i)
        for (auto* control : cells[(size_t) i])
            control->setBounds (area.getX() + (i % columns) * cw, area.getY() + (i / columns) * rh, cw, rh);
}

//==============================================================================
ModAmountCell::ModAmountCell()
    : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
{
    setRange (-1.0, 1.0, 0.01);
    setDoubleClickReturnValue (true, 0.0);
    setMouseDragSensitivity (160);
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void ModAmountCell::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    if (! isEnabled())
    {
        g.setColour (kPanel.darker (0.25f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (kOutline);
        g.fillRect (juce::Rectangle<float> (juce::jmin (14.0f, r.getWidth() * 0.4f), 1.5f).withCentre (r.getCentre()));
        return;
    }

    g.setColour (kBackground);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (kOutline);
    g.fillRect (juce::Rectangle<float> (1.0f, r.getHeight() * 0.5f).withCentre (r.getCentre()));

    // The bar grows from the centre: right and green for positive, left and red for negative amounts.
    const float amount = (float) getValue();
    if (amount != 0.0f)
    {
        const auto colour = amount > 0.0f ? kPerfAccent : kNegative;
        const float from = r.getCentreX(), to = from + amount * (r.getWidth() * 0.5f - 3.0f);
        auto bar = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, to), r.getY() + 4.0f,
                                                               juce::jmax (from, to), r.getBottom() - 4.0f);
        g.setColour (colour.withAlpha (0.3f));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (colour);
        g.fillRect (amount > 0.0f ? bar.removeFromRight (2.0f) : bar.removeFromLeft (2.0f));

        g.setColour (kText);
        g.setFont (juce::Font (juce::FontOptions (r.getWidth() < 60.0f ? 10.5f : 12.5f)).boldened());
        g.drawFittedText ((amount > 0.0f ? "+" : "") + juce::String (amount, 2), r.toNearestInt(),
                          juce::Justification::centred, 1, 0.7f);
    }

    g.setColour (isMouseOverOrDragging() ? kText.withAlpha (0.7f) : kOutline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

//==============================================================================
ModMatrixGrid::ModMatrixGrid (ModMatrix& m, bool isDirect)
    : matrix (&m), direct (isDirect)
{
    const int columns = columnCount();
    rowLabels.resize ((size_t) ModMatrixData::sourceCount);
    columnLabels.resize ((size_t) columns);
    activeRows.assign (rowLabels.size(), false);
    activeColumns.assign (columnLabels.size(), false);

    cells.reserve ((size_t) (ModMatrixData::sourceCount * columns));
    for (int source = 0; source < ModMatrixData::sourceCount; ++source)
        for (int target = 0; target < columns; ++target)
        {
            auto cell = std::make_unique<ModAmountCell>();
            const auto route = juce::String (ModMatrix::getSourceName ((ModSource) source)) + " -> " + targetName (target);
            cell->setTooltip (route);
            if (direct && ModMatrix::isPanelDuplicate ((ModSource) source, (DirectModTarget) target))
            {
                cell->setEnabled (false);
                cell->setTooltip (route + " (already a knob on the panel)");
            }
            cell->onValueChange = [this, source, target, control = cell.get()]
            {
                if (! updating && ! setAmountAt (source, target, (float) control->getValue()))
                    control->setValue (amountAt (source, target), juce::dontSendNotification);
            };
            addAndMakeVisible (*cell);
            cells.push_back (std::move (cell));
        }
    startTimerHz (15);
}

int ModMatrixGrid::columnCount() const noexcept
{
    return direct ? ModMatrixData::directTargetCount : ModMatrixData::targetCount;
}

juce::String ModMatrixGrid::targetName (int target) const
{
    return direct ? ModMatrix::getDirectTargetName ((DirectModTarget) target) : ModMatrix::getTargetName ((ModTarget) target);
}

void ModMatrixGrid::setMatrix (ModMatrix& m)
{
    matrix = &m;
    timerCallback();
}

float ModMatrixGrid::amountAt (int source, int target) const noexcept
{
    if (direct)
        return matrix->getDirectAmount ((ModSource) source, (DirectModTarget) target);
    return matrix->getFixedAmount ((ModSource) source, (ModTarget) target);
}

bool ModMatrixGrid::setAmountAt (int source, int target, float amount)
{
    if (direct)
        return matrix->setDirectAmount ((ModSource) source, (DirectModTarget) target, amount);
    matrix->setFixedAmount ((ModSource) source, (ModTarget) target, amount);
    return true;
}

// Clears only this page's cells: ModMatrix::clearSource() would also wipe the other page.
void ModMatrixGrid::clearRow (int source)
{
    for (int target = 0; target < columnCount(); ++target)
        if (amountAt (source, target) != 0.0f)
            setAmountAt (source, target, 0.0f);
    timerCallback();
}

void ModMatrixGrid::clearColumn (int target)
{
    if (direct)
        matrix->clearDirectTarget ((DirectModTarget) target);
    else
        matrix->clearTarget ((ModTarget) target);
    timerCallback();
}

juce::Rectangle<int> ModMatrixGrid::rowClearButton (juce::Rectangle<int> label) noexcept
{
    return label.removeFromRight (22).withSizeKeepingCentre (16, 16);
}

juce::Rectangle<int> ModMatrixGrid::columnClearButton (juce::Rectangle<int> label) noexcept
{
    return label.removeFromBottom (18).withSizeKeepingCentre (16, 16);
}

std::pair<int, int> ModMatrixGrid::clearButtonAt (juce::Point<int> position) const noexcept
{
    for (size_t source = 0; source < rowLabels.size(); ++source)
        if (activeRows[source] && rowClearButton (rowLabels[source]).expanded (3).contains (position))
            return { (int) source, -1 };
    for (size_t target = 0; target < columnLabels.size(); ++target)
        if (activeColumns[target] && columnClearButton (columnLabels[target]).expanded (3).contains (position))
            return { -1, (int) target };
    return { -1, -1 };
}

void ModMatrixGrid::mouseDown (const juce::MouseEvent& event)
{
    const auto [source, target] = clearButtonAt (event.getPosition());
    if (source >= 0)
        clearRow (source);
    else if (target >= 0)
        clearColumn (target);
    else
        return;
    hoveredClear = { -1, -1 };
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ModMatrixGrid::mouseMove (const juce::MouseEvent& event)
{
    const auto hit = clearButtonAt (event.getPosition());
    if (hit == hoveredClear)
        return;
    hoveredClear = hit;
    setMouseCursor (hit.first >= 0 || hit.second >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void ModMatrixGrid::mouseExit (const juce::MouseEvent&)
{
    if (hoveredClear == std::pair<int, int> { -1, -1 })
        return;
    hoveredClear = { -1, -1 };
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void ModMatrixGrid::timerCallback()
{
    const int columns = columnCount();
    std::vector<bool> rows (activeRows.size(), false), targets (activeColumns.size(), false);
    {
        juce::ScopedValueSetter<bool> guard (updating, true);
        for (int source = 0; source < ModMatrixData::sourceCount; ++source)
            for (int target = 0; target < columns; ++target)
            {
                const float amount = amountAt (source, target);
                cells[(size_t) (source * columns + target)]->setValue (amount, juce::dontSendNotification);
                if (amount != 0.0f)
                {
                    rows[(size_t) source] = true;
                    targets[(size_t) target] = true;
                }
            }
    }
    if (rows != activeRows || targets != activeColumns)
    {
        activeRows = std::move (rows);
        activeColumns = std::move (targets);
        repaint();
    }
}

void ModMatrixGrid::paint (juce::Graphics& g)
{
    // Alternate row shading keeps a source readable across the wide direct-mapping grid.
    for (size_t source = 0; source < rowLabels.size(); ++source)
    {
        const auto row = rowLabels[source].withRight (getWidth());
        if (source % 2 == 0)
        {
            g.setColour (kBackground.withAlpha (0.35f));
            g.fillRoundedRectangle (row.toFloat(), 4.0f);
        }
        const bool active = activeRows[source];
        auto label = rowLabels[source].withTrimmedLeft (10);
        if (active)
        {
            const auto button = rowClearButton (rowLabels[source]);
            paintClearButton (g, button, hoveredClear.first == (int) source);
            label.setRight (button.getX() - 4);
        }
        g.setColour (active ? kText : kTextDim);
        g.setFont (juce::Font (juce::FontOptions (13.0f)).withStyle (active ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (ModMatrix::getSourceName ((ModSource) source), label, juce::Justification::centredLeft, 1, 0.8f);
    }

    for (size_t target = 0; target < columnLabels.size(); ++target)
    {
        const bool active = activeColumns[target];
        const auto label = columnLabels[target];
        if (active)
            paintClearButton (g, columnClearButton (label), hoveredClear.second == (int) target);
        g.setColour (active ? kText : kTextDim);
        g.setFont (juce::Font (juce::FontOptions (direct ? 10.5f : 12.5f)).withStyle (active ? juce::Font::bold : juce::Font::plain));
        // Multi-word names wrap instead of squashing, so the headers keep one font size; a single
        // long word (Portamento) may still be narrowed rather than cut off.
        const auto name = targetName ((int) target);
        g.drawFittedText (name, label.withTrimmedBottom (20).reduced (2, 0), juce::Justification::centredBottom, 3,
                          name.containsChar (' ') ? 1.0f : 0.7f);
    }
}

void ModMatrixGrid::resized()
{
    auto area = getLocalBounds();
    const int columns = columnCount();
    const int labelWidth = 160;
    auto header = area.removeFromTop (direct ? 64 : 44);
    header.removeFromLeft (labelWidth);
    const float columnWidth = (float) header.getWidth() / (float) columns;
    const float rowHeight = (float) area.getHeight() / (float) ModMatrixData::sourceCount;

    for (int target = 0; target < columns; ++target)
        columnLabels[(size_t) target] = juce::Rectangle<float> ((float) header.getX() + (float) target * columnWidth, (float) header.getY(),
                                                                columnWidth, (float) header.getHeight()).toNearestInt();

    for (int source = 0; source < ModMatrixData::sourceCount; ++source)
    {
        auto row = juce::Rectangle<float> ((float) area.getX(), (float) area.getY() + (float) source * rowHeight,
                                           (float) area.getWidth(), rowHeight).toNearestInt();
        rowLabels[(size_t) source] = row.removeFromLeft (labelWidth);
        for (int target = 0; target < columns; ++target)
            cells[(size_t) (source * columns + target)]->setBounds (
                juce::Rectangle<float> ((float) row.getX() + (float) target * columnWidth, (float) row.getY(),
                                        columnWidth, (float) row.getHeight()).toNearestInt());
    }
}

//==============================================================================
ModMatrixPage::ModMatrixPage (SynthEngine& e, Layer initialLayer)
    : engine (e), layer (initialLayer),
      matrixGrid (engine.modMatrix (initialLayer), false), directGrid (engine.modMatrix (initialLayer), true)
{
    setOpaque (true);
    addAndMakeVisible (matrixGrid);
    addChildComponent (directGrid);

    for (auto* tab : { &matrixTab, &directTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (1);
        addAndMakeVisible (*tab);
    }
    matrixTab.setTooltip ("Destinations that exist only in the matrix");
    directTab.setTooltip ("Modulate the panel parameters directly");
    matrixTab.onClick = [this] { showDirect (false); };
    directTab.onClick = [this] { showDirect (true); };

    clearAllButton.setTooltip ("Remove every route of this layer (both MATRIX and DIRECT MAPPING)");
    clearAllButton.onClick = [this] { engine.modMatrix (layer).clearAll(); };
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (clearAllButton);
    addAndMakeVisible (closeButton);

    hint.setJustificationType (juce::Justification::centredLeft);
    help.setJustificationType (juce::Justification::centredRight);
    help.setText ("Drag a cell up/down to set it   |   double-click resets   |   x clears a row or column",
                  juce::dontSendNotification);
    for (auto* label : { &hint, &help })
    {
        label->setColour (juce::Label::textColourId, kTextDim);
        label->setFont (juce::Font (juce::FontOptions (12.5f)));
        addAndMakeVisible (*label);
    }

    setLayer (initialLayer);
    showDirect (false);
}

void ModMatrixPage::setLayer (Layer selectedLayer)
{
    layer = selectedLayer;
    matrixGrid.setMatrix (engine.modMatrix (layer));
    directGrid.setMatrix (engine.modMatrix (layer));
    for (auto* tab : { &matrixTab, &directTab })
        tab->setColour (juce::TextButton::buttonOnColourId, accentForLayer (layer).darker (0.45f));
    repaint();
}

void ModMatrixPage::showDirect (bool direct)
{
    matrixTab.setToggleState (! direct, juce::dontSendNotification);
    directTab.setToggleState (direct, juce::dontSendNotification);
    matrixGrid.setVisible (! direct);
    directGrid.setVisible (direct);
    hint.setText (direct ? "Rows: sources. Columns: panel parameters. Greyed cells already have a knob on the panel."
                         : "Rows: sources. Columns: destinations that exist only in the matrix.",
                  juce::dontSendNotification);
}

void ModMatrixPage::paint (juce::Graphics& g)
{
    const auto accent = accentForLayer (layer);
    paintPageFrame (g, getLocalBounds(), accent);
    paintPageTitle (g, pageContent (getLocalBounds()).removeFromTop (kPageHeader), "MOD MATRIX",
                    layer == Layer::upper ? "UPPER" : "LOWER", accent);
}

void ModMatrixPage::resized()
{
    auto area = pageContent (getLocalBounds());
    auto header = area.removeFromTop (kPageHeader);
    clearAllButton.setBounds (header.removeFromRight (110));
    auto tabs = juce::Rectangle<int> (0, 0, 2 * 170 + 4, header.getHeight()).withCentre ({ getLocalBounds().getCentreX(), header.getCentreY() });
    matrixTab.setBounds (tabs.removeFromLeft (170));
    tabs.removeFromLeft (4);
    directTab.setBounds (tabs);

    area.removeFromTop (8);
    auto info = area.removeFromTop (22);
    help.setBounds (info.removeFromRight (580));
    hint.setBounds (info);
    area.removeFromTop (8);

    closeButton.setBounds (area.removeFromBottom (kPageFooter).withSizeKeepingCentre (140, 32));
    area.removeFromBottom (8);
    matrixGrid.setBounds (area);
    directGrid.setBounds (area);
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
                     &loadPatch, &savePatch, &comparePatch, &modMatrixButton, &waveBrowserButton })
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
    modMatrixButton.onClick = [this] { modMatrixPage->setVisible (true); };
    waveBrowserButton.onClick = [this] { waveBrowserPage->setVisible (true); };

    upperSelect.setClickingTogglesState (false);
    upperSelect.onClick = [this] { selectBottomLayer (Layer::upper); };
    lowerSelect.onClick = [this] { selectBottomLayer (Layer::lower); };
    initLayer.onClick = [this] { synth.initLayer (bottomLayer); };
    copyUpToLow.onClick = [this] { synth.copyLayer (Layer::upper, Layer::lower); };
    copyLowToUp.onClick = [this] { synth.copyLayer (Layer::lower, Layer::upper); };

    voiceMeter.setJustificationType (juce::Justification::centredRight);
    voiceMeter.setColour (juce::Label::textColourId, kTextDim);
    content.addAndMakeVisible (voiceMeter);

    // One layer at a time, three rows in signal-flow order (column counts set the section widths).
    const std::vector<std::pair<Section, int>> rowLayout[3] = {
        { { Section::dds1, 2 }, { Section::dds2, 2 }, { Section::mixer, 1 }, { Section::ddsMod, 3 }, { Section::vcf, 4 }, { Section::vca, 2 } },
        { { Section::env1, 4 }, { Section::env2, 3 }, { Section::lfo1, 3 }, { Section::perfControl, 5 } },
        { { Section::voiceAssign, 5 }, { Section::fx, 4 }, { Section::delay, 4 }, { Section::reverb, 4 } } };

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
    modMatrixPage = std::make_unique<ModMatrixPage> (synth.getEngine(), bottomLayer);
    modMatrixPage->onClose = [this] { modMatrixPage->setVisible (false); };
    content.addAndMakeVisible (*modMatrixPage);
    modMatrixPage->setVisible (false);
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
    // The full-size pages cover the header, including the size box added after them.
    modMatrixPage->toFront (false);
    waveBrowserPage->toFront (false);

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
    startTimerHz (10);
}

NSynthAudioProcessorEditor::~NSynthAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

std::unique_ptr<SectionPanel> NSynthAudioProcessorEditor::makeSection (Layer layer, Section section, int columns, juce::Colour accent)
{
    // Two controls in one cell; the selector parameter (on = value > 0.5) decides which one is shown.
    struct SharedCell { const char* first; const char* second; const char* selector; bool secondShownWhenOn; };
    static constexpr SharedCell sharedCells[] = {
        { pid::delayMs, pid::delayDivision, pid::delaySync, true }, // SYNC on: division, off: milliseconds
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
            auto control = std::make_unique<ParamControl> (synth.getState(), layerParamId (layer, spec.suffix), spec, accent);
            configureMidiLearn (*control);
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

void NSynthAudioProcessorEditor::selectBottomLayer (Layer layer)
{
    bottomLayer = layer;
    if (modMatrixPage != nullptr)
        modMatrixPage->setLayer (layer);
    if (presetBrowser != nullptr && presetBrowser->getTargetLayer() != layer)
        presetBrowser->setTargetLayer (layer);
    for (auto l : { Layer::upper, Layer::lower })
        for (auto& row : layers[(size_t) toInt (l)].rows)
            for (auto& s : row)
                s->setVisible (l == layer);
    upperSelect.setToggleState (layer == Layer::upper, juce::dontSendNotification);
    lowerSelect.setToggleState (layer == Layer::lower, juce::dontSendNotification);
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
    title.setBounds (header.removeFromLeft (110));
    modMatrixButton.setBounds (header.removeFromLeft (100).withSizeKeepingCentre (100, 28));
    header.removeFromLeft (6);
    waveBrowserButton.setBounds (header.removeFromLeft (94).withSizeKeepingCentre (94, 28));
    header.removeFromLeft (6);
    sizeBox.setBounds (header.removeFromRight (84).withSizeKeepingCentre (84, 26));
    header.removeFromRight (8);
    voiceMeter.setBounds (header.removeFromRight (190));
    auto bar = header.withSizeKeepingCentre (juce::jmin (600, header.getWidth() - 8), 30);
    prevPreset.setBounds (bar.removeFromLeft (30));
    savePreset.setBounds (bar.removeFromRight (62));
    bar.removeFromRight (6);
    nextPreset.setBounds (bar.removeFromRight (30));
    presetDisplay.setBounds (bar.reduced (4, 0));
    modMatrixPage->setBounds (juce::Rectangle<int> (0, 0, kDesignWidth, kDesignHeight));
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
    modMatrixPage->setVisible (false);
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
