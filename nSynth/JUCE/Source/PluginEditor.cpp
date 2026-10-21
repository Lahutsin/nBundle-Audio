#include "PluginEditor.h"

using namespace nsynth;

namespace
{
const juce::Colour kBackground { 0xff17191e };
const juce::Colour kPanel { 0xff22252c };
const juce::Colour kOutline { 0xff363b48 };
const juce::Colour kText { 0xffe6e7eb };
const juce::Colour kTextDim { 0xff8b909c };
const juce::Colour kUpperAccent { 0xff3fa9f5 };
const juce::Colour kLowerAccent { 0xfff58a3f };
const juce::Colour kPerfAccent { 0xff9ad46a };

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
    l->setFont (juce::Font (juce::FontOptions (9.5f)));
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
    caption.setFont (juce::Font (juce::FontOptions (9.5f)));
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
            if (spec.choices.size() == 3)
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
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 12);
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
    caption.setBounds (r.removeFromTop (11));
    if (slider != nullptr)
        slider->setBounds (r);
    else if (! choiceButtons.empty())
    {
        const int buttonWidth = r.getWidth() / (int) choiceButtons.size();
        for (int i = 0; i < (int) choiceButtons.size(); ++i)
            choiceButtons[(size_t) i]->setBounds (r.getX() + i * buttonWidth, r.getY() + 1,
                                                  i == (int) choiceButtons.size() - 1 ? r.getRight() - (r.getX() + i * buttonWidth) : buttonWidth,
                                                  juce::jmin (20, r.getHeight() - 2));
    }
    else if (combo != nullptr)
        combo->setBounds (r.withSizeKeepingCentre (r.getWidth() - 2, juce::jmin (20, r.getHeight())));
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

void SectionPanel::add (std::unique_ptr<ParamControl> control)
{
    addAndMakeVisible (*control);
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
    g.setFont (juce::Font (juce::FontOptions (10.5f)).boldened());
    g.drawText (title.toUpperCase(), getLocalBounds().reduced (6, 2).removeFromTop (14), juce::Justification::centredLeft);
}

void SectionPanel::resized()
{
    auto area = getLocalBounds().reduced (3, 2);
    area.removeFromTop (15);
    const int n = (int) controls.size();
    const int rows = juce::jmax (1, (n + columns - 1) / columns);
    const int cw = area.getWidth() / columns, rh = area.getHeight() / rows;
    for (int i = 0; i < n; ++i)
        controls[(size_t) i]->setBounds (area.getX() + (i % columns) * cw, area.getY() + (i / columns) * rh, cw, rh);
}

//==============================================================================
ModMatrixGrid::ModMatrixGrid (ModMatrix& m, bool isDirect)
    : matrix (&m), direct (isDirect)
{
    const int columns = direct ? ModMatrixData::directTargetCount : ModMatrixData::targetCount;
    for (int source = 0; source < ModMatrixData::sourceCount; ++source)
    {
        auto label = std::make_unique<juce::Label>();
        label->setText (ModMatrix::getSourceName ((ModSource) source), juce::dontSendNotification);
        label->setColour (juce::Label::textColourId, kText);
        label->setFont (juce::Font (juce::FontOptions (11.0f)));
        addAndMakeVisible (*label);
        rowLabels.push_back (std::move (label));
    }
    for (int target = 0; target < columns; ++target)
    {
        auto label = std::make_unique<juce::Label>();
        label->setText (direct ? ModMatrix::getDirectTargetName ((DirectModTarget) target)
                               : ModMatrix::getTargetName ((ModTarget) target), juce::dontSendNotification);
        label->setColour (juce::Label::textColourId, kTextDim);
        label->setFont (juce::Font (juce::FontOptions (10.0f)));
        label->setMinimumHorizontalScale (0.5f);
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*label);
        columnLabels.push_back (std::move (label));
    }

    cells.reserve ((size_t) (ModMatrixData::sourceCount * columns));
    for (int source = 0; source < ModMatrixData::sourceCount; ++source)
        for (int target = 0; target < columns; ++target)
        {
            auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::TextBoxBelow);
            slider->setRange (-1.0, 1.0, 0.01);
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, direct ? 44 : 70, 14);
            slider->setColour (juce::Slider::textBoxTextColourId, kText);
            slider->setColour (juce::Slider::textBoxBackgroundColourId, kBackground);
            slider->setColour (juce::Slider::thumbColourId, kPerfAccent);
            slider->setTooltip (juce::String (ModMatrix::getSourceName ((ModSource) source)) + " → "
                                + (direct ? ModMatrix::getDirectTargetName ((DirectModTarget) target)
                                          : ModMatrix::getTargetName ((ModTarget) target)));
            if (direct && ModMatrix::isPanelDuplicate ((ModSource) source, (DirectModTarget) target))
            {
                slider->setEnabled (false);
                slider->setTooltip (slider->getTooltip() + " (already available on the panel)");
            }
            slider->onValueChange = [this, source, target, control = slider.get()]
            {
                if (! updating && ! setAmountAt (source, target, (float) control->getValue()))
                    control->setValue (amountAt (source, target), juce::dontSendNotification);
            };
            addAndMakeVisible (*slider);
            cells.push_back (std::move (slider));
        }
    startTimerHz (15);
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

void ModMatrixGrid::timerCallback()
{
    const int columns = direct ? ModMatrixData::directTargetCount : ModMatrixData::targetCount;
    juce::ScopedValueSetter<bool> guard (updating, true);
    for (int source = 0; source < ModMatrixData::sourceCount; ++source)
        for (int target = 0; target < columns; ++target)
            cells[(size_t) (source * columns + target)]->setValue (amountAt (source, target), juce::dontSendNotification);
}

void ModMatrixGrid::resized()
{
    auto area = getLocalBounds().reduced (6);
    const int labelWidth = direct ? 105 : 130;
    const int headerHeight = 52;
    const int rowHeight = juce::jmax (44, (area.getHeight() - headerHeight) / ModMatrixData::sourceCount);
    auto header = area.removeFromTop (headerHeight);
    header.removeFromLeft (labelWidth);
    const int columnWidth = header.getWidth() / (int) columnLabels.size();
    for (int target = 0; target < (int) columnLabels.size(); ++target)
        columnLabels[(size_t) target]->setBounds (header.getX() + target * columnWidth, header.getY(), columnWidth, header.getHeight());

    for (int source = 0; source < ModMatrixData::sourceCount; ++source)
    {
        auto row = area.removeFromTop (rowHeight);
        rowLabels[(size_t) source]->setBounds (row.removeFromLeft (labelWidth).reduced (2));
        const int cellWidth = row.getWidth() / (int) columnLabels.size();
        for (int target = 0; target < (int) columnLabels.size(); ++target)
            cells[(size_t) (source * (int) columnLabels.size() + target)]->setBounds (
                row.getX() + target * cellWidth, row.getY(), cellWidth, row.getHeight());
    }
}

ModMatrixPage::ModMatrixPage (SynthEngine& e, Layer initialLayer)
    : engine (e), layer (initialLayer)
{
    setOpaque (true);
    tabs.addTab ("8 × 8 MATRIX", kPanel, new ModMatrixGrid (engine.modMatrix (layer), false), true);
    tabs.addTab ("DIRECT MAPPING", kPanel, new ModMatrixGrid (engine.modMatrix (layer), true), true);
    addAndMakeVisible (tabs);
    for (auto* button : { &closeButton, &clearAllButton, &clearSourceButton, &clearTargetButton })
        addAndMakeVisible (*button);
    addAndMakeVisible (sourceChoice);
    addAndMakeVisible (targetChoice);
    for (int i = 0; i < ModMatrixData::sourceCount; ++i)
        sourceChoice.addItem (ModMatrix::getSourceName ((ModSource) i), i + 1);
    for (int i = 0; i < ModMatrixData::targetCount; ++i)
        targetChoice.addItem (ModMatrix::getTargetName ((ModTarget) i), i + 1);
    targetChoice.setSelectedId (1, juce::dontSendNotification);
    sourceChoice.setSelectedId (1, juce::dontSendNotification);
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    clearAllButton.onClick = [this] { engine.modMatrix (layer).clearAll(); };
    clearSourceButton.onClick = [this]
    {
        const int selected = sourceChoice.getSelectedId() - 1;
        if (selected >= 0 && selected < ModMatrixData::sourceCount)
            engine.modMatrix (layer).clearSource ((ModSource) selected);
    };
    clearTargetButton.onClick = [this]
    {
        const int selected = targetChoice.getSelectedId() - 1;
        if (selected < 0)
            return;
        if (tabs.getCurrentTabIndex() == 0 && selected < ModMatrixData::targetCount)
            engine.modMatrix (layer).clearTarget ((ModTarget) selected);
        else if (tabs.getCurrentTabIndex() == 1 && selected < ModMatrixData::directTargetCount)
            engine.modMatrix (layer).clearDirectTarget ((DirectModTarget) selected);
    };
    tabs.onTabChanged = [this] (int tabIndex)
    {
        targetChoice.clear (juce::dontSendNotification);
        const int count = tabIndex == 0 ? ModMatrixData::targetCount : ModMatrixData::directTargetCount;
        for (int i = 0; i < count; ++i)
            targetChoice.addItem (tabIndex == 0 ? ModMatrix::getTargetName ((ModTarget) i)
                                               : ModMatrix::getDirectTargetName ((DirectModTarget) i), i + 1);
        targetChoice.setSelectedId (1, juce::dontSendNotification);
    };
}

void ModMatrixPage::setLayer (Layer selectedLayer)
{
    layer = selectedLayer;
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (auto* grid = dynamic_cast<ModMatrixGrid*> (tabs.getTabContentComponent (i)))
            grid->setMatrix (engine.modMatrix (layer));
}

void ModMatrixPage::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto controls = area.removeFromTop (36);
    closeButton.setBounds (controls.removeFromRight (90));
    controls.removeFromRight (8);
    clearTargetButton.setBounds (controls.removeFromRight (120));
    targetChoice.setBounds (controls.removeFromRight (190).reduced (2));
    controls.removeFromRight (8);
    clearSourceButton.setBounds (controls.removeFromRight (125));
    sourceChoice.setBounds (controls.removeFromRight (170).reduced (2));
    controls.removeFromRight (8);
    clearAllButton.setBounds (controls.removeFromRight (100));
    area.removeFromTop (8);
    tabs.setBounds (area);
}

WaveBrowserPage::WaveBrowserPage (SynthEngine& e)
    : engine (e)
{
    setOpaque (true);
    for (int i = 0; i < WaveBank::kNumAltWaves; ++i)
        slotChoice.addItem ("ALT " + juce::String (i + 1), i + 1);
    slotChoice.setSelectedId (1, juce::dontSendNotification);
    slotChoice.onChange = [this] { repaint(); };
    status.setText ("Import one periodic cycle into the selected slot.", juce::dontSendNotification);
    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, kText);
    for (juce::Component* component : std::array<juce::Component*, 5> {
             &slotChoice, &importButton, &exportButton, &closeButton, &status })
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
            if (! engine.requestWaveImport (slotChoice.getSelectedId() - 1, file, &error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Wave import failed", error);
            else
                status.setText ("Building mip tables off the audio thread…", juce::dontSendNotification);
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
            if (! engine.exportWave (slotChoice.getSelectedId() - 1, file, &error))
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
            status.setText ("Building mip tables off the audio thread…", juce::dontSendNotification);
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
    g.fillAll (kBackground);
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (190);
    auto waveBox = area.withSizeKeepingCentre (juce::jmin (860, area.getWidth()), juce::jmin (320, area.getHeight())).toFloat();
    if (waveBox.getHeight() < 40.0f)
        return;

    g.setColour (kPanel);
    g.fillRoundedRectangle (waveBox, 8.0f);
    g.setColour (kOutline);
    g.drawRoundedRectangle (waveBox, 8.0f, 1.0f);
    g.drawHorizontalLine (juce::roundToInt (waveBox.getCentreY()), waveBox.getX() + 8.0f, waveBox.getRight() - 8.0f);

    const int slot = juce::jlimit (0, WaveBank::kNumAltWaves - 1, slotChoice.getSelectedId() - 1);
    const auto& cycle = engine.getWaveBank().altCycle (slot);
    if (cycle.empty())
        return;

    const auto plot = waveBox.reduced (16.0f, 18.0f);
    juce::Path path;
    constexpr int points = 512;
    for (int i = 0; i < points; ++i)
    {
        const float normX = (float) i / (float) (points - 1);
        const size_t idx = (size_t) juce::jlimit (0, (int) cycle.size() - 1, (int) (normX * (float) (cycle.size() - 1)));
        const float x = plot.getX() + normX * plot.getWidth();
        const float y = plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, cycle[idx]) * (plot.getHeight() * 0.45f);
        if (i == 0) path.startNewSubPath (x, y);
        else        path.lineTo (x, y);
    }
    g.setColour (kUpperAccent);
    g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void WaveBrowserPage::resized()
{
    auto area = getLocalBounds().reduced (20);
    closeButton.setBounds (area.removeFromTop (30).removeFromRight (84));
    area.removeFromTop (8);
    slotChoice.setBounds (area.removeFromTop (30).withSizeKeepingCentre (200, 30));
    area.removeFromTop (8);
    auto buttons = area.removeFromTop (30).withSizeKeepingCentre (300, 30);
    importButton.setBounds (buttons.removeFromLeft (142));
    buttons.removeFromLeft (12);
    exportButton.setBounds (buttons.removeFromLeft (146));
    area.removeFromTop (10);
    status.setBounds (area.removeFromTop (36));
}

SequencePage::SequencePage (juce::AudioProcessorValueTreeState& apvts, SynthEngine& e, Layer initialLayer)
    : state (apvts), engine (e), layer (initialLayer)
{
    setOpaque (true);
    for (int i = 0; i < SequenceBankData::kNumSlots; ++i)
        slotChoice.addItem ("SLOT " + juce::String (i + 1), i + 1);
    slotChoice.onChange = [this] { chooseSlot(); };
    addAndMakeVisible (slotChoice);

    auto addSlider = [this] (juce::Slider& slider)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 22);
        addAndMakeVisible (slider);
    };
    lengthSlider.setRange (1, Sequence::kMaxSteps, 1);
    noteSlider.setRange (-48, 48, 1);
    gateSlider.setRange (1, 100, 1);
    addSlider (lengthSlider);
    addSlider (noteSlider);
    addSlider (gateSlider);
    lengthSlider.onValueChange = [this]
    {
        engine.sequenceBank (layer).setLength (slotChoice.getSelectedId() - 1, (int) lengthSlider.getValue());
        refreshControls();
    };
    noteSlider.onValueChange = [this] { commitSelectedStep(); };
    gateSlider.onValueChange = [this] { commitSelectedStep(); };

    for (auto* toggle : { &slideButton, &accentButton, &restButton, &recordButton })
        addAndMakeVisible (*toggle);
    slideButton.onClick = [this] { commitSelectedStep(); };
    accentButton.onClick = [this] { commitSelectedStep(); };
    restButton.onClick = [this] { commitSelectedStep(); };
    recordButton.setClickingTogglesState (true);
    recordButton.onClick = [this]
    {
        engine.sequenceBank (layer).setRecording (recordButton.getToggleState(),
                                                  slotChoice.getSelectedId() - 1, selectedStep);
        status.setText (recordButton.getToggleState() ? "Recording MIDI into selected step(s)." : "Recording stopped.",
                        juce::dontSendNotification);
    };

    for (int i = 0; i < Sequence::kMaxSteps; ++i)
    {
        auto button = std::make_unique<juce::TextButton>();
        button->setClickingTogglesState (false);
        button->onClick = [this, i] { selectStep (i); };
        addAndMakeVisible (*button);
        stepButtons.push_back (std::move (button));
    }

    for (auto* button : { &clearSlotButton, &saveBankButton, &loadBankButton, &closeButton })
        addAndMakeVisible (*button);
    addAndMakeVisible (status);
    for (auto* label : { &noteLabel, &gateLabel, &lengthLabel })
    {
        label->setColour (juce::Label::textColourId, kTextDim);
        label->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*label);
    }

    clearSlotButton.onClick = [this]
    {
        engine.sequenceBank (layer).clearSlot (slotChoice.getSelectedId() - 1);
        selectedStep = 0;
        refreshControls();
    };
    saveBankButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Save sequence bank", juce::File(), "*.nsynthseq");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            const auto xml = engine.sequenceBank (layer).toValueTree (toInt (layer)).createXml();
            if (xml == nullptr || ! file.withFileExtension (".nsynthseq").replaceWithText (xml->toString()))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sequence save failed",
                                                        "Could not write sequence bank file.");
        });
    };
    loadBankButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load sequence bank", juce::File(), "*.nsynthseq");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            const auto xml = juce::XmlDocument::parse (file);
            juce::String error;
            if (xml == nullptr
                || ! engine.sequenceBank (layer).restoreFromValueTree (juce::ValueTree::fromXml (*xml), toInt (layer), &error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sequence load failed",
                                                        error.isNotEmpty() ? error : "Invalid sequence file.");
            else
                refreshControls();
        });
    };
    closeButton.onClick = [this]
    {
        engine.sequenceBank (layer).setRecording (false, 0, 0);
        recordButton.setToggleState (false, juce::dontSendNotification);
        if (onClose != nullptr)
            onClose();
    };

    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, kText);
    setLayer (initialLayer);
    startTimerHz (20);
}

SequencePage::~SequencePage()
{
    engine.sequenceBank (layer).setRecording (false, 0, 0);
}

void SequencePage::setLayer (Layer selectedLayer)
{
    engine.sequenceBank (layer).setRecording (false, 0, 0);
    layer = selectedLayer;
    recordButton.setToggleState (false, juce::dontSendNotification);
    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
        state.getParameter (layerParamId (layer, pid::seqSlot)));
    const int selectedSlot = parameter != nullptr
        ? juce::jlimit (1, SequenceBankData::kNumSlots, (int) std::lround (parameter->getValue()))
        : 1;
    slotChoice.setSelectedId (selectedSlot, juce::dontSendNotification);
    selectedStep = 0;
    refreshControls();
}

void SequencePage::chooseSlot()
{
    const int slot = slotChoice.getSelectedId();
    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
        state.getParameter (layerParamId (layer, pid::seqSlot)));
    if (parameter != nullptr && slot > 0)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) slot));
    selectedStep = 0;
    if (recordButton.getToggleState())
        engine.sequenceBank (layer).setRecording (true, slot - 1, selectedStep);
    refreshControls();
}

void SequencePage::selectStep (int step)
{
    selectedStep = juce::jlimit (0, Sequence::kMaxSteps - 1, step);
    if (recordButton.getToggleState())
        engine.sequenceBank (layer).setRecording (true, slotChoice.getSelectedId() - 1, selectedStep);
    refreshControls();
}

void SequencePage::commitSelectedStep()
{
    const int slot = slotChoice.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow (slot, SequenceBankData::kNumSlots))
        return;
    auto snapshot = engine.sequenceBank (layer).snapshot();
    auto step = snapshot->slots[(size_t) slot].steps[(size_t) selectedStep];
    if (step.noteCount == 0)
    {
        step.notes[0] = 0;
        step.noteCount = 1;
    }
    step.notes[0] = (int8_t) (int) noteSlider.getValue();
    step.rest = restButton.getToggleState();
    step.slide = slideButton.getToggleState();
    step.accent = accentButton.getToggleState();
    step.gatePercent = (uint8_t) (int) gateSlider.getValue();
    engine.sequenceBank (layer).setStep (slot, selectedStep, step);
    refreshControls();
}

void SequencePage::refreshControls()
{
    const int slot = slotChoice.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow (slot, SequenceBankData::kNumSlots))
        return;
    const auto current = engine.sequenceBank (layer).snapshot();
    const auto& sequence = current->slots[(size_t) slot];
    selectedStep = juce::jmin (selectedStep, (int) sequence.length - 1);
    if (recordButton.getToggleState())
        engine.sequenceBank (layer).setRecording (true, slot, selectedStep);
    lengthSlider.setValue (sequence.length, juce::dontSendNotification);
    for (int i = 0; i < Sequence::kMaxSteps; ++i)
    {
        const auto& step = sequence.steps[(size_t) i];
        juce::String text = juce::String (i + 1).paddedLeft ('0', 2);
        if (step.rest)
            text += " R";
        else if (step.noteCount > 0)
            text += " " + juce::String ((int) step.notes[0] >= 0 ? "+" : "")
                  + juce::String ((int) step.notes[0]);
        stepButtons[(size_t) i]->setButtonText (text);
        stepButtons[(size_t) i]->setEnabled (i < sequence.length);
        stepButtons[(size_t) i]->setColour (juce::TextButton::buttonColourId,
                                            i == selectedStep ? accentForLayer (layer)
                                                              : (step.rest ? kPanel : kPanel.brighter (0.18f)));
    }
    const auto& selected = sequence.steps[(size_t) selectedStep];
    noteSlider.setValue (selected.noteCount > 0 ? selected.notes[0] : 0, juce::dontSendNotification);
    gateSlider.setValue (selected.gatePercent, juce::dontSendNotification);
    slideButton.setToggleState (selected.slide, juce::dontSendNotification);
    accentButton.setToggleState (selected.accent, juce::dontSendNotification);
    restButton.setToggleState (selected.rest, juce::dontSendNotification);
}

void SequencePage::timerCallback()
{
    auto& bank = engine.sequenceBank (layer);
    SequenceBank::RecordedNote note;
    while (bank.popRecordedNote (note))
    {
        auto snapshot = bank.snapshot();
        auto step = snapshot->slots[(size_t) note.slot].steps[(size_t) note.step];
        if (step.noteCount < SequenceStep::kMaxNotes)
        {
            bool duplicate = false;
            for (int i = 0; i < (int) step.noteCount; ++i)
                duplicate = duplicate || step.notes[(size_t) i] == note.interval;
            if (! duplicate)
            {
                step.notes[(size_t) step.noteCount++] = (int8_t) note.interval;
                step.rest = false;
                bank.setStep (note.slot, note.step, step);
            }
        }
        else
            status.setText ("Selected step already contains 8 notes; extra MIDI notes were ignored.", juce::dontSendNotification);
    }
    const int dropped = bank.takeDroppedRecordCount();
    if (dropped > 0)
        status.setText ("MIDI record queue full; dropped " + juce::String (dropped) + " note(s).", juce::dontSendNotification);
    else if (bank.isRecording())
        status.setText ("Recording MIDI into the selected step.", juce::dontSendNotification);
    refreshControls();
}

void SequencePage::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (32);
    slotChoice.setBounds (header.removeFromLeft (130));
    header.removeFromLeft (8);
    lengthLabel.setBounds (header.removeFromLeft (68));
    lengthSlider.setBounds (header.removeFromLeft (220));
    header.removeFromLeft (10);
    closeButton.setBounds (header.removeFromRight (84));
    header.removeFromRight (6);
    saveBankButton.setBounds (header.removeFromRight (100));
    header.removeFromRight (4);
    loadBankButton.setBounds (header.removeFromRight (100));
    header.removeFromRight (4);
    clearSlotButton.setBounds (header.removeFromRight (100));

    area.removeFromTop (8);
    const int gridHeight = juce::jmax (260, area.getHeight() - 88);
    auto grid = area.removeFromTop (gridHeight);
    const int gap = 4, cellW = (grid.getWidth() - 7 * gap) / 8, cellH = (grid.getHeight() - 7 * gap) / 8;
    for (int i = 0; i < Sequence::kMaxSteps; ++i)
    {
        const int row = i / 8, column = i % 8;
        stepButtons[(size_t) i]->setBounds (grid.getX() + column * (cellW + gap), grid.getY() + row * (cellH + gap), cellW, cellH);
    }
    area.removeFromTop (8);
    auto controls = area.removeFromTop (34);
    noteLabel.setBounds (controls.removeFromLeft (130));
    noteSlider.setBounds (controls.removeFromLeft (240));
    controls.removeFromLeft (10);
    gateLabel.setBounds (controls.removeFromLeft (54));
    gateSlider.setBounds (controls.removeFromLeft (190));
    controls.removeFromLeft (10);
    slideButton.setBounds (controls.removeFromLeft (80));
    accentButton.setBounds (controls.removeFromLeft (88));
    restButton.setBounds (controls.removeFromLeft (76));
    recordButton.setBounds (controls.removeFromLeft (125));
    status.setBounds (area.removeFromTop (32));
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

    presetName.setJustificationType (juce::Justification::centred);
    presetName.setColour (juce::Label::backgroundColourId, kBackground.darker (0.3f));
    presetName.setColour (juce::Label::outlineColourId, kOutline);
    presetName.setFont (juce::Font (juce::FontOptions (14.0f)));
    content.addAndMakeVisible (presetName);
    for (auto* b : { &prevPreset, &nextPreset, &loadPreset, &savePreset, &upperSelect, &lowerSelect, &initLayer, &copyUpToLow, &copyLowToUp,
                     &loadPatch, &savePatch, &comparePatch, &modMatrixButton, &waveBrowserButton, &sequenceButton })
        content.addAndMakeVisible (*b);

    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    loadPreset.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load performance", synth.getPresetManager().getUserPresetDirectory(), "*.nsynthperf");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            juce::String error;
            if (fc.getResult() != juce::File() && ! synth.loadPerformance (fc.getResult(), &error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load failed", error);
        });
    };
    savePreset.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Save performance", synth.getPresetManager().getUserPresetDirectory(), "*.nsynthperf");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File())
                return;
            juce::String error;
            if (! synth.savePerformance (f.withFileExtension ("nsynthperf"), &error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save failed", error);
        });
    };
    loadPatch.onClick = [this]
    {
        const auto factoryPatches = synth.getFactoryPatchDirectory();
        const auto startDirectory = factoryPatches.isDirectory() ? factoryPatches : synth.getUserPatchDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Load layer patch", startDirectory, "*.nsynthpatch");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            juce::String error;
            if (! synth.loadPatch (bottomLayer, file, &error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Patch load failed", error);
        });
    };
    savePatch.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Save layer patch", synth.getUserPatchDirectory(), "*.nsynthpatch");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            juce::String error;
            if (! synth.savePatch (bottomLayer, file, &error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Patch save failed", error);
        });
    };
    comparePatch.setClickingTogglesState (true);
    comparePatch.onClick = [this]
    {
        if (! synth.setPatchCompare (bottomLayer, comparePatch.getToggleState()))
            comparePatch.setToggleState (synth.isPatchCompareActive (bottomLayer), juce::dontSendNotification);
    };
    modMatrixButton.onClick = [this] { modMatrixPage->setVisible (true); };
    waveBrowserButton.onClick = [this] { waveBrowserPage->setVisible (true); };
    sequenceButton.onClick = [this] { sequencePage->setVisible (true); };

    upperSelect.setClickingTogglesState (false);
    upperSelect.onClick = [this] { selectBottomLayer (Layer::upper); };
    lowerSelect.onClick = [this] { selectBottomLayer (Layer::lower); };
    initLayer.onClick = [this] { synth.initLayer (bottomLayer); };
    copyUpToLow.onClick = [this] { synth.copyLayer (Layer::upper, Layer::lower); };
    copyLowToUp.onClick = [this] { synth.copyLayer (Layer::lower, Layer::upper); };

    voiceMeter.setJustificationType (juce::Justification::centredRight);
    voiceMeter.setColour (juce::Label::textColourId, kTextDim);
    content.addAndMakeVisible (voiceMeter);

    // Two identical rows of sound-shaping sections, UPPER on top.
    // Three control rows per section keeps the columns wide enough for readable switches.
    const std::pair<Section, int> rowSections[] = {
        { Section::dds1, 2 }, { Section::dds2, 2 }, { Section::mixer, 1 }, { Section::vcf, 3 }, { Section::vca, 2 },
        { Section::env1, 3 }, { Section::env2, 2 }, { Section::lfo1, 2 }, { Section::ddsMod, 3 } };
    const std::pair<Section, int> sharedSections[] = {
        { Section::perfControl, 7 }, { Section::voiceAssign, 2 }, { Section::arpSeq, 4 }, { Section::fx, 3 } };

    for (auto layer : { Layer::upper, Layer::lower })
    {
        auto& ui = layers[(size_t) toInt (layer)];
        const auto accent = accentForLayer (layer);
        for (auto [section, cols] : rowSections)
        {
            ui.rowSections.push_back (makeSection (layer, section, cols, accent));
            content.addAndMakeVisible (*ui.rowSections.back());
        }
        for (auto [section, cols] : sharedSections)
        {
            ui.sharedSections.push_back (makeSection (layer, section, cols, accent));
            content.addChildComponent (*ui.sharedSections.back());
        }
        ui.rowLabel.setText (layer == Layer::upper ? "UPPER" : "LOWER", juce::dontSendNotification);
        ui.rowLabel.setColour (juce::Label::textColourId, accent);
        ui.rowLabel.setFont (juce::Font (juce::FontOptions (13.0f)).boldened());
        ui.rowLabel.setJustificationType (juce::Justification::centred);
        content.addAndMakeVisible (ui.rowLabel);
    }

    performancePanel = std::make_unique<SectionPanel> ("Performance", kPerfAccent, 13);
    controllerPanel = std::make_unique<SectionPanel> ("Controllers", kPerfAccent, 1);
    for (auto& spec : getPerformanceParamSpecs())
    {
        const bool controller = juce::String (spec.suffix) == pid::ribbon || juce::String (spec.suffix) == pid::modLever
                             || juce::String (spec.suffix) == pid::expression;
        auto control = std::make_unique<ParamControl> (synth.getState(), perfParamId (spec.suffix), spec, kPerfAccent);
        configureMidiLearn (*control);
        (controller ? controllerPanel : performancePanel)->add (std::move (control));
    }
    content.addAndMakeVisible (*performancePanel);
    content.addAndMakeVisible (*controllerPanel);
    modMatrixPage = std::make_unique<ModMatrixPage> (synth.getEngine(), bottomLayer);
    modMatrixPage->onClose = [this] { modMatrixPage->setVisible (false); };
    content.addAndMakeVisible (*modMatrixPage);
    modMatrixPage->setVisible (false);
    waveBrowserPage = std::make_unique<WaveBrowserPage> (synth.getEngine());
    waveBrowserPage->onClose = [this] { waveBrowserPage->setVisible (false); };
    content.addAndMakeVisible (*waveBrowserPage);
    waveBrowserPage->setVisible (false);
    sequencePage = std::make_unique<SequencePage> (synth.getState(), synth.getEngine(), bottomLayer);
    sequencePage->onClose = [this] { sequencePage->setVisible (false); };
    content.addAndMakeVisible (*sequencePage);
    sequencePage->setVisible (false);

    for (int pct : { 50, 75, 100, 125, 150, 200 })
        sizeBox.addItem (juce::String (pct) + " %", pct);
    sizeBox.onChange = [this] { if (sizeBox.getSelectedId() > 0) setScale ((float) sizeBox.getSelectedId() / 100.0f); };
    content.addAndMakeVisible (sizeBox);

    selectBottomLayer (Layer::upper);
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
    auto panel = std::make_unique<SectionPanel> (sectionName (section), accent, columns);
    for (auto& spec : getLayerParamSpecs())
        if (spec.section == section)
        {
            auto control = std::make_unique<ParamControl> (synth.getState(), layerParamId (layer, spec.suffix), spec, accent);
            configureMidiLearn (*control);
            panel->add (std::move (control));
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
    if (sequencePage != nullptr)
        sequencePage->setLayer (layer);
    for (auto l : { Layer::upper, Layer::lower })
        for (auto& s : layers[(size_t) toInt (l)].sharedSections)
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

    auto header = r.removeFromTop (36);
    title.setBounds (header.removeFromLeft (110));
    modMatrixButton.setBounds (header.removeFromLeft (92).withSizeKeepingCentre (92, 24));
    header.removeFromLeft (6);
    waveBrowserButton.setBounds (header.removeFromLeft (86).withSizeKeepingCentre (86, 24));
    header.removeFromLeft (6);
    sequenceButton.setBounds (header.removeFromLeft (86).withSizeKeepingCentre (86, 24));
    header.removeFromLeft (6);
    sizeBox.setBounds (header.removeFromRight (84).withSizeKeepingCentre (84, 24));
    header.removeFromRight (8);
    voiceMeter.setBounds (header.removeFromRight (210));
    auto bar = header.withSizeKeepingCentre (juce::jmin (480, header.getWidth() - 8), 26);
    prevPreset.setBounds (bar.removeFromLeft (28));
    savePreset.setBounds (bar.removeFromRight (58));
    bar.removeFromRight (4);
    loadPreset.setBounds (bar.removeFromRight (58));
    bar.removeFromRight (6);
    nextPreset.setBounds (bar.removeFromRight (28));
    presetName.setBounds (bar.reduced (4, 0));
    modMatrixPage->setBounds (juce::Rectangle<int> (0, 0, kDesignWidth, kDesignHeight));
    waveBrowserPage->setBounds (juce::Rectangle<int> (0, 0, kDesignWidth, kDesignHeight));
    sequencePage->setBounds (juce::Rectangle<int> (0, 0, kDesignWidth, kDesignHeight));

    performancePanel->setBounds (r.removeFromTop (84));
    r.removeFromTop (4);

    auto layoutRow = [] (juce::Rectangle<int> area, std::vector<std::unique_ptr<SectionPanel>>& sections)
    {
        int totalCols = 0;
        for (auto& s : sections)
            totalCols += s->getColumns();
        const float colW = (float) area.getWidth() / (float) totalCols;
        float x = (float) area.getX();
        for (auto& s : sections)
        {
            const float w = colW * (float) s->getColumns();
            s->setBounds (juce::Rectangle<float> (x, (float) area.getY(), w, (float) area.getHeight()).toNearestInt().reduced (1));
            x += w;
        }
    };

    for (auto layer : { Layer::upper, Layer::lower })
    {
        auto& ui = layers[(size_t) toInt (layer)];
        auto row = r.removeFromTop (214);
        // Vertical caption: lay out horizontally, then rotate -90 degrees into the left strip.
        const auto strip = row.removeFromLeft (20);
        ui.rowLabel.setBounds (0, 0, strip.getHeight(), strip.getWidth());
        ui.rowLabel.setTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi)
                                      .translated ((float) strip.getX(), (float) strip.getBottom()));
        layoutRow (row, ui.rowSections);
        r.removeFromTop (3);
    }

    auto bottom = r;
    auto layerBlock = bottom.removeFromLeft (158).reduced (2);
    auto selectRow = layerBlock.removeFromTop (30);
    upperSelect.setBounds (selectRow.removeFromLeft (selectRow.getWidth() / 2 - 2));
    selectRow.removeFromLeft (4);
    lowerSelect.setBounds (selectRow);
    layerBlock.removeFromTop (6);

    const int buttonRowH = 26;
    auto row1 = layerBlock.removeFromTop (buttonRowH);
    initLayer.setBounds (row1.removeFromLeft (row1.getWidth() / 2 - 2));
    row1.removeFromLeft (4);
    comparePatch.setBounds (row1);
    layerBlock.removeFromTop (5);

    auto row2 = layerBlock.removeFromTop (buttonRowH);
    copyUpToLow.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 2));
    row2.removeFromLeft (4);
    copyLowToUp.setBounds (row2);
    layerBlock.removeFromTop (5);

    auto row3 = layerBlock.removeFromTop (buttonRowH);
    loadPatch.setBounds (row3.removeFromLeft (row3.getWidth() / 2 - 2));
    row3.removeFromLeft (4);
    savePatch.setBounds (row3);

    controllerPanel->setBounds (bottom.removeFromRight (160).reduced (1));
    for (auto layer : { Layer::upper, Layer::lower })
        layoutRow (bottom, layers[(size_t) toInt (layer)].sharedSections);
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

std::vector<juce::File> NSynthAudioProcessorEditor::allPresets() const
{
    auto& pm = synth.getPresetManager();
    auto files = pm.getFactoryPresets();
    auto user = pm.getUserPresets();
    files.insert (files.end(), user.begin(), user.end());
    return files;
}

void NSynthAudioProcessorEditor::stepPreset (int delta)
{
    auto files = allPresets();
    if (files.empty())
        return;

    int index = -1;
    for (int i = 0; i < (int) files.size(); ++i)
        if (files[(size_t) i].getFileNameWithoutExtension() == synth.getCurrentPresetName())
            index = i;

    index = (index + delta + (int) files.size()) % (int) files.size();
    juce::String error;
    if (! synth.loadPerformance (files[(size_t) index], &error))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load failed", error);
}

void NSynthAudioProcessorEditor::refreshDisplay() noexcept
{
    const auto patchDirty = synth.isPatchDirty (bottomLayer);
    presetName.setText (synth.getCurrentPresetName() + "  |  " + synth.getPatchName (bottomLayer)
                        + (patchDirty ? "*" : ""), juce::dontSendNotification);
    comparePatch.setToggleState (synth.isPatchCompareActive (bottomLayer), juce::dontSendNotification);
    voiceMeter.setText ("Voices  U " + juce::String (synth.getActiveVoices (Layer::upper))
                        + "  /  L " + juce::String (synth.getActiveVoices (Layer::lower)),
                        juce::dontSendNotification);
}

void NSynthAudioProcessorEditor::timerCallback()
{
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
