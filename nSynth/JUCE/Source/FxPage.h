#pragma once

#include "ModulationUi.h"

// One effect module: chain number, power, name and move buttons in the header (drag the header
// onto another card to move it there), the module's controls below. Bypassed modules are dimmed.
class FxCard : public juce::Component,
               public juce::DragAndDropTarget
{
public:
    FxCard (juce::AudioProcessorValueTreeState& state, nsynth::SynthEngine& engine, nsynth::Layer layer,
            nsynth::FxModule module, juce::Colour accent, const ControlFactory& makeControl,
            const SwitchedCellRegistrar& registerSwitched);
    void setChainPosition (int position);
    int getChainPosition() const noexcept { return chainPosition; }
    nsynth::FxModule getModule() const noexcept { return module; }
    bool isModuleOn() const noexcept;
    void refresh();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDrag (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    std::function<void (int from, int to)> onMove;

    static constexpr int kHeaderHeight = 28;

private:
    float raw (const char* suffix) const;
    void paintEqCurve (juce::Graphics&, juce::Rectangle<float> area) const;
    void paintReductionMeter (juce::Graphics&, juce::Rectangle<float> area) const;

    juce::AudioProcessorValueTreeState& state;
    nsynth::SynthEngine& engine;
    nsynth::Layer layer;
    nsynth::FxModule module;
    juce::Colour accent;
    int chainPosition { 0 }, columns { 4 };
    bool dropHover { false }, shownOn { true };
    float shownReduction { 0.0f };
    std::vector<float> shownEq;
    std::unique_ptr<ParamControl> power;          // none for the chorus (its MODE has an Off position)
    juce::ToggleButton chorusPower;
    int chorusLastMode { 1 };
    std::vector<std::unique_ptr<ParamControl>> controls;
    std::vector<std::vector<ParamControl*>> cells;  // empty cell = spacer
    juce::TextButton moveLeft { "<" }, moveRight { ">" };
    juce::Rectangle<int> visual;
};

// FX page of one layer: the eight modules as cards in their processing order (1 -> 8).
class FxPage : public juce::Component,
               private juce::Timer
{
public:
    FxPage (juce::AudioProcessorValueTreeState& state, nsynth::SynthEngine& engine, nsynth::Layer layer,
            juce::Colour accent, const ControlFactory& makeControl, const SwitchedCellRegistrar& registerSwitched);
    void resized() override;
    void visibilityChanged() override;
    FxCard& getCard (nsynth::FxModule module) noexcept { return *cards[(size_t) module]; }

private:
    void timerCallback() override;
    nsynth::SynthEngine& engine;
    nsynth::Layer layer;
    std::array<std::unique_ptr<FxCard>, nsynth::ModMatrixData::kFxModules> cards;
    std::array<nsynth::FxModule, nsynth::ModMatrixData::kFxModules> shownOrder {};
};
