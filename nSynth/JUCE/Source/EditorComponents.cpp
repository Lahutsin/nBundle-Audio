#include "EditorComponents.h"

using namespace nsynth;
using namespace nSynth::ui;

namespace
{
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
} // namespace

//==============================================================================
ParamControl::ParamControl (juce::AudioProcessorValueTreeState& state, const juce::String& id,
                            const ParamSpec& spec, juce::Colour accent, const juce::String& captionOverride)
    : paramId (id),
      parameter (*state.getParameter (id)),
      captionText (captionOverride.isNotEmpty() ? captionOverride : shortCaption (spec.name)),
      baseTooltip (juce::String (spec.name) + (spec.notes[0] != 0 ? " - " + juce::String (spec.notes) : juce::String()))
{
    caption.setText (captionText, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setColour (juce::Label::textColourId, kTextDim);
    caption.setMinimumHorizontalScale (0.55f);
    caption.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (caption);

    setTooltip (baseTooltip);
    addMouseListener (&contextMenuTrigger, true);

    switch (spec.kind)
    {
        case Kind::choiceParam:
            combo = std::make_unique<juce::ComboBox>();
            combo->addItemList (spec.choices, 1);
            combo->getProperties().set ("accent", (juce::int64) accent.getARGB());
            addChildComponent (*combo);
            comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, *combo);
            // Three-way switches, and two-way switches with short labels, are segmented buttons
            // (a radio group: clicking the active one keeps it on).
            if (spec.choices.size() == 3
                || (spec.choices.size() == 2 && spec.choices[0].length() <= 7 && spec.choices[1].length() <= 7))
            {
                for (int i = 0; i < spec.choices.size(); ++i)
                {
                    auto button = std::make_unique<juce::TextButton> (shortChoiceText (spec.choices[i]));
                    button->setTooltip (spec.choices[i]);
                    button->setClickingTogglesState (true);
                    button->setRadioGroupId (kChoiceRadioGroup);
                    button->onClick = [this, i]
                    {
                        combo->setSelectedId (i + 1, juce::sendNotificationSync);
                    };
                    choiceButtons.push_back (std::move (button));
                    addAndMakeVisible (*choiceButtons.back());
                }
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
            // A click on the knob focuses the control, which handles the arrow keys.
            setWantsKeyboardFocus (true);

            // The ribbon is relative: it springs back to the centre on release (its own gesture,
            // the drag's gesture has ended by now).
            if (ribbonLike)
                slider->onDragEnd = [this] { setValueAsGesture (0.0f); };
            break;
        }
    }
    if (combo != nullptr && ! choiceButtons.empty())
    {
        const int selected = combo->getSelectedId() - 1;
        for (int i = 0; i < (int) choiceButtons.size(); ++i)
            choiceButtons[(size_t) i]->setToggleState (i == selected, juce::dontSendNotification);
    }
}

ParamControl::~ParamControl()
{
    removeMouseListener (&contextMenuTrigger);
}

void ParamControl::setMidiLearnHandlers (std::function<bool()> learn, std::function<void()> clear,
                                         std::function<int()> assignedCC)
{
    learnMidiCC = std::move (learn);
    clearMidiCC = std::move (clear);
    getAssignedMidiCC = std::move (assignedCC);
    midiLearnShown = false;
}

void ParamControl::ContextMenuTrigger::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        owner.showContextMenu();
}

void ParamControl::showContextMenu()
{
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
            // Until a controller moves; refresh() restores the tooltip once the assignment changed.
            if (safeThis->learnMidiCC())
                safeThis->setTooltip ("Move a MIDI controller to assign it to " + safeThis->paramId + ".");
        }
        else if (result == 2 && safeThis->clearMidiCC != nullptr)
            safeThis->clearMidiCC();
        else if (result >= ModulationHost::kModulationMenuBase && safeThis->modulationHost != nullptr)
            safeThis->modulationHost->handleModulationMenu (result, safeThis->layer, safeThis->parameterIndex);
    });
}

void ParamControl::setValueAsGesture (float newValue)
{
    parameter.beginChangeGesture();
    slider->setValue (newValue, juce::sendNotificationSync);
    parameter.endChangeGesture();
}

bool ParamControl::keyPressed (const juce::KeyPress& key)
{
    // Plain (or Shift for fine) arrow and page keys; shortcuts with other modifiers go to the editor.
    const auto mods = key.getModifiers();
    if (slider == nullptr || mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const int code = key.getKeyCode();
    const int steps = code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey ? 1
                    : code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey ? -1
                    : code == juce::KeyPress::pageUpKey ? 10
                    : code == juce::KeyPress::pageDownKey ? -10 : 0;
    if (steps == 0)
        return false;

    // Stepped parameters move by whole steps, continuous ones by 1 % (Shift: 0.1 %) of their range.
    const auto& range = parameter.getNormalisableRange();
    float target;
    if (range.interval > 0.0f)
        target = range.snapToLegalValue (juce::jlimit (range.start, range.end, (float) slider->getValue() + range.interval * (float) steps));
    else
        target = range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, parameter.getValue() + (mods.isShiftDown() ? 0.001f : 0.01f) * (float) steps));
    setValueAsGesture (target);
    return true;
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
    // The editor shows (and commits) only the name, never the " [CC n]" suffix of the caption.
    caption.onEditorShow = [this]
    {
        if (auto* editor = caption.getCurrentTextEditor())
        {
            editor->setText (captionText, false);
            editor->selectAll();
        }
    };
    caption.onTextChange = [this, setText = std::move (setText)]
    {
        setText (caption.getText());
        captionText = getCaptionText();
        updateCaption();
    };
    captionText = getCaptionText();
    updateCaption();
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

void ParamControl::refresh (uint32_t midiLearnGeneration)
{
    if (combo != nullptr && ! choiceButtons.empty())
    {
        const int selected = combo->getSelectedId() - 1;
        for (int i = 0; i < (int) choiceButtons.size(); ++i)
            if (choiceButtons[(size_t) i]->getToggleState() != (i == selected))
                choiceButtons[(size_t) i]->setToggleState (i == selected, juce::dontSendNotification);
    }

    bool captionChanged = false;
    if (! midiLearnShown || midiLearnGeneration != shownMidiLearnGeneration)
    {
        midiLearnShown = true;
        shownMidiLearnGeneration = midiLearnGeneration;
        shownMidiCC = getAssignedMidiCC != nullptr ? getAssignedMidiCC() : -1;
        setTooltip (baseTooltip + (shownMidiCC >= 0 ? " [MIDI CC " + juce::String (shownMidiCC) + "]" : juce::String()));
        captionChanged = true;
    }
    if (getCaptionText != nullptr && ! caption.isBeingEdited())
    {
        const auto text = getCaptionText();
        captionChanged |= text != captionText;
        captionText = text;
    }
    if (captionChanged)
        updateCaption();
}

void ParamControl::updateCaption()
{
    if (! caption.isBeingEdited())
        caption.setText (captionText + (shownMidiCC >= 0 ? " [CC " + juce::String (shownMidiCC) + "]" : ""),
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

bool ModulationHost::isCentredSource (ModSource source) noexcept
{
    return source == ModSource::lfo1 || source == ModSource::lfo2 || source == ModSource::dds2
        || source == ModSource::perNotePitch || source == ModSource::ribbon;
}

juce::String ModulationHost::sourceName (const ModMatrix& matrix, int source)
{
    return source >= (int) ModSource::macro1 ? matrix.getMacroName (source - (int) ModSource::macro1)
                                             : juce::String (ModMatrix::getSourceName ((ModSource) source));
}

int ModulationHost::findEmptySlot (const ModMatrix& matrix) noexcept
{
    for (int i = 0; i < ModMatrixData::kMaxSlots; ++i)
    {
        const auto slot = matrix.getSlot (i);
        if (slot.source < 0 && slot.destination < 0)
            return i;
    }
    return -1;
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
SectionPanel::SectionPanel (juce::String t, juce::Colour a, int cols) : title (std::move (t)), accent (a), columns (juce::jmax (1, cols))
{
    // A click on the panel takes the keyboard focus away from the last knob.
    setWantsKeyboardFocus (true);
}

void SectionPanel::add (std::unique_ptr<ParamControl> control, bool sharesPreviousCell)
{
    addAndMakeVisible (*control);
    if (sharesPreviousCell && ! cells.empty())
        cells.back().push_back (control.get());
    else
        cells.push_back ({ control.get() });
    controls.push_back (std::move (control));
}

void SectionPanel::addHeaderComponent (std::unique_ptr<juce::Component> component, int width)
{
    addAndMakeVisible (*component);
    headerComponents.push_back (std::move (component));
    headerWidths.push_back (width);
}

juce::String SectionPanel::titleFor (Section section)
{
    if (section == Section::perfControl) return "LFO 2 / Bend / Glide";
    if (section == Section::ddsMod)      return "Osc Modulation";
    if (section == Section::vcf)         return "Filter";
    if (section == Section::vca)         return "Amp";
    return sectionName (section);
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
    for (size_t i = headerComponents.size(); i-- > 0;)
    {
        auto* chip = dynamic_cast<ModSourceChip*> (headerComponents[i].get());
        headerComponents[i]->setBounds (header.removeFromRight (chip != nullptr ? chip->getPreferredWidth() : headerWidths[i]));
        header.removeFromRight (4);
    }
    const int n = (int) cells.size();
    const int rows = juce::jmax (1, (n + columns - 1) / columns);
    const int cw = area.getWidth() / columns, rh = area.getHeight() / rows;
    for (int i = 0; i < n; ++i)
        for (auto* control : cells[(size_t) i])
            control->setBounds (area.getX() + (i % columns) * cw, area.getY() + (i / columns) * rh, cw, rh);
}
