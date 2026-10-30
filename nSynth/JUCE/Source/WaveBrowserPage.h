#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Core/Engine/SynthEngine.h"

// The 32 alternative waveforms as tiles (number, cycle, name); click selects one.
class WaveSlotGrid : public juce::Component
{
public:
    explicit WaveSlotGrid (nsynth::SynthEngine& engine);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    int getSelected() const noexcept { return selected; }
    void setLayer (nsynth::Layer newLayer) { layer = newLayer; repaint(); }
    std::function<void (int)> onSelect;

    static constexpr int kColumns = 4;

private:
    juce::Rectangle<int> tileBounds (int slot) const noexcept;
    int slotAt (juce::Point<int> position) const noexcept;
    nsynth::SynthEngine& engine;
    nsynth::Layer layer { nsynth::Layer::upper };
    int selected { 0 }, hovered { -1 };
};

class WaveBrowserPage : public juce::Component,
                        private juce::Timer
{
public:
    explicit WaveBrowserPage (nsynth::SynthEngine& engine);
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    // The wave bank page edits the waveforms of one layer (each patch carries its own).
    void setLayer (nsynth::Layer newLayer);
    std::function<void()> onClose;

private:
    void timerCallback() override;
    nsynth::SynthEngine& engine;
    nsynth::Layer layer { nsynth::Layer::upper };
    const nsynth::WaveBank* shownBank { nullptr };
    WaveSlotGrid slots;
    juce::Rectangle<int> preview;
    juce::TextButton importButton { "IMPORT..." }, exportButton { "EXPORT .WS6..." }, closeButton { "CLOSE" };
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
};
