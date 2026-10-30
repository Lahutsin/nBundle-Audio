#include "ModulationUi.h"

using namespace nsynth;
using namespace nSynth::ui;

namespace
{
constexpr int kListColumnGap = 6;

// The MSEG a parameter belongs to (-1: none).
int msegOfParameter (const ParamSpec& spec)
{
    for (int m = 0; m < kMaxMsegs; ++m)
        for (auto field : { MsegField::mode, MsegField::sync, MsegField::time, MsegField::division })
            if (std::strcmp (spec.suffix, msegSuffix (m, field)) == 0)
                return m;
    return -1;
}

void fillDestinationMenu (juce::ComboBox& box, int msegCount)
{
    // Sections in the order of the SYNTH and FX pages.
    static constexpr Section order[] = {
        Section::dds2, Section::mixer, Section::ddsMod, Section::vcf, Section::filter2, Section::vca,
        Section::env1, Section::env2, Section::lfo1, Section::perfControl, Section::voiceAssign,
        Section::mseg, Section::fxDistortion, Section::fxEq, Section::fxCompressor, Section::fx,
        Section::fxPhaser, Section::fxFlanger, Section::delay, Section::reverb
    };
    auto* root = box.getRootMenu();
    const auto& specs = getLayerParamSpecs();
    for (auto section : order)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) specs.size(); ++i)
            if (specs[(size_t) i].section == section && ModMatrix::isModulatable (i)
                && (section != Section::mseg || msegOfParameter (specs[(size_t) i]) < msegCount))
                sub.addItem (i + 1, specs[(size_t) i].name);
        if (sub.getNumItems() > 0)
            root->addSubMenu (SectionPanel::titleFor (section), sub);
    }
}

void fillSourceMenu (juce::ComboBox& box, int msegCount)
{
    auto add = [&box] (ModSource s) { box.addItem (ModMatrix::getSourceName (s), (int) s + 1); };
    box.addSectionHeading ("Envelopes");
    for (auto s : { ModSource::env1, ModSource::env2 }) add (s);
    for (int m = 0; m < msegCount; ++m)
        add (msegSource (m));
    box.addSectionHeading ("Oscillators");
    for (auto s : { ModSource::lfo1, ModSource::lfo2, ModSource::dds2 }) add (s);
    box.addSectionHeading ("Note");
    for (auto s : { ModSource::velocity, ModSource::noteNumber, ModSource::random, ModSource::polyAftertouch,
                    ModSource::perNotePitch, ModSource::timbre }) add (s);
    box.addSectionHeading ("Controllers");
    for (auto s : { ModSource::modWheel, ModSource::expression, ModSource::ribbon }) add (s);
    box.addSectionHeading ("Macros");
    for (int m = 0; m < ModMatrixData::kMacros; ++m)
        add ((ModSource) ((int) ModSource::macro1 + m));
}
} // namespace

//==============================================================================
MsegEditor::MsegEditor (SynthEngine& e, Layer l, int i, juce::Colour a)
    : engine (e), layer (l), index (i), accent (a), shape (e.modMatrix (l).getMseg (i))
{
    setRepaintsOnMouseActivity (false);
}

juce::Rectangle<float> MsegEditor::plotArea() const noexcept
{
    return getLocalBounds().toFloat().reduced (10.0f, 10.0f);
}

juce::Point<float> MsegEditor::toScreen (float x, float y) const noexcept
{
    const auto a = plotArea();
    return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() };
}

juce::Point<float> MsegEditor::fromScreen (juce::Point<float> p) const noexcept
{
    const auto a = plotArea();
    return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()), juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight()) };
}

juce::Point<float> MsegEditor::handlePosition (int segmentEnd) const noexcept
{
    const auto& a = shape.points[(size_t) segmentEnd - 1];
    const auto& b = shape.points[(size_t) segmentEnd];
    return toScreen (0.5f * (a.x + b.x), a.y + (b.y - a.y) * MsegShape::bend (0.5f, b.curve));
}

juce::Point<float> MsegEditor::snapped (juce::Point<float> v, const juce::ModifierKeys& mods) const noexcept
{
    // Shift inverts the SNAP setting.
    if (snap == mods.isShiftDown())
        return v;
    return { std::round (v.x * kGridX) / kGridX, std::round (v.y * kGridY) / kGridY };
}

int MsegEditor::pointAt (juce::Point<float> p) const noexcept
{
    for (int i = shape.count - 1; i >= 0; --i)
        if (toScreen (shape.points[(size_t) i].x, shape.points[(size_t) i].y).getDistanceFrom (p) <= 8.0f)
            return i;
    return -1;
}

int MsegEditor::handleAt (juce::Point<float> p) const noexcept
{
    for (int i = 1; i < shape.count; ++i)
    {
        const auto& a = shape.points[(size_t) i - 1];
        const auto& b = shape.points[(size_t) i];
        if (b.x - a.x > 1.0e-4f && std::abs (b.y - a.y) > 1.0e-3f && handlePosition (i).getDistanceFrom (p) <= 7.0f)
            return i;
    }
    return -1;
}

void MsegEditor::store()
{
    engine.modMatrix (layer).setMseg (index, shape);
    shapeCacheValid = false;
    repaint();
}

juce::Rectangle<int> MsegEditor::playheadArea (float phase) const noexcept
{
    if (phase < 0.0f)
        return {};
    // The line and the dot (7 px) on it, with a margin for anti-aliasing.
    const auto a = plotArea();
    const float x = toScreen (phase, 0.0f).x;
    return juce::Rectangle<float> (x - 6.0f, a.getY() - 6.0f, 12.0f, a.getHeight() + 12.0f).getSmallestIntegerContainer();
}

void MsegEditor::refresh()
{
    if (dragPoint < 0 && dragHandle < 0)
    {
        const auto current = engine.modMatrix (layer).getMseg (index);
        if (! (current == shape))
        {
            shape = current;
            shapeCacheValid = false;
            lastPhase = engine.layer (layer).getMsegPosition (index);
            repaint();
            return;
        }
    }
    const float phase = engine.layer (layer).getMsegPosition (index);
    if (std::abs (phase - lastPhase) > 1.0e-4f)
    {
        repaint (playheadArea (lastPhase));
        lastPhase = phase;
        repaint (playheadArea (lastPhase));
    }
}

void MsegEditor::rebuildShapeCache()
{
    // The shape, sampled every few pixels per segment.
    const auto a = plotArea();
    juce::Path line;
    line.startNewSubPath (toScreen (shape.points[0].x, shape.points[0].y));
    for (int i = 1; i < shape.count; ++i)
    {
        const auto& p0 = shape.points[(size_t) i - 1];
        const auto& p1 = shape.points[(size_t) i];
        const int steps = juce::jmax (1, (int) ((p1.x - p0.x) * a.getWidth() / 3.0f));
        for (int k = 1; k <= steps; ++k)
        {
            const float t = (float) k / (float) steps;
            line.lineTo (toScreen (p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * MsegShape::bend (t, p1.curve)));
        }
    }
    shapeFill = line;
    shapeFill.lineTo (a.getBottomRight());
    shapeFill.lineTo (a.getBottomLeft());
    shapeFill.closeSubPath();
    shapeStroke.clear();
    juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (shapeStroke, line);
    shapeCacheValid = true;
}

void MsegEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (kBackground);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (kOutline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    const auto a = plotArea();
    for (int i = 0; i <= kGridX; ++i)
    {
        const float x = a.getX() + a.getWidth() * (float) i / (float) kGridX;
        g.setColour (kOutline.withAlpha (i % 4 == 0 ? 0.9f : 0.35f));
        g.drawVerticalLine (juce::roundToInt (x), a.getY(), a.getBottom());
    }
    for (int i = 0; i <= kGridY; ++i)
    {
        const float y = a.getY() + a.getHeight() * (float) i / (float) kGridY;
        g.setColour (kOutline.withAlpha (i % 4 == 0 ? 0.9f : 0.35f));
        g.drawHorizontalLine (juce::roundToInt (y), a.getX(), a.getRight());
    }

    if (! shapeCacheValid)
        rebuildShapeCache();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.28f), a.getX(), a.getY(), accent.withAlpha (0.03f), a.getX(), a.getBottom(), false));
    g.fillPath (shapeFill);
    g.setColour (accent);
    g.fillPath (shapeStroke);

    // Play position of the newest voice.
    if (lastPhase >= 0.0f)
    {
        int hint = 0;
        const auto p = toScreen (lastPhase, shape.evaluate (lastPhase, hint));
        g.setColour (kText.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (p.x), a.getY(), a.getBottom());
        g.setColour (kText);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
    }

    for (int i = 1; i < shape.count; ++i)
    {
        const auto& p0 = shape.points[(size_t) i - 1];
        const auto& p1 = shape.points[(size_t) i];
        if (p1.x - p0.x <= 1.0e-4f || std::abs (p1.y - p0.y) <= 1.0e-3f)
            continue;
        const bool hot = i == hoverHandle || i == dragHandle;
        const auto h = handlePosition (i);
        g.setColour (kPanel);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (h));
        g.setColour (hot ? kText : accent.withAlpha (0.8f));
        g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (h), 1.5f);
    }
    for (int i = 0; i < shape.count; ++i)
    {
        const bool hot = i == hoverPoint || i == dragPoint;
        const auto p = toScreen (shape.points[(size_t) i].x, shape.points[(size_t) i].y);
        g.setColour (hot ? kText : accent);
        g.fillEllipse (juce::Rectangle<float> (hot ? 12.0f : 9.0f, hot ? 12.0f : 9.0f).withCentre (p));
        g.setColour (kBackground);
        g.drawEllipse (juce::Rectangle<float> (hot ? 12.0f : 9.0f, hot ? 12.0f : 9.0f).withCentre (p), 1.5f);
    }
}

void MsegEditor::mouseMove (const juce::MouseEvent& event)
{
    const int point = pointAt (event.position);
    const int handle = point < 0 ? handleAt (event.position) : -1;
    if (point != hoverPoint || handle != hoverHandle)
    {
        hoverPoint = point;
        hoverHandle = handle;
        setMouseCursor (point >= 0 ? juce::MouseCursor::DraggingHandCursor
                                   : handle >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor);
        repaint();
    }
}

void MsegEditor::mouseExit (const juce::MouseEvent&)
{
    hoverPoint = hoverHandle = -1;
    repaint();
}

void MsegEditor::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        return;
    dragPoint = pointAt (event.position);
    if (dragPoint >= 0)
        return;
    dragHandle = handleAt (event.position);
    if (dragHandle >= 0)
    {
        dragStartCurve = shape.points[(size_t) dragHandle].curve;
        return;
    }
    if (shape.count >= MsegShape::kMaxPoints)
        return;

    // A new point on the clicked position, inserted in x order.
    const auto v = snapped (fromScreen (event.position), event.mods);
    int insert = 1;
    while (insert < shape.count - 1 && shape.points[(size_t) insert].x <= v.x)
        ++insert;
    for (int i = shape.count; i > insert; --i)
        shape.points[(size_t) i] = shape.points[(size_t) i - 1];
    shape.points[(size_t) insert] = { v.x, v.y, 0.0f };
    ++shape.count;
    dragPoint = hoverPoint = insert;
    store();
}

void MsegEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (dragPoint >= 0)
    {
        auto v = snapped (fromScreen (event.position), event.mods);
        auto& p = shape.points[(size_t) dragPoint];
        if (dragPoint == 0 || dragPoint == shape.count - 1)
            v.x = p.x;   // the ends stay at 0 and 1
        else
            v.x = juce::jlimit (shape.points[(size_t) dragPoint - 1].x, shape.points[(size_t) dragPoint + 1].x, v.x);
        p.x = v.x;
        p.y = v.y;
        store();
    }
    else if (dragHandle >= 0)
    {
        const auto& a = shape.points[(size_t) dragHandle - 1];
        auto& b = shape.points[(size_t) dragHandle];
        // Dragging up bulges the segment upwards, whichever way it slopes.
        const float direction = b.y >= a.y ? 1.0f : -1.0f;
        b.curve = juce::jlimit (-1.0f, 1.0f, dragStartCurve - direction * (float) event.getDistanceFromDragStartY() / 120.0f);
        store();
    }
}

void MsegEditor::mouseUp (const juce::MouseEvent&)
{
    dragPoint = dragHandle = -1;
    repaint();
}

void MsegEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    const int point = pointAt (event.position);
    if (point > 0 && point < shape.count - 1)
    {
        for (int i = point; i < shape.count - 1; ++i)
            shape.points[(size_t) i] = shape.points[(size_t) i + 1];
        --shape.count;
        hoverPoint = -1;
        store();
        return;
    }
    const int handle = handleAt (event.position);
    if (handle >= 0)
    {
        shape.points[(size_t) handle].curve = 0.0f;
        store();
    }
}

//==============================================================================
MsegPanel::MsegPanel (SynthEngine& e, Layer l, int i, juce::Colour a, const ControlFactory& makeControl,
                      const SwitchedCellRegistrar& registerSwitched)
    : engine (e), layer (l), index (i), accent (a), editor (e, l, i, a),
      chip (msegSource (i), a)
{
    // A click on the panel or the curve takes the keyboard focus away from the last knob.
    setWantsKeyboardFocus (true);
    const std::array<const char*, 4> suffixes { msegSuffix (i, MsegField::mode), msegSuffix (i, MsegField::sync),
                                                msegSuffix (i, MsegField::time), msegSuffix (i, MsegField::division) };
    for (auto* suffix : suffixes)
    {
        controls.push_back (makeControl (l, suffix));
        addAndMakeVisible (*controls.back());
    }
    registerSwitched (controls[2].get(), controls[3].get(), suffixes[1]);   // SYNC on: length, off: time

    addAndMakeVisible (editor);
    chip.setTooltip ("MSEG " + juce::String (i + 1) + ": drag onto a knob (SYNTH / FX pages) or into the slot list");
    addAndMakeVisible (chip);

    shapeButton.setTooltip ("Replace the shape with a preset");
    shapeButton.onClick = [this]
    {
        juce::PopupMenu menu;
        const char* names[] = { "Envelope", "Ramp Up", "Ramp Down", "Triangle", "Sine", "Square", "Steps" };
        for (int k = 0; k < 7; ++k)
            menu.addItem (k + 1, names[k]);
        juce::Component::SafePointer<MsegPanel> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&shapeButton), [safeThis] (int result)
        {
            if (safeThis == nullptr || result <= 0)
                return;
            safeThis->engine.modMatrix (safeThis->layer).setMseg (safeThis->index, MsegShape::make ((MsegShape::Preset) (result - 1)));
            safeThis->editor.refresh();
        });
    };
    snapButton.setClickingTogglesState (true);
    snapButton.setToggleState (true, juce::dontSendNotification);
    snapButton.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.45f));
    snapButton.setTooltip ("Snap points to the grid (hold Shift for the opposite)");
    snapButton.onClick = [this] { editor.setSnap (snapButton.getToggleState()); };
    addAndMakeVisible (shapeButton);
    addAndMakeVisible (snapButton);
}

void MsegPanel::refresh (const ModMatrixData& data)
{
    editor.refresh();

    // Where this MSEG goes: the destinations of its active slots.
    const auto& specs = getLayerParamSpecs();
    juce::StringArray destinations;
    for (int k = 0; k < data.activeSlotCount; ++k)
    {
        const auto& slot = data.slots[(size_t) data.activeSlots[(size_t) k]];
        if (slot.source == (int) msegSource (index))
            destinations.addIfNotAlreadyThere (specs[(size_t) slot.destination].name);
    }
    const auto summary = destinations.isEmpty() ? juce::String ("not routed: drag the chip onto a knob")
                                                : juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + destinations.joinIntoString (", ");
    if (summary != routeSummary)
    {
        routeSummary = summary;
        repaint (summaryArea);
    }
}

void MsegPanel::paint (juce::Graphics& g)
{
    SectionPanel::paintFrame (g, getLocalBounds(), accent, "MSEG " + juce::String (index + 1));
    g.setColour (routeSummary.startsWith ("not") ? kTextDim : kModAccent);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawFittedText (routeSummary, summaryArea, juce::Justification::centredLeft, 1, 1.0f);
    g.setColour (kTextDim);
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    g.drawText ("click: add point   drag: move   drag a handle: bend   double-click: remove",
                editor.getBounds().withY (editor.getBottom()).withHeight (16), juce::Justification::centredRight);
}

void MsegPanel::resized()
{
    auto area = getLocalBounds().reduced (6, 3);
    auto header = area.removeFromTop (SectionPanel::kTitleHeight + 2);
    header.removeFromLeft (70);
    chip.setBounds (header.removeFromRight (chip.getPreferredWidth()).reduced (0, 1));
    header.removeFromRight (8);
    snapButton.setBounds (header.removeFromRight (52).reduced (0, 1));
    header.removeFromRight (4);
    shapeButton.setBounds (header.removeFromRight (60).reduced (0, 1));
    header.removeFromRight (10);
    summaryArea = header;

    area.removeFromTop (4);
    area.removeFromBottom (16);
    auto column = area.removeFromLeft (128);
    area.removeFromLeft (8);
    editor.setBounds (area);
    // Mode, sync and time / length from the top, the time knob no larger than the other knobs.
    column.removeFromTop (8);
    controls[0]->setBounds (column.removeFromTop (40));
    column.removeFromTop (14);
    controls[1]->setBounds (column.removeFromTop (40));
    column.removeFromTop (14);
    const auto timeCell = column.removeFromTop (juce::jmin (column.getHeight(), 104));
    controls[2]->setBounds (timeCell.withSizeKeepingCentre (juce::jmin (timeCell.getWidth(), 92), timeCell.getHeight()));
    controls[3]->setBounds (timeCell.withHeight (40));
}

//==============================================================================
ModSlotRow::ModSlotRow (ModMatrix& m, int index, juce::Colour a)
    : matrix (m), slotIndex (index), accent (a)
{
    sourceBox.setTextWhenNothingSelected ("Source...");
    destinationBox.setTextWhenNothingSelected ("Choose destination...");
    for (auto* box : { &sourceBox, &destinationBox })
    {
        box->getProperties().set ("accent", (juce::int64) accent.getARGB());
        box->onChange = [this] { commit(); };
        addAndMakeVisible (*box);
    }

    amount.setRange (-1.0, 1.0, 0.01);
    amount.setDoubleClickReturnValue (true, 0.0);
    amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
    amount.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    amount.setColour (juce::Slider::textBoxTextColourId, kText);
    amount.getProperties().set ("accent", (juce::int64) kModAccent.getARGB());
    amount.textFromValueFunction = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    amount.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("+-.0123456789").getDoubleValue() / 100.0; };
    amount.setTooltip ("Amount: share of the destination's range (double-click: 0)");
    amount.onValueChange = [this] { commit(); };
    addAndMakeVisible (amount);

    curve.setRange (-1.0, 1.0, 0.01);
    curve.setDoubleClickReturnValue (true, 0.0);
    curve.getProperties().set ("accent", (juce::int64) kModAccent.getARGB());
    curve.setTooltip ("Curve: bends the source response (double-click: linear)");
    curve.onValueChange = [this] { commit(); };
    addAndMakeVisible (curve);

    polarity.setClickingTogglesState (true);
    polarity.setTooltip ("UNI: the source adds 0..amount; BI: it swings -amount..+amount around the knob");
    polarity.setColour (juce::TextButton::buttonOnColourId, kModAccent.darker (0.5f));
    polarity.onClick = [this]
    {
        polarity.setButtonText (polarity.getToggleState() ? "BI" : "UNI");
        commit();
    };
    addAndMakeVisible (polarity);

    removeButton.setTooltip ("Remove this slot");
    removeButton.onClick = [this] { matrix.removeSlot (slotIndex); };
    addAndMakeVisible (removeButton);
}

void ModSlotRow::setSlotIndex (int newIndex)
{
    if (newIndex == slotIndex)
        return;
    slotIndex = newIndex;
    shown = { -2, -2 };   // forces the next refresh
}

void ModSlotRow::commit()
{
    if (updating)
        return;
    ModSlot slot;
    slot.source = sourceBox.getSelectedId() - 1;
    slot.destination = destinationBox.getSelectedId() - 1;
    slot.amount = (float) amount.getValue();
    slot.curve = (float) curve.getValue();
    slot.bipolar = polarity.getToggleState();
    if (matrix.setSlot (slotIndex, slot))
        shown = matrix.getSlot (slotIndex);
    repaint();
}

void ModSlotRow::refresh (const juce::StringArray& macroNames, int msegCount)
{
    if (msegCount != shownMsegCount)
    {
        shownMsegCount = msegCount;
        sourceBox.clear (juce::dontSendNotification);
        fillSourceMenu (sourceBox, msegCount);
        destinationBox.clear (juce::dontSendNotification);
        fillDestinationMenu (destinationBox, msegCount);
        shown = { -2, -2 };   // select the slot's items again
    }
    for (int m = 0; m < macroNames.size(); ++m)
        if (sourceBox.getItemText (sourceBox.indexOfItemId ((int) ModSource::macro1 + m + 1)) != macroNames[m])
            sourceBox.changeItemText ((int) ModSource::macro1 + m + 1, macroNames[m]);

    const auto slot = matrix.getSlot (slotIndex);
    if (slot == shown || amount.isMouseButtonDown() || curve.isMouseButtonDown())
        return;
    shown = slot;
    const juce::ScopedValueSetter<bool> guard (updating, true);
    sourceBox.setSelectedId (slot.source + 1, juce::dontSendNotification);
    destinationBox.setSelectedId (slot.destination + 1, juce::dontSendNotification);
    amount.setValue (slot.amount, juce::dontSendNotification);
    curve.setValue (slot.curve, juce::dontSendNotification);
    polarity.setToggleState (slot.bipolar, juce::dontSendNotification);
    polarity.setButtonText (slot.bipolar ? "BI" : "UNI");
    repaint();
}

void ModSlotRow::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().reduced (0, 2).toFloat();
    g.setColour (kBackground.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 4.0f);
    const bool complete = shown.source >= 0 && shown.destination >= 0;
    g.setColour (complete && ! juce::exactlyEqual (shown.amount, 0.0f) ? kModAccent : kOutline);
    g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f);
    g.setColour (kTextDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (juce::String (slotIndex + 1).paddedLeft ('0', 2), getLocalBounds().withX (8).withWidth (20), juce::Justification::centredLeft);
    g.setColour (kTextDim);
    const auto arrowArea = sourceBox.getBounds().withX (sourceBox.getRight()).withWidth (kListColumnGap * 2 + 6).toFloat();
    juce::Path arrow;
    arrow.addArrow ({ arrowArea.getX() + 3.0f, arrowArea.getCentreY(), arrowArea.getRight() - 3.0f, arrowArea.getCentreY() }, 1.2f, 6.0f, 5.0f);
    g.fillPath (arrow);
}

void ModSlotRow::resized()
{
    auto r = getLocalBounds().reduced (0, 5);
    r.removeFromLeft (30);
    removeButton.setBounds (r.removeFromRight (24));
    r.removeFromRight (kListColumnGap);
    polarity.setBounds (r.removeFromRight (40));
    r.removeFromRight (kListColumnGap);
    curve.setBounds (r.removeFromRight (26).withSizeKeepingCentre (24, 24));
    r.removeFromRight (kListColumnGap);
    amount.setBounds (r.removeFromRight (150));
    r.removeFromRight (kListColumnGap);
    sourceBox.setBounds (r.removeFromLeft (118));
    r.removeFromLeft (kListColumnGap * 2 + 6);
    destinationBox.setBounds (r);
}

//==============================================================================
ModSlotList::ModSlotList (ModMatrix& m, juce::Colour a)
    : matrix (m), accent (a)
{
}

bool ModSlotList::isInterestedInDragSource (const SourceDetails& details)
{
    return ModulationHost::sourceFromDrag (details.description) != ModSource::count;
}

void ModSlotList::itemDropped (const SourceDetails& details)
{
    addSlotWithSource (ModulationHost::sourceFromDrag (details.description));
}

void ModSlotList::addSlotWithSource (ModSource source)
{
    const int slot = ModulationHost::findEmptySlot (matrix);
    if (slot >= 0)
        matrix.setSlot (slot, { (int) source, -1, 0.5f, 0.0f, ModulationHost::isCentredSource (source) });
}

void ModSlotList::refresh (const juce::StringArray& macroNames, int msegCount)
{
    std::vector<int> used;
    for (int i = 0; i < ModMatrixData::kMaxSlots; ++i)
    {
        const auto slot = matrix.getSlot (i);
        if (slot.source >= 0 || slot.destination >= 0)
            used.push_back (i);
    }

    bool layoutChanged = used.size() != rows.size();
    while (rows.size() < used.size())
    {
        rows.push_back (std::make_unique<ModSlotRow> (matrix, used[rows.size()], accent));
        addAndMakeVisible (*rows.back());
    }
    rows.resize (used.size());
    for (size_t k = 0; k < used.size(); ++k)
        rows[k]->setSlotIndex (used[k]);
    for (auto& row : rows)
        row->refresh (macroNames, msegCount);
    if (layoutChanged)
    {
        setSize (getWidth(), juce::jmax (ModSlotRow::kHeight, (int) rows.size() * ModSlotRow::kHeight));
        resized();
        repaint();
    }
}

void ModSlotList::resized()
{
    for (size_t k = 0; k < rows.size(); ++k)
        rows[k]->setBounds (0, (int) k * ModSlotRow::kHeight, getWidth(), ModSlotRow::kHeight);
}

void ModSlotList::paint (juce::Graphics& g)
{
    if (! rows.empty())
        return;
    g.setColour (kTextDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("No modulation yet: drop a source here, click one above, or right-click any knob.",
                getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
MsegTabBar::MsegTabBar()
{
    setRepaintsOnMouseActivity (false);
}

void MsegTabBar::setState (int newCount, int newSelected, const std::array<int, kMaxMsegs>& newRoutes)
{
    if (newCount == count && newSelected == selected && newRoutes == routes)
        return;
    count = newCount;
    selected = newSelected;
    routes = newRoutes;
    repaint();
}

juce::Rectangle<int> MsegTabBar::addButtonBounds() const
{
    return count < kMaxMsegs ? getLocalBounds().removeFromRight (30).reduced (2, 3) : juce::Rectangle<int>();
}

std::vector<MsegTabBar::Tab> MsegTabBar::tabs() const
{
    auto area = getLocalBounds();
    if (count < kMaxMsegs)
        area.removeFromRight (32);
    const int width = count > 0 ? juce::jlimit (30, 96, area.getWidth() / count) : 0;
    std::vector<Tab> result;
    for (int m = 0; m < count; ++m)
    {
        const auto bounds = juce::Rectangle<int> (area.getX() + m * width, area.getY(), width, area.getHeight()).reduced (1, 3);
        // The x of the selected (or hovered) tab; there is always at least one MSEG.
        const bool closable = count > 1 && (m == selected || m == hoverTab);
        result.push_back ({ bounds, closable ? bounds.withTrimmedLeft (bounds.getWidth() - 16).reduced (2, 5) : juce::Rectangle<int>() });
    }
    return result;
}

void MsegTabBar::paint (juce::Graphics& g)
{
    const auto all = tabs();
    for (int m = 0; m < (int) all.size(); ++m)
    {
        const auto& tab = all[(size_t) m];
        const auto r = tab.bounds.toFloat();
        const bool isSelected = m == selected, hot = m == hoverTab;
        g.setColour (isSelected ? kPanel : hot ? kPanel.brighter (0.05f) : kBackground);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (isSelected ? kModAccent : kOutline);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, isSelected ? 1.5f : 1.0f);

        auto content = tab.bounds.reduced (5, 0);
        if (! tab.close.isEmpty())
            content.removeFromRight (tab.close.getWidth());
        // Routes: a count pill on wide tabs, a dot on narrow ones.
        if (routes[(size_t) m] > 0)
        {
            const bool wide = content.getWidth() >= 54;
            const auto pill = (wide ? content.removeFromRight (18) : content.removeFromRight (7)).toFloat()
                                  .withSizeKeepingCentre (wide ? 18.0f : 6.0f, wide ? 14.0f : 6.0f);
            g.setColour (kModAccent.withAlpha (wide ? 0.3f : 1.0f));
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
            if (wide)
            {
                g.setColour (kText);
                g.setFont (juce::Font (juce::FontOptions (10.0f)).boldened());
                g.drawText (juce::String (routes[(size_t) m]), pill, juce::Justification::centred);
            }
        }
        g.setColour (isSelected ? kText : kTextDim);
        g.setFont (juce::Font (juce::FontOptions (11.5f)).boldened());
        const auto label = content.getWidth() >= 52 ? "MSEG " + juce::String (m + 1) : juce::String (m + 1);
        g.drawText (label, content, juce::Justification::centred);

        if (! tab.close.isEmpty())
        {
            const auto x = tab.close.toFloat().reduced (2.0f);
            g.setColour (hoverClose && hot ? kText : kTextDim);
            g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.4f);
            g.drawLine (x.getX(), x.getBottom(), x.getRight(), x.getY(), 1.4f);
        }
    }

    const auto add = addButtonBounds();
    if (! add.isEmpty())
    {
        g.setColour (hoverAdd ? kPanel.brighter (0.08f) : kBackground);
        g.fillRoundedRectangle (add.toFloat(), 4.0f);
        g.setColour (hoverAdd ? kModAccent : kOutline);
        g.drawRoundedRectangle (add.toFloat().reduced (0.5f), 4.0f, 1.0f);
        g.setColour (hoverAdd ? kText : kTextDim);
        const auto c = add.toFloat().getCentre();
        g.drawLine (c.x - 5.0f, c.y, c.x + 5.0f, c.y, 1.6f);
        g.drawLine (c.x, c.y - 5.0f, c.x, c.y + 5.0f, 1.6f);
    }
}

void MsegTabBar::mouseMove (const juce::MouseEvent& event)
{
    int tab = -1;
    bool close = false;
    const auto all = tabs();
    for (int m = 0; m < (int) all.size(); ++m)
        if (all[(size_t) m].bounds.contains (event.getPosition()))
        {
            tab = m;
            // The x appears on hover, so test against the hovered layout.
            close = count > 1 && all[(size_t) m].bounds.withTrimmedLeft (all[(size_t) m].bounds.getWidth() - 16).contains (event.getPosition());
        }
    const bool add = addButtonBounds().contains (event.getPosition());
    if (tab != hoverTab || close != hoverClose || add != hoverAdd)
    {
        hoverTab = tab;
        hoverClose = close;
        hoverAdd = add;
        repaint();
    }
}

void MsegTabBar::mouseExit (const juce::MouseEvent&)
{
    hoverTab = -1;
    hoverClose = hoverAdd = false;
    repaint();
}

void MsegTabBar::mouseDown (const juce::MouseEvent& event)
{
    mouseMove (event);
    if (hoverAdd)
    {
        if (onAdd != nullptr)
            onAdd();
        return;
    }
    if (hoverTab < 0)
        return;
    if (hoverClose)
    {
        if (onRemove != nullptr)
            onRemove (hoverTab);
    }
    else if (onSelect != nullptr)
        onSelect (hoverTab);
}

juce::String MsegTabBar::getTooltip()
{
    if (hoverAdd)
        return "Add an MSEG (up to " + juce::String (kMaxMsegs) + ")";
    if (hoverTab < 0)
        return {};
    const int n = routes[(size_t) hoverTab];
    const auto name = "MSEG " + juce::String (hoverTab + 1);
    if (hoverClose)
        return "Delete " + name + (n > 0 ? " and its " + juce::String (n) + (n == 1 ? " slot" : " slots") : juce::String())
             + "; the MSEGs after it move down one place (undo restores it)";
    return name + ": " + (n > 0 ? juce::String (n) + (n == 1 ? " active slot" : " active slots") : juce::String ("not routed"));
}

//==============================================================================
ModPage::ModPage (juce::AudioProcessorValueTreeState& s, SynthEngine& e, Layer l, juce::Colour a, const ControlFactory& makeControl,
                  const SwitchedCellRegistrar& registerSwitched)
    : state (s), engine (e), layer (l), accent (a), slotList (e.modMatrix (l), a)
{
    setWantsKeyboardFocus (true);
    // Every MSEG a patch can have is built once; the tabs show the patch's ones.
    for (int m = 0; m < kMaxMsegs; ++m)
    {
        msegs[(size_t) m] = std::make_unique<MsegPanel> (e, l, m, a, makeControl, registerSwitched);
        addChildComponent (*msegs[(size_t) m]);
    }
    tabs.onSelect = [this] (int index) { selectMseg (index); };
    tabs.onAdd = [this] { addMseg(); };
    tabs.onRemove = [this] (int index) { removeMseg (index); };
    addAndMakeVisible (tabs);

    std::vector<ModSource> paletteOrder { ModSource::env1, ModSource::env2 };
    for (int m = 0; m < kMaxMsegs; ++m)
        paletteOrder.push_back (msegSource (m));
    for (auto source : { ModSource::lfo1, ModSource::lfo2, ModSource::dds2, ModSource::velocity, ModSource::noteNumber,
                         ModSource::random, ModSource::polyAftertouch, ModSource::perNotePitch, ModSource::timbre,
                         ModSource::modWheel, ModSource::expression, ModSource::ribbon })
        paletteOrder.push_back (source);
    for (int m = 0; m < ModMatrixData::kMacros; ++m)
        paletteOrder.push_back ((ModSource) ((int) ModSource::macro1 + m));
    for (auto source : paletteOrder)
    {
        auto chip = std::make_unique<ModSourceChip> (source, a);
        chip->setTooltip (juce::String (ModMatrix::getSourceName (source)) + ": click to add a slot, or drag it into the list");
        chip->onClick = [this, source] { slotList.addSlotWithSource (source); };
        addChildComponent (*chip);
        chips.push_back (std::move (chip));
    }
    viewport.setViewedComponent (&slotList, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    addButton.setTooltip ("Add an empty modulation slot");
    addButton.onClick = [this]
    {
        auto& matrix = engine.modMatrix (layer);
        const int slot = ModulationHost::findEmptySlot (matrix);
        if (slot >= 0)
            matrix.setSlot (slot, { (int) ModSource::env1, -1, 0.5f, 0.0f, false });
    };
    clearButton.setTooltip ("Remove every modulation slot of this layer");
    clearButton.onClick = [this]
    {
        auto& matrix = engine.modMatrix (layer);
        for (int i = ModMatrixData::kMaxSlots - 1; i >= 0; --i)
            if (matrix.getSlot (i).source >= 0 || matrix.getSlot (i).destination >= 0)
                matrix.removeSlot (i);
    };
    countLabel.setJustificationType (juce::Justification::centredRight);
    countLabel.setColour (juce::Label::textColourId, kTextDim);
    countLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (auto* c : { (juce::Component*) &addButton, (juce::Component*) &clearButton, (juce::Component*) &countLabel })
        addAndMakeVisible (*c);
    timerCallback();
}

void ModPage::selectMseg (int index)
{
    const int count = engine.modMatrix (layer).getMsegCount();
    selectedMseg = juce::jlimit (0, count - 1, index);
    for (int m = 0; m < kMaxMsegs; ++m)
        msegs[(size_t) m]->setVisible (m == selectedMseg);
    timerCallback();
}

void ModPage::copyMsegParameters (int from, int to)
{
    for (auto field : { MsegField::mode, MsegField::sync, MsegField::time, MsegField::division })
    {
        auto* source = state.getParameter (layerParamId (layer, msegSuffix (from, field)));
        auto* target = state.getParameter (layerParamId (layer, msegSuffix (to, field)));
        target->beginChangeGesture();
        target->setValueNotifyingHost (source->getValue());
        target->endChangeGesture();
    }
}

void ModPage::resetMsegParameters (int index)
{
    for (auto field : { MsegField::mode, MsegField::sync, MsegField::time, MsegField::division })
    {
        auto* parameter = state.getParameter (layerParamId (layer, msegSuffix (index, field)));
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getDefaultValue());
        parameter->endChangeGesture();
    }
}

int ModPage::addMseg()
{
    const int index = engine.modMatrix (layer).addMseg();
    if (index >= 0)
    {
        resetMsegParameters (index);
        selectMseg (index);
    }
    return index;
}

void ModPage::removeMseg (int index)
{
    auto& matrix = engine.modMatrix (layer);
    const int count = matrix.getMsegCount();
    if (count <= 1 || index < 0 || index >= count)
        return;
    // Shapes and routes move inside the matrix; the timing parameters move the same way.
    for (int m = index; m + 1 < count; ++m)
        copyMsegParameters (m + 1, m);
    resetMsegParameters (count - 1);
    matrix.removeMseg (index);
    selectMseg (juce::jmin (index, count - 2));
}

void ModPage::visibilityChanged()
{
    if (isVisible())
    {
        timerCallback();
        startTimerHz (30);
    }
    else
    {
        stopTimer();
    }
}

void ModPage::timerCallback()
{
    auto& matrix = engine.modMatrix (layer);
    const auto data = matrix.snapshot();
    juce::StringArray macroNames;
    for (int m = 0; m < ModMatrixData::kMacros; ++m)
        macroNames.add (matrix.getMacroName (m));

    // A preset load or an undo can change the number of MSEGs.
    const int count = data->msegCount;
    if (count != shownMsegCount)
    {
        shownMsegCount = count;
        selectedMseg = juce::jlimit (0, count - 1, selectedMseg);
        for (int m = 0; m < kMaxMsegs; ++m)
            msegs[(size_t) m]->setVisible (m == selectedMseg);
        for (auto& chip : chips)
        {
            const int mseg = msegIndexOf (chip->getSource());
            chip->setVisible (mseg < count);
        }
        resized();
    }

    std::array<int, kMaxMsegs> routes {};
    for (int k = 0; k < data->activeSlotCount; ++k)
    {
        const int mseg = msegIndexOf ((ModSource) data->slots[(size_t) data->activeSlots[(size_t) k]].source);
        if (mseg >= 0)
            ++routes[(size_t) mseg];
    }
    tabs.setState (count, selectedMseg, routes);
    msegs[(size_t) selectedMseg]->refresh (*data);
    slotList.refresh (macroNames, count);
    for (auto& chip : chips)
    {
        const auto source = chip->getSource();
        chip->setInUse (data->sourceUsed[(size_t) source]);
        if ((int) source >= (int) ModSource::macro1 && (int) source <= (int) ModSource::macro8)
            chip->setLabel (macroNames[(int) source - (int) ModSource::macro1]);
    }
    countLabel.setText (juce::String (data->activeSlotCount) + " / " + juce::String (ModMatrixData::kMaxSlots) + " active",
                        juce::dontSendNotification);
    slotList.setSize (viewport.getMaximumVisibleWidth(), slotList.getHeight());
}

void ModPage::paint (juce::Graphics& g)
{
    SectionPanel::paintFrame (g, matrixFrame, accent, "Modulation");
    g.setColour (kTextDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("SOURCES  -  click to add a slot, drag into the list; on the SYNTH and FX pages drag a chip onto a knob",
                paletteArea.withHeight (14), juce::Justification::centredLeft);

    auto header = listHeader;
    header.removeFromLeft (30);
    g.setFont (juce::Font (juce::FontOptions (10.5f)).boldened());
    auto column = [&] (juce::Rectangle<int> r, const char* text, juce::Justification j = juce::Justification::centredLeft)
    {
        g.drawText (text, r, j);
    };
    auto right = header;
    right.removeFromRight (viewport.getScrollBarThickness());
    right.removeFromRight (24 + kListColumnGap);
    column (right.removeFromRight (40), "+/-", juce::Justification::centred);
    right.removeFromRight (kListColumnGap);
    column (right.removeFromRight (26).expanded (14, 0), "CURVE", juce::Justification::centred);
    right.removeFromRight (kListColumnGap);
    column (right.removeFromRight (150), "AMOUNT");
    column (header.removeFromLeft (118 + kListColumnGap * 3 + 6), "SOURCE");
    column (header, "DESTINATION");
    g.setColour (kOutline);
    g.drawHorizontalLine (listHeader.getBottom(), (float) listHeader.getX(), (float) listHeader.getRight());
}

void ModPage::layoutPalette()
{
    // Chips flow left to right; the palette grows by a row when the MSEG chips need one.
    auto chipsArea = paletteArea.withTrimmedTop (18);
    int x = chipsArea.getX(), y = chipsArea.getY();
    for (auto& chip : chips)
    {
        if (! chip->isVisible())
            continue;
        const int w = chip->getPreferredWidth();
        if (x + w > chipsArea.getRight())
        {
            x = chipsArea.getX();
            y += 26;
        }
        chip->setBounds (x, y, w, 22);
        x += w + 5;
    }
    paletteArea.setBottom (y + 22);
}

void ModPage::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft (juce::roundToInt ((float) area.getWidth() * 0.5f)).reduced (2);
    tabs.setBounds (left.removeFromTop (MsegTabBar::kHeight));
    left.removeFromTop (2);
    for (auto& panel : msegs)
        panel->setBounds (left);

    matrixFrame = area.reduced (2);
    auto inner = matrixFrame.reduced (8, 3);
    auto title = inner.removeFromTop (SectionPanel::kTitleHeight + 2);
    clearButton.setBounds (title.removeFromRight (84).reduced (0, 1));
    title.removeFromRight (8);
    countLabel.setBounds (title.removeFromRight (120));
    inner.removeFromTop (6);

    paletteArea = inner;
    layoutPalette();
    inner.setTop (paletteArea.getBottom());
    inner.removeFromTop (12);
    listHeader = inner.removeFromTop (20);
    inner.removeFromTop (4);
    auto footer = inner.removeFromBottom (30);
    addButton.setBounds (footer.removeFromLeft (120).reduced (0, 2));
    inner.removeFromBottom (4);
    viewport.setBounds (inner);
    slotList.setSize (viewport.getMaximumVisibleWidth(), juce::jmax (slotList.getHeight(), ModSlotRow::kHeight));
    slotList.resized();
}
