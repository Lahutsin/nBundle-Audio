#include "FxPage.h"
#include "Core/DSP/Effects.h"

using namespace nsynth;
using namespace nSynth::ui;

namespace
{
struct CardLayout
{
    const char* power;                       // nullptr: the chorus
    int columns;
    std::vector<const char*> cells;          // nullptr = spacer
    int visualHeight;                        // curve / meter above the controls
};

CardLayout layoutFor (FxModule module)
{
    switch (module)
    {
        case FxModule::distortion:
            return { pid::distOn, 4, { pid::distType, pid::distDrive, pid::distTone, pid::distMix }, 96 };
        case FxModule::eq:
            // One column per band: low shelf, peak, high shelf.
            return { pid::eqOn, 3, { pid::eqLowFreq, pid::eqMidFreq, pid::eqHighFreq,
                                     pid::eqLowGain, pid::eqMidGain, pid::eqHighGain,
                                     pid::eqLowQ, pid::eqMidQ, pid::eqHighQ }, 44 };
        case FxModule::compressor:
            return { pid::compOn, 4, { pid::compThreshold, pid::compRatio, pid::compAttack, pid::compRelease,
                                       pid::compMakeup, pid::compKey, pid::compMix, nullptr }, 22 };
        case FxModule::chorus:
            return { nullptr, 4, { pid::chorus, pid::chorusRate, pid::chorusDepth, pid::chorusDelay,
                                   pid::chorusFeedback, pid::chorusWidth, pid::chorusMix, nullptr }, 0 };
        case FxModule::phaser:
            return { pid::phaserOn, 4, { pid::phaserRate, pid::phaserDepth, pid::phaserFeedback, pid::phaserMix }, 0 };
        case FxModule::flanger:
            return { pid::flangerOn, 4, { pid::flangerRate, pid::flangerDepth, pid::flangerFeedback, pid::flangerMix }, 0 };
        case FxModule::delay:
            return { pid::delayOn, 5, { pid::delaySync, pid::delayMs, pid::delayDivision, pid::delayFeedback, pid::delayMix,
                                        pid::delayDuck, pid::delayPingPong, pid::delayFreeze, pid::delayLowCut,
                                        pid::delayHighCut, nullptr }, 0 };
        case FxModule::reverb:
            return { pid::reverbOn, 5, { pid::reverbMix, pid::reverbSize, pid::reverbDecay, pid::reverbPreDelay, pid::reverbDuck,
                                         pid::reverbDamping, pid::reverbLowCut, pid::reverbWidth, pid::reverbModulation,
                                         nullptr }, 0 };
        case FxModule::count:
            break;
    }
    return {};
}

constexpr int kMaxCellHeight = 104, kMaxCellWidth = 92;

// Parameters the card's curve is drawn from (it repaints when one of them changes).
const std::vector<const char*>& curveParameters (FxModule module)
{
    static const std::vector<const char*> eq { pid::eqLowFreq, pid::eqLowGain, pid::eqMidFreq, pid::eqMidGain,
                                               pid::eqMidQ, pid::eqHighFreq, pid::eqHighGain, pid::eqLowQ, pid::eqHighQ };
    static const std::vector<const char*> distortion { pid::distType, pid::distDrive };
    static const std::vector<const char*> none;
    return module == FxModule::eq ? eq : module == FxModule::distortion ? distortion : none;
}
} // namespace

//==============================================================================
FxCard::FxCard (juce::AudioProcessorValueTreeState& s, SynthEngine& e, Layer l, FxModule m, juce::Colour a,
                const ControlFactory& makeControl, const SwitchedCellRegistrar& registerSwitched)
    : state (s), engine (e), layer (l), module (m), accent (a)
{
    // A click on the card takes the keyboard focus away from the last knob.
    setWantsKeyboardFocus (true);
    for (auto* suffix : curveParameters (m))
        curveValues.push_back (state.getRawParameterValue (layerParamId (l, suffix)));
    shownCurve.assign (curveValues.size(), 0.0f);
    const auto layout = layoutFor (m);
    columns = layout.columns;
    if (layout.power != nullptr)
    {
        power = makeControl (l, layout.power);
        power->setCaptionVisible (false);
        power->setTooltip (juce::String (ModMatrix::getFxModuleName (m)) + " on/off");
        addAndMakeVisible (*power);
    }
    else
    {
        // CHORUS: the switch toggles MODE between Off and the last mode used.
        chorusPower.setTooltip ("Chorus on/off (MODE)");
        chorusPower.setColour (juce::ToggleButton::tickColourId, accent);
        chorusPower.onClick = [this]
        {
            auto* parameter = state.getParameter (layerParamId (layer, pid::chorus));
            const int mode = (int) state.getRawParameterValue (layerParamId (layer, pid::chorus))->load();
            if (mode != 0)
                chorusLastMode = mode;
            const int next = mode != 0 ? 0 : chorusLastMode;
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) next));
            parameter->endChangeGesture();
        };
        addAndMakeVisible (chorusPower);
    }

    ParamControl* previous = nullptr;
    for (auto* suffix : layout.cells)
    {
        if (suffix == nullptr)
        {
            cells.push_back ({});
            continue;
        }
        auto control = makeControl (l, suffix);
        if (juce::String (suffix) == pid::delayDivision && previous != nullptr)
        {
            // Delay TIME: milliseconds or a division, depending on SYNC.
            registerSwitched (previous, control.get(), pid::delaySync);
            cells.back().push_back (control.get());
        }
        else
        {
            cells.push_back ({ control.get() });
        }
        previous = control.get();
        addAndMakeVisible (*control);
        controls.push_back (std::move (control));
    }

    for (auto* b : { &moveLeft, &moveRight })
    {
        b->setConnectedEdges (b == &moveLeft ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
        addAndMakeVisible (*b);
    }
    moveLeft.setTooltip ("Move earlier in the chain");
    moveRight.setTooltip ("Move later in the chain");
    moveLeft.onClick = [this] { if (onMove != nullptr) onMove (chainPosition, chainPosition - 1); };
    moveRight.onClick = [this] { if (onMove != nullptr) onMove (chainPosition, chainPosition + 1); };
}

float FxCard::raw (const char* suffix) const
{
    return state.getRawParameterValue (layerParamId (layer, suffix))->load();
}

void FxCard::setChainPosition (int position)
{
    chainPosition = position;
    moveLeft.setEnabled (position > 0);
    moveRight.setEnabled (position < ModMatrixData::kFxModules - 1);
    repaint();
}

bool FxCard::isModuleOn() const noexcept
{
    const auto layout = layoutFor (module);
    return layout.power != nullptr ? raw (layout.power) > 0.5f : raw (pid::chorus) > 0.5f;
}

void FxCard::refresh()
{
    const bool on = isModuleOn();
    if (power == nullptr)
        chorusPower.setToggleState (on, juce::dontSendNotification);
    bool dirty = on != shownOn;
    shownOn = on;

    if (module == FxModule::compressor)
    {
        const float reduction = engine.layer (layer).getCompressorReductionDb();
        dirty |= std::abs (reduction - shownReduction) > 0.05f;
        shownReduction = reduction;
    }
    for (size_t i = 0; i < curveValues.size(); ++i)
    {
        const float value = curveValues[i]->load();
        dirty |= ! juce::exactlyEqual (value, shownCurve[i]);
        shownCurve[i] = value;
    }
    if (dirty)
        repaint();
}

void FxCard::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (kPanel);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (dropHover ? accent : kOutline);
    g.drawRoundedRectangle (bounds, 5.0f, dropHover ? 2.0f : 1.0f);
    g.setColour (shownOn ? accent : kOutline);
    g.fillRect (bounds.withHeight (2.5f).reduced (5.0f, 0.0f));

    auto header = getLocalBounds().reduced (8, 0).removeFromTop (kHeaderHeight).withTrimmedTop (3);
    const auto badge = header.removeFromLeft (20).withSizeKeepingCentre (20, 18).toFloat();
    g.setColour (shownOn ? accent.withAlpha (0.22f) : kBackground);
    g.fillRoundedRectangle (badge, 4.0f);
    g.setColour (shownOn ? accent : kTextDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)).boldened());
    g.drawText (juce::String (chainPosition + 1), badge, juce::Justification::centred);
    header.removeFromLeft (30);
    g.setColour (shownOn ? kText : kTextDim);
    g.setFont (juce::Font (juce::FontOptions (12.5f)).boldened());
    g.drawText (juce::String (ModMatrix::getFxModuleName (module)).toUpperCase(), header, juce::Justification::centredLeft);

    if (visual.isEmpty())
        return;
    const auto area = visual.toFloat();
    if (module == FxModule::eq)
        paintEqCurve (g, area);
    else if (module == FxModule::compressor)
        paintReductionMeter (g, area);
    else if (module == FxModule::distortion)
    {
        // Transfer curve of the shaper at the current drive.
        g.setColour (kBackground);
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (kOutline);
        g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
        g.drawVerticalLine (juce::roundToInt (area.getCentreX()), area.getY(), area.getBottom());
        const auto type = (DistortionType) juce::roundToInt (raw (pid::distType));
        const float drive = raw (pid::distDrive);
        const float gain = juce::Decibels::decibelsToGain (36.0f * drive), makeup = 1.0f / (1.0f + 1.5f * drive);
        const float steps = std::exp2 (15.0f - 13.0f * drive);
        juce::Path path;
        const auto plot = area.reduced (6.0f, 6.0f);
        for (int i = 0; i <= 120; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / 120.0f;
            float y = 0.0f;
            switch (type)
            {
                case DistortionType::soft:  y = std::tanh (x * gain) * makeup; break;
                case DistortionType::hard:  y = juce::jlimit (-1.0f, 1.0f, x * gain) * makeup; break;
                case DistortionType::tube:  y = (std::tanh (x * gain + 0.35f) - 0.33637554f) * makeup; break;
                case DistortionType::fold:  y = std::sin (juce::MathConstants<float>::halfPi * x * (1.0f + 0.25f * (gain - 1.0f))) * makeup; break;
                case DistortionType::crush: y = std::round (juce::jlimit (-1.0f, 1.0f, x * (1.0f + drive)) * steps) / steps; break;
            }
            const auto p = juce::Point<float> (plot.getX() + plot.getWidth() * (float) i / 120.0f,
                                               plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, y) * plot.getHeight() * 0.5f);
            if (i == 0) path.startNewSubPath (p);
            else        path.lineTo (p);
        }
        g.setColour (shownOn ? accent : kTextDim);
        g.strokePath (path, juce::PathStrokeType (1.8f));
    }
}

void FxCard::paintEqCurve (juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setColour (kBackground);
    g.fillRoundedRectangle (area, 4.0f);
    const auto plot = area.reduced (6.0f, 6.0f);
    g.setColour (kOutline);
    g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
    for (double hz : { 100.0, 1000.0, 10000.0 })
    {
        const float x = plot.getX() + plot.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }
    ThreeBandEq::Settings s { raw (pid::eqLowFreq), raw (pid::eqLowGain), raw (pid::eqMidFreq), raw (pid::eqMidGain),
                              raw (pid::eqMidQ), raw (pid::eqHighFreq), raw (pid::eqHighGain), raw (pid::eqLowQ), raw (pid::eqHighQ) };
    // The response at the processor's rate (the bands bend towards Nyquist), 48 kHz before prepare.
    const double processorRate = state.processor.getSampleRate();
    const double sampleRate = processorRate > 0.0 ? processorRate : 48000.0;
    juce::Path path;
    for (int i = 0; i <= 160; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, (double) i / 160.0);
        const float db = juce::jlimit (-18.0f, 18.0f, ThreeBandEq::responseDb (s, hz, sampleRate));
        const juce::Point<float> p (plot.getX() + plot.getWidth() * (float) i / 160.0f, plot.getCentreY() - db / 18.0f * plot.getHeight() * 0.5f);
        if (i == 0) path.startNewSubPath (p);
        else        path.lineTo (p);
    }
    g.setColour (shownOn ? accent : kTextDim);
    g.strokePath (path, juce::PathStrokeType (1.8f));
}

void FxCard::paintReductionMeter (juce::Graphics& g, juce::Rectangle<float> area) const
{
    auto r = area;
    g.setColour (kTextDim);
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    g.drawText ("GR", r.removeFromLeft (24.0f), juce::Justification::centredLeft);
    auto value = r.removeFromRight (52.0f);
    const auto bar = r.withSizeKeepingCentre (r.getWidth(), 8.0f);
    g.setColour (kBackground);
    g.fillRoundedRectangle (bar, 3.0f);
    const float fraction = juce::jlimit (0.0f, 1.0f, -shownReduction / 24.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * fraction), 3.0f);
    g.setColour (kText);
    g.drawText (juce::String (shownReduction, 1) + " dB", value, juce::Justification::centredRight);
}

void FxCard::paintOverChildren (juce::Graphics& g)
{
    if (shownOn)
        return;
    // Bypassed: the controls stay editable, dimmed.
    g.setColour (kPanel.withAlpha (0.45f));
    g.fillRect (getLocalBounds().withTrimmedTop (kHeaderHeight).reduced (2));
}

void FxCard::resized()
{
    auto area = getLocalBounds().reduced (6, 0);
    auto header = area.removeFromTop (kHeaderHeight).withTrimmedTop (3);
    header.removeFromLeft (24);
    auto powerArea = header.removeFromLeft (26);
    if (power != nullptr)
        power->setBounds (powerArea.withSizeKeepingCentre (24, 22));
    else
        chorusPower.setBounds (powerArea.withSizeKeepingCentre (24, 22));
    moveRight.setBounds (header.removeFromRight (24).withSizeKeepingCentre (24, 20));
    moveLeft.setBounds (header.removeFromRight (24).withSizeKeepingCentre (24, 20));

    area.removeFromBottom (4);
    const int visualHeight = layoutFor (module).visualHeight;
    visual = visualHeight > 0 ? area.removeFromTop (visualHeight).reduced (2, 2) : juce::Rectangle<int>();
    if (visualHeight > 0)
        area.removeFromTop (4);

    // Cells keep the same size on every card; the block is centred in the free space.
    const int n = (int) cells.size();
    const int rows = juce::jmax (1, (n + columns - 1) / columns);
    const int cw = juce::jmin (kMaxCellWidth, area.getWidth() / columns);
    const int rh = juce::jmin (kMaxCellHeight, area.getHeight() / rows);
    const auto block = area.withSizeKeepingCentre (cw * columns, rh * rows);
    for (int i = 0; i < n; ++i)
        for (auto* control : cells[(size_t) i])
            control->setBounds (block.getX() + (i % columns) * cw, block.getY() + (i / columns) * rh, cw, rh);
}

void FxCard::mouseDrag (const juce::MouseEvent& event)
{
    if (event.getMouseDownY() > kHeaderHeight || event.getDistanceFromDragStart() < 6)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! container->isDragAndDropActive())
            container->startDragging ("fxcard:" + juce::String (chainPosition), this);
}

bool FxCard::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith ("fxcard:") && details.sourceComponent != this;
}

void FxCard::itemDropped (const SourceDetails& details)
{
    dropHover = false;
    repaint();
    const int from = details.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    if (onMove != nullptr)
        onMove (from, chainPosition);
}

//==============================================================================
FxPage::FxPage (juce::AudioProcessorValueTreeState& state, SynthEngine& e, Layer l, juce::Colour accent,
                const ControlFactory& makeControl, const SwitchedCellRegistrar& registerSwitched)
    : engine (e), layer (l)
{
    setWantsKeyboardFocus (true);
    for (int m = 0; m < ModMatrixData::kFxModules; ++m)
    {
        auto& card = cards[(size_t) m];
        card = std::make_unique<FxCard> (state, e, l, (FxModule) m, accent, makeControl, registerSwitched);
        card->onMove = [this] (int from, int to)
        {
            engine.modMatrix (layer).moveFxModule (from, juce::jlimit (0, ModMatrixData::kFxModules - 1, to));
            timerCallback();
        };
        addAndMakeVisible (*card);
    }
    shownOrder = engine.modMatrix (layer).getFxOrder();
    for (int position = 0; position < ModMatrixData::kFxModules; ++position)
        cards[(size_t) shownOrder[(size_t) position]]->setChainPosition (position);
}

void FxPage::visibilityChanged()
{
    if (isVisible())
    {
        timerCallback();
        startTimerHz (15);
    }
    else
    {
        stopTimer();
    }
}

void FxPage::timerCallback()
{
    const auto order = engine.modMatrix (layer).getFxOrder();
    if (order != shownOrder)
    {
        shownOrder = order;
        for (int position = 0; position < ModMatrixData::kFxModules; ++position)
            cards[(size_t) order[(size_t) position]]->setChainPosition (position);
        resized();
    }
    for (auto& card : cards)
        card->refresh();
}

void FxPage::resized()
{
    // Two rows of four, in processing order (left to right, top to bottom).
    auto area = getLocalBounds();
    const int rowHeight = (area.getHeight() - 4) / 2;
    const float columnWidth = (float) area.getWidth() / 4.0f;
    for (auto& card : cards)
    {
        const int position = card->getChainPosition();
        const int row = position / 4, column = position % 4;
        card->setBounds (juce::Rectangle<float> ((float) area.getX() + columnWidth * (float) column,
                                                 (float) (area.getY() + row * (rowHeight + 4)),
                                                 columnWidth, (float) rowHeight).toNearestInt().reduced (2));
    }
}
