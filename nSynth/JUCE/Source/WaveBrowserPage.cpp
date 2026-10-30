#include "WaveBrowserPage.h"
#include "UiPalette.h"

using namespace nsynth;
using namespace nSynth::ui;

namespace
{
// Overlay pages (mod matrix, wave bank): a framed panel with an accent strip, a title row on
// top and a centred CLOSE button at the bottom.
constexpr int kPageHeader = 32, kPageFooter = 40, kPageTitleWidth = 320;
// Preview of the selected slot: name row, gap, waveform box.
constexpr int kPreviewNameHeight = 30, kPreviewGap = 8, kPreviewPlotHeight = 340;

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
WaveSlotGrid::WaveSlotGrid (SynthEngine& e)
    : engine (e)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int WaveSlotGrid::slotAt (juce::Point<int> position) const noexcept
{
    for (int slot = 0; slot < WaveBank::kNumAltWaves; ++slot)
        if (tileBounds (slot).contains (position))
            return slot;
    return -1;
}

void WaveSlotGrid::mouseMove (const juce::MouseEvent& event)
{
    const int slot = slotAt (event.getPosition());
    if (slot == hovered)
        return;
    if (hovered >= 0)
        repaint (tileBounds (hovered));
    hovered = slot;
    if (hovered >= 0)
        repaint (tileBounds (hovered));
}

void WaveSlotGrid::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0)
        repaint (tileBounds (hovered));
    hovered = -1;
}

juce::Rectangle<int> WaveSlotGrid::tileBounds (int slot) const noexcept
{
    const int rows = (WaveBank::kNumAltWaves + kColumns - 1) / kColumns;
    const float w = (float) getWidth() / (float) kColumns, h = (float) getHeight() / (float) rows;
    return juce::Rectangle<float> ((float) (slot % kColumns) * w, (float) (slot / kColumns) * h, w, h).toNearestInt().reduced (3);
}

void WaveSlotGrid::paint (juce::Graphics& g)
{
    const auto bank = engine.getWaveBank (layer);   // keeps the bank alive while painting
    const auto& names = WaveBank::altWaveNames();
    const auto clip = g.getClipBounds();
    for (int slot = 0; slot < WaveBank::kNumAltWaves; ++slot)
    {
        const auto r = tileBounds (slot);
        if (! r.intersects (clip))
            continue;
        const bool isSelected = slot == selected, hot = slot == hovered;
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
        paintCycle (g, bank->altCycle (slot), inner.toFloat().reduced (0.0f, 4.0f),
                    isSelected ? kPerfAccent : kTextDim, 1.5f, 128);
    }
}

void WaveSlotGrid::mouseDown (const juce::MouseEvent& event)
{
    const int slot = slotAt (event.getPosition());
    if (slot < 0)
        return;
    repaint (tileBounds (selected));
    selected = slot;
    repaint (tileBounds (selected));
    if (onSelect != nullptr)
        onSelect (slot);
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
            if (! engine.requestWaveImport (layer, slots.getSelected(), file, &error))
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
            if (! engine.exportWave (layer, slots.getSelected(), file, &error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Wave export failed", error);
        });
    };
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
}

void WaveBrowserPage::setLayer (Layer newLayer)
{
    layer = newLayer;
    slots.setLayer (newLayer);
    shownBank = nullptr;
    repaint();
}

void WaveBrowserPage::visibilityChanged()
{
    // Only the visible page polls the import state and the bank.
    if (isVisible())
        startTimerHz (5);
    else
        stopTimer();
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
            engine.acknowledgeWaveImport();
            break;
        case SynthEngine::WaveImportState::failed:
            status.setText ("Import failed: " + engine.getWaveImportError(), juce::dontSendNotification);
            engine.acknowledgeWaveImport();
            break;
    }
    // Repaint the waveforms only when the layer's bank changed (import, preset load, undo).
    const auto bank = engine.getWaveBank (layer);
    if (bank.get() != shownBank)
    {
        shownBank = bank.get();
        repaint();
        slots.repaint();
    }
}

void WaveBrowserPage::paint (juce::Graphics& g)
{
    paintPageFrame (g, getLocalBounds(), kPerfAccent);
    auto header = pageContent (getLocalBounds()).removeFromTop (kPageHeader);
    paintPageTitle (g, header, "WAVE BANK", juce::String (WaveBank::kNumAltWaves) + " SLOTS", kPerfAccent);
    g.setColour (kTextDim);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (juce::String ("Alternative waveforms for DDS1 (Alt Wave A / B) of the ")
                    + (layer == Layer::upper ? "UPPER" : "LOWER") + " patch",
                header.withTrimmedLeft (kPageTitleWidth), juce::Justification::centredRight);

    if (preview.isEmpty())
        return;
    const int slot = slots.getSelected();
    auto r = preview;
    auto nameRow = r.removeFromTop (kPreviewNameHeight);
    g.setFont (juce::Font (juce::FontOptions (12.0f)).boldened());
    g.setColour (kPerfAccent);
    g.drawText ("SLOT " + juce::String (slot + 1).paddedLeft ('0', 2), nameRow.removeFromLeft (64), juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions (20.0f)).boldened());
    g.setColour (kText);
    g.drawFittedText (WaveBank::altWaveNames()[slot], nameRow, juce::Justification::centredLeft, 1, 0.8f);

    r.removeFromTop (kPreviewGap);
    const auto box = r.toFloat();
    g.setColour (kBackground);
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (kOutline);
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
    paintCycle (g, engine.getWaveBank (layer)->altCycle (slot), box.reduced (18.0f, 24.0f), kPerfAccent, 2.5f, 512);
}

void WaveBrowserPage::resized()
{
    auto area = pageContent (getLocalBounds());
    area.removeFromTop (kPageHeader + 12);
    closeButton.setBounds (area.removeFromBottom (kPageFooter).withSizeKeepingCentre (140, 32));
    area.removeFromBottom (8);

    slots.setBounds (area.removeFromLeft (area.getWidth() * 3 / 5));
    area.removeFromLeft (24);
    preview = area.removeFromTop (kPreviewNameHeight + kPreviewGap + kPreviewPlotHeight);
    area.removeFromTop (16);
    auto buttons = area.removeFromTop (32);
    importButton.setBounds (buttons.removeFromLeft (150));
    buttons.removeFromLeft (8);
    exportButton.setBounds (buttons.removeFromLeft (170));
    area.removeFromTop (10);
    status.setBounds (area.removeFromTop (44));
}
