#include "PluginEditor.h"
#include "UiDialogs.h"

using namespace nsynth;

using namespace nSynth::ui;

namespace
{

juce::Colour accentForLayer (Layer l) { return l == Layer::upper ? kUpperAccent : kLowerAccent; }
juce::String layerName (Layer l) { return l == Layer::upper ? "UPPER" : "LOWER"; }

// Header and layer bar (design pixels).
constexpr int kHeaderHeight = 40, kPresetBarMaxWidth = 580, kHeaderButtonHeight = 28;
constexpr int kUndoButtonWidth = 54;
constexpr int kPerformanceStripHeight = 84;
constexpr int kLayerBarHeight = 34, kLayerTabWidth = 290, kLayerActionWidth = 92, kSavePatchWidth = 104;
// SYNTH page: the sources and modulator rows; the third row takes the rest.
constexpr int kSourceRowHeight = 222, kModulatorRowHeight = 184;

// Modulation menu ids above kModulationMenuBase: + source adds a route, + kRemoveSlotItems + slot
// removes one, kRemoveAllItem clears the parameter, kOpenModPageItem shows the MOD page.
constexpr int kRemoveSlotItems = 100, kRemoveAllItem = 200, kOpenModPageItem = 300;

// Editor timer (30 Hz): modulation rings every tick, the rest every kRefreshDivider ticks, undo
// points every kUndoCaptureDivider ticks (~270 ms); the MIDI overflow warning stays up ~3 s.
constexpr int kTimerHz = 30, kRefreshDivider = 3, kUndoCaptureDivider = 8, kMidiWarningRefreshes = 30;
} // namespace

//==============================================================================
NSynthAudioProcessorEditor::NSynthAudioProcessorEditor (NSynthAudioProcessor& p)
    : AudioProcessorEditor (p), synth (p)
{
    setLookAndFeel (&lnf);
    // Keyboard shortcuts reach the editor from any child that does not use the key itself; a click
    // on the background takes the focus away from the last knob.
    setWantsKeyboardFocus (true);
    content.setWantsKeyboardFocus (true);
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
                     &loadPatch, &savePatch, &comparePatch, &synthTab, &modTab, &fxTab, &waveBrowserButton, &undoButton, &redoButton })
        content.addAndMakeVisible (*b);

    prevPreset.setTooltip ("Previous preset (of the browser list, or of the loaded preset's category)");
    nextPreset.setTooltip ("Next preset (of the browser list, or of the loaded preset's category)");
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    undoButton.setTooltip ("Undo the last edit (Cmd/Ctrl+Z)");
    redoButton.setTooltip ("Redo (Cmd/Ctrl+Shift+Z)");
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
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
    initLayer.onClick = [this]
    {
        const auto layer = bottomLayer;
        confirmLayerChange (layer, "Init layer", "Reset it to the init patch anyway?", "Reset",
                            [this, layer] { synth.initLayer (layer); });
    };
    copyUpToLow.onClick = [this]
    {
        confirmLayerChange (Layer::lower, "Copy layer", "Replace it with a copy of UPPER anyway?", "Copy",
                            [this] { synth.copyLayer (Layer::upper, Layer::lower); });
    };
    copyLowToUp.onClick = [this]
    {
        confirmLayerChange (Layer::upper, "Copy layer", "Replace it with a copy of LOWER anyway?", "Copy",
                            [this] { synth.copyLayer (Layer::lower, Layer::upper); });
    };

    voiceMeter.setJustificationType (juce::Justification::centredRight);
    voiceMeter.setColour (juce::Label::textColourId, kTextDim);
    content.addAndMakeVisible (voiceMeter);

    // SYNTH page: three rows in signal-flow order. Columns are chosen so every section fits in
    // two rows of controls; the weight is the section's share of the row width (the oscillators'
    // wave menus need more room than a knob).
    struct RowEntry { Section section; int columns; float weight; };
    const std::vector<RowEntry> rowLayout[3] = {
        { { Section::dds1, 2, 2.4f }, { Section::dds2, 2, 2.4f }, { Section::mixer, 1, 1.1f }, { Section::ddsMod, 6, 5.8f },
          { Section::vcf, 5, 5.6f }, { Section::vca, 2, 2.2f } },
        { { Section::env1, 4, 4.0f }, { Section::env2, 3, 3.0f }, { Section::lfo1, 3, 3.0f }, { Section::perfControl, 7, 7.0f } },
        { { Section::voiceAssign, 5, 5.0f }, { Section::filter2, 5, 5.0f }, { Section::macros, 4, 4.0f } } };

    const SwitchedCellRegistrar registerSwitched = [this] (ParamControl* whenOff, ParamControl* whenOn, const char* selector)
    {
        switchedCells.push_back ({ whenOff, whenOn, synth.getState().getRawParameterValue (layerParamId (whenOff->getLayer(), selector)) });
    };
    const ControlFactory factory = [this] (Layer layer, const char* suffix) { return makeControl (layer, suffix); };

    for (auto layer : { Layer::upper, Layer::lower })
    {
        auto& ui = layers[(size_t) toInt (layer)];
        const auto accent = accentForLayer (layer);
        buildingPage = Page::synth;
        for (size_t row = 0; row < 3; ++row)
            for (const auto& entry : rowLayout[row])
            {
                ui.rows[row].push_back (makeSection (layer, entry.section, entry.columns, accent));
                ui.rows[row].back()->setWidthWeight (entry.weight);
                content.addChildComponent (*ui.rows[row].back());
            }
        buildingPage = Page::mod;
        ui.modPage = std::make_unique<ModPage> (synth.getState(), synth.getEngine(), layer, accent, factory, registerSwitched);
        content.addChildComponent (*ui.modPage);
        buildingPage = Page::fx;
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
    updateUndoButtons();
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
    startTimerHz (kTimerHz);
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
    layers[(size_t) toInt (layer)].pageControls[(size_t) buildingPage].push_back (control.get());
    return control;
}

std::unique_ptr<SectionPanel> NSynthAudioProcessorEditor::makeSection (Layer layer, Section section, int columns, juce::Colour accent)
{
    // Two controls in one cell; the selector parameter (on = value > 0.5) decides which one is shown.
    struct SharedCell { const char* first; const char* second; const char* selector; bool secondShownWhenOn; };
    static constexpr SharedCell sharedCells[] = {
        { pid::velFixed, pid::velCurve, pid::velMode, false },      // Static: fixed velocity, Dynamic: curve
    };

    auto panel = std::make_unique<SectionPanel> (SectionPanel::titleFor (section), accent, columns);
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
    refreshControls();
}

void NSynthAudioProcessorEditor::selectBottomLayer (Layer layer)
{
    bottomLayer = layer;
    if (presetBrowser != nullptr && presetBrowser->getTargetLayer() != layer)
        presetBrowser->setTargetLayer (layer);
    if (waveBrowserPage != nullptr)
        waveBrowserPage->setLayer (layer);
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
    auto& matrix = synth.getEngine().modMatrix (layer);
    // The patch's MSEGs in their own submenu, the other sources in enum order.
    juce::PopupMenu add, msegMenu;
    const int msegCount = matrix.getMsegCount();
    for (int m = 0; m < msegCount; ++m)
        msegMenu.addItem (kModulationMenuBase + (int) msegSource (m), sourceName (matrix, (int) msegSource (m)));
    for (int source = 0; source < (int) ModSource::count; ++source)
    {
        if (msegIndexOf ((ModSource) source) == 0)
            add.addSubMenu ("MSEG", msegMenu);
        if (msegIndexOf ((ModSource) source) < 0)
            add.addItem (kModulationMenuBase + source, sourceName (matrix, source));
    }
    menu.addSubMenu ("Modulate with", add);

    int routes = 0;
    for (int i = 0; i < ModMatrixData::kMaxSlots; ++i)
    {
        const auto slot = matrix.getSlot (i);
        if (slot.destination != parameterIndex || slot.source < 0)
            continue;
        menu.addItem (kModulationMenuBase + kRemoveSlotItems + i, "Remove " + sourceName (matrix, slot.source) + " ("
                      + (slot.amount > 0.0f ? "+" : "") + juce::String (juce::roundToInt (slot.amount * 100.0f)) + " %)");
        ++routes;
    }
    if (routes > 1)
        menu.addItem (kModulationMenuBase + kRemoveAllItem, "Remove all modulation");
    menu.addItem (kModulationMenuBase + kOpenModPageItem, "Open the MOD page");
}

void NSynthAudioProcessorEditor::handleModulationMenu (int result, Layer layer, int parameterIndex)
{
    auto& matrix = synth.getEngine().modMatrix (layer);
    const int id = result - kModulationMenuBase;
    if (id >= 0 && id < (int) ModSource::count)
        dropModulationSource ((ModSource) id, layer, parameterIndex);
    else if (id >= kRemoveSlotItems && id < kRemoveSlotItems + ModMatrixData::kMaxSlots)
        matrix.removeSlot (id - kRemoveSlotItems);
    else if (id == kRemoveAllItem)
        matrix.clearDestination (parameterIndex);
    else if (id == kOpenModPageItem)
        showPage (Page::mod);
    updateModulationDisplay();
}

void NSynthAudioProcessorEditor::dropModulationSource (ModSource source, Layer layer, int parameterIndex)
{
    // LFO-like sources swing around the knob; the others push it one way.
    const bool centred = isCentredSource (source);
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
    for (auto* control : layerUi.pageControls[(size_t) page])
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

    auto header = r.removeFromTop (kHeaderHeight);
    title.setBounds (header.removeFromLeft (100));
    for (auto* tab : { &synthTab, &modTab, &fxTab })
        tab->setBounds (header.removeFromLeft (68).withSizeKeepingCentre (68, kHeaderButtonHeight));
    header.removeFromLeft (12);
    undoButton.setBounds (header.removeFromLeft (kUndoButtonWidth).withSizeKeepingCentre (kUndoButtonWidth, kHeaderButtonHeight));
    header.removeFromLeft (4);
    redoButton.setBounds (header.removeFromLeft (kUndoButtonWidth).withSizeKeepingCentre (kUndoButtonWidth, kHeaderButtonHeight));
    header.removeFromLeft (8);
    sizeBox.setBounds (header.removeFromRight (84).withSizeKeepingCentre (84, 26));
    header.removeFromRight (8);
    waveBrowserButton.setBounds (header.removeFromRight (94).withSizeKeepingCentre (94, kHeaderButtonHeight));
    header.removeFromRight (8);
    voiceMeter.setBounds (header.removeFromRight (140));
    auto bar = header.withSizeKeepingCentre (juce::jmin (kPresetBarMaxWidth, header.getWidth() - 16), 30);
    prevPreset.setBounds (bar.removeFromLeft (30));
    savePreset.setBounds (bar.removeFromRight (62));
    bar.removeFromRight (6);
    nextPreset.setBounds (bar.removeFromRight (30));
    presetDisplay.setBounds (bar.reduced (4, 0));
    waveBrowserPage->setBounds (juce::Rectangle<int> (0, 0, kDesignWidth, kDesignHeight));
    presetBrowser->setBounds (juce::Rectangle<int> (0, 0, kDesignWidth, kDesignHeight));
    r.removeFromTop (4);

    performancePanel->setBounds (r.removeFromTop (kPerformanceStripHeight));
    r.removeFromTop (6);

    // Layer tabs with the loaded patch names, then the layer actions.
    auto tabs = r.removeFromTop (kLayerBarHeight);
    upperSelect.setBounds (tabs.removeFromLeft (kLayerTabWidth));
    tabs.removeFromLeft (4);
    lowerSelect.setBounds (tabs.removeFromLeft (kLayerTabWidth));
    tabs.removeFromLeft (12);
    for (auto* b : { &savePatch, &loadPatch, &copyLowToUp, &copyUpToLow, &comparePatch, &initLayer })
    {
        b->setBounds (tabs.removeFromRight (b == &savePatch ? kSavePatchWidth : kLayerActionWidth).reduced (0, 3));
        tabs.removeFromRight (5);
    }
    layerHint.setBounds (tabs);
    r.removeFromTop (5);

    auto layoutRow = [] (juce::Rectangle<int> area, std::vector<SectionPanel*> sections)
    {
        float totalWeight = 0.0f;
        for (auto* s : sections)
            totalWeight += s->getWidthWeight();
        const float colW = (float) area.getWidth() / totalWeight;
        float x = (float) area.getX();
        for (auto* s : sections)
        {
            const float w = colW * s->getWidthWeight();
            s->setBounds (juce::Rectangle<float> (x, (float) area.getY(), w, (float) area.getHeight()).toNearestInt().reduced (2));
            x += w;
        }
    };

    const int rowHeights[] = { kSourceRowHeight, kModulatorRowHeight, 0 };
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
    const auto layer = bottomLayer;   // the layer shown when the dialog opened
    const auto directory = synth.getPresetLibrary().getUserDirectory (kind);
    directory.createDirectory();
    const auto suggested = patch ? synth.getPatchName (layer) : synth.getCurrentPresetName();
    chooser = std::make_unique<juce::FileChooser> (patch ? "Save layer patch" : "Save performance",
                                                   directory.getChildFile (juce::File::createLegalFileName (suggested)),
                                                   patch ? "*.nsynthpatch" : "*.nsynthperf");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, patch, layer] (const juce::FileChooser& fc)
    {
        const auto chosen = fc.getResult();
        if (chosen == juce::File())
            return;
        // The chooser confirmed `chosen`; adding the extension can name another, existing file.
        const auto target = chosen.withFileExtension (patch ? "nsynthpatch" : "nsynthperf");
        confirmThen (target != chosen && target.exists(), messageBox, this, "Replace preset",
                     "\"" + target.getFileName() + "\" already exists. Replace it?", "Replace",
                     [this, patch, layer, target] { writePreset (patch, layer, target); });
    });
}

void NSynthAudioProcessorEditor::writePreset (bool patch, Layer layer, const juce::File& file)
{
    juce::String error;
    const bool ok = patch ? synth.savePatch (layer, file, &error) : synth.savePerformance (file, &error);
    if (! ok)
        messageBox = juce::AlertWindow::showScopedAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                                                                                                 "Save failed", error, {}, this),
                                                         nullptr);
    if (presetBrowser->isOpen())
        presetBrowser->refresh();
    refreshDisplay();
}

void NSynthAudioProcessorEditor::confirmLayerChange (Layer target, const juce::String& dialogTitle, const juce::String& question,
                                                     const juce::String& confirmText, std::function<void()> change)
{
    confirmThen (synth.isPatchDirty (target), messageBox, this, dialogTitle,
                 layerName (target) + " has unsaved changes. " + question, confirmText,
                 [this, change = std::move (change)]
                 {
                     change();
                     refreshDisplay();
                 });
}

void NSynthAudioProcessorEditor::stepPreset (int delta)
{
    presetBrowser->step (delta);
    refreshDisplay();
}

void NSynthAudioProcessorEditor::undo()
{
    if (synth.undo())
    {
        updateSwitchedControls();
        refreshControls();
        refreshDisplay();
    }
    updateUndoButtons();
}

void NSynthAudioProcessorEditor::redo()
{
    if (synth.redo())
    {
        updateSwitchedControls();
        refreshControls();
        refreshDisplay();
    }
    updateUndoButtons();
}

void NSynthAudioProcessorEditor::updateUndoButtons()
{
    undoButton.setEnabled (synth.canUndo());
    redoButton.setEnabled (synth.canRedo());
}

bool NSynthAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Text fields and focused knobs use their keys first; only what they leave arrives here.
    const auto mods = key.getModifiers();
    if (! mods.isCommandDown() || mods.isAltDown())
        return false;
    const int code = key.getKeyCode();
    const auto letter = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);
    if (letter == 'Z')
    {
        if (mods.isShiftDown())
            redo();
        else
            undo();
        return true;
    }
    if (letter == 'Y' && ! mods.isShiftDown())
    {
        redo();
        return true;
    }
    if ((code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) && ! mods.isShiftDown())
    {
        stepPreset (code == juce::KeyPress::rightKey ? 1 : -1);
        return true;
    }
    return false;
}

void NSynthAudioProcessorEditor::refreshDisplay() noexcept
{
    // Each dirty check compares the whole layer: once per refresh.
    const std::array<bool, 2> patchDirty { synth.isPatchDirty (Layer::upper), synth.isPatchDirty (Layer::lower) };
    if (presetBrowser->getKind() == nSynth::PresetKind::patch)
        presetDisplay.setContent (layerName (bottomLayer),
                                  synth.getPatchName (bottomLayer) + (patchDirty[(size_t) toInt (bottomLayer)] ? " *" : ""),
                                  synth.getPatchCategory (bottomLayer), accentForLayer (bottomLayer));
    else
        presetDisplay.setContent ("PERF", synth.getCurrentPresetName() + (synth.isPerformanceDirty() ? " *" : ""),
                                  "U: " + synth.getPatchName (Layer::upper) + "  L: " + synth.getPatchName (Layer::lower),
                                  kPerfAccent);
    comparePatch.setToggleState (synth.isPatchCompareActive (bottomLayer), juce::dontSendNotification);

    for (auto [button, layer] : { std::pair { &upperSelect, Layer::upper }, std::pair { &lowerSelect, Layer::lower } })
    {
        const auto text = layerName (layer) + "   " + synth.getPatchName (layer) + (patchDirty[(size_t) toInt (layer)] ? " *" : "");
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

    // Dropped MIDI-mapped parameter changes: a warning in place of the voice count for a while.
    const bool midiWarning = midiWarningTicks > 0;
    voiceMeter.setColour (juce::Label::textColourId, midiWarning ? kNegative : kTextDim);
    voiceMeter.setTooltip (midiWarning ? "Some MIDI-mapped parameter updates were dropped because the control queue was full."
                                       : juce::String());
    voiceMeter.setText (midiWarning ? juce::String ("MIDI overflow")
                                    : "Voices  U " + juce::String (synth.getActiveVoices (Layer::upper))
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

void NSynthAudioProcessorEditor::refreshControls()
{
    // Only the shown page of the shown layer and the performance strip; the others catch up when
    // they are shown (the MIDI learn generation tells them whether their CC changed meanwhile).
    const auto generation = synth.getEngine().getMidiLearnGeneration();
    if (performancePanel != nullptr)
        for (auto& control : performancePanel->getControls())
            control->refresh (generation);
    for (auto* control : layers[(size_t) toInt (bottomLayer)].pageControls[(size_t) page])
        control->refresh (generation);
}

void NSynthAudioProcessorEditor::timerCallback()
{
    // Modulation rings at the full timer rate, everything else at 10 Hz, undo points every ~270 ms.
    updateModulationDisplay();
    ++timerTicks;
    if (timerTicks % kUndoCaptureDivider == 0)
    {
        synth.captureUndoPoint();
        updateUndoButtons();
    }
    if (timerTicks % kRefreshDivider != 0)
        return;
    updateSwitchedControls();
    refreshControls();
    const auto dropped = synth.getEngine().getDroppedMidiParameterChanges();
    if (dropped != lastMidiParameterDrops)
    {
        lastMidiParameterDrops = dropped;
        midiWarningTicks = kMidiWarningRefreshes;
    }
    else if (midiWarningTicks > 0)
    {
        --midiWarningTicks;
    }
    refreshDisplay();
}
