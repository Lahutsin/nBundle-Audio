#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include "PluginProcessor.h"
#include "PresetBrowser.h"
#include "EditorComponents.h"
#include "ModulationUi.h"
#include "FxPage.h"

// The 32 alternative waveforms as tiles (number, cycle, name); click selects one.
class WaveSlotGrid : public juce::Component
{
public:
    explicit WaveSlotGrid (nsynth::SynthEngine& engine);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int getSelected() const noexcept { return selected; }
    std::function<void (int)> onSelect;

    static constexpr int kColumns = 4;

private:
    juce::Rectangle<int> tileBounds (int slot) const noexcept;
    nsynth::SynthEngine& engine;
    int selected { 0 };
};

class WaveBrowserPage : public juce::Component,
                        private juce::Timer
{
public:
    explicit WaveBrowserPage (nsynth::SynthEngine& engine);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;

private:
    void timerCallback() override;
    nsynth::SynthEngine& engine;
    WaveSlotGrid slots;
    juce::Rectangle<int> preview;
    juce::TextButton importButton { "IMPORT..." }, exportButton { "EXPORT .WS6..." }, closeButton { "CLOSE" };
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
};

// Header preset display: shows what is loaded (performance or the selected layer's patch) and
// opens the preset browser when clicked.
class PresetDisplay : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    void setContent (const juce::String& tag, const juce::String& name, const juce::String& detail, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void()> onClick;
    juce::String getPresetName() const { return name; }

private:
    juce::String tag, name, detail;
    juce::Colour accent;
};

class NSynthAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   public juce::DragAndDropContainer,
                                   public ModulationHost,
                                   private juce::Timer
{
public:
    explicit NSynthAudioProcessorEditor (NSynthAudioProcessor&);
    ~NSynthAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The layer pages: SYNTH (sound sources, filters, envelopes, voicing, macros), MOD (MSEGs and
    // the modulation slots) and FX (the effect chain).
    enum class Page { synth, mod, fx };
    void showPage (Page page);
    Page getPage() const noexcept { return page; }

    // Exposed for the editor tests.
    float getScale() const noexcept { return scale; }
    juce::Component& getContent() noexcept { return content; }
    static constexpr int kDesignWidth = NSynthAudioProcessor::kDesignWidth;
    static constexpr int kDesignHeight = NSynthAudioProcessor::kDesignHeight;
    void setScale (float newScale);
    ModPage& getModPage (nsynth::Layer layer) noexcept { return *layers[(size_t) nsynth::toInt (layer)].modPage; }
    FxPage& getFxPage (nsynth::Layer layer) noexcept { return *layers[(size_t) nsynth::toInt (layer)].fxPage; }
    WaveBrowserPage& getWaveBrowserPage() noexcept { return *waveBrowserPage; }
    void showWaveBrowserPage (bool shouldShow) { waveBrowserPage->setVisible (shouldShow); }
    PresetBrowser& getPresetBrowser() noexcept { return *presetBrowser; }
    PresetDisplay& getPresetDisplay() noexcept { return presetDisplay; }
    void openPresetBrowser (nSynth::PresetKind kind);
    void selectBottomLayer (nsynth::Layer layer);
    // Cells shared by two controls (delay ms / division, static velocity / curve, MSEG time / length).
    void updateSwitchedControls();
    // Modulation rings of the shown layer's knobs.
    void updateModulationDisplay();

    // ModulationHost
    bool canModulate (nsynth::Layer layer, int parameterIndex) const override;
    void addModulationMenuItems (juce::PopupMenu& menu, nsynth::Layer layer, int parameterIndex) override;
    void handleModulationMenu (int result, nsynth::Layer layer, int parameterIndex) override;
    void dropModulationSource (nsynth::ModSource source, nsynth::Layer layer, int parameterIndex) override;

private:
    // The SYNTH page of a layer is three rows following the signal flow: sources -> filter -> amp,
    // modulators, voicing / filter 2 / macros. MOD and FX are pages of their own.
    struct LayerUi
    {
        std::array<std::vector<std::unique_ptr<SectionPanel>>, 3> rows;
        std::unique_ptr<ModPage> modPage;
        std::unique_ptr<FxPage> fxPage;
        std::vector<ParamControl*> controls;   // every control of the layer (modulation rings)
    };

    struct SwitchedCell
    {
        ParamControl* whenOff { nullptr };
        ParamControl* whenOn { nullptr };
        std::atomic<float>* condition { nullptr };
    };

    void timerCallback() override;
    void refreshDisplay() noexcept;
    std::unique_ptr<ParamControl> makeControl (nsynth::Layer layer, const char* suffix);
    std::unique_ptr<SectionPanel> makeSection (nsynth::Layer layer, nsynth::Section section, int columns, juce::Colour accent);
    void configureMidiLearn (ParamControl& control);
    void layoutContent();
    void updatePageVisibility();
    void savePresetAs (nSynth::PresetKind kind);

    NSynthAudioProcessor& synth;
    NSynthLookAndFeel lnf;
    juce::Component content;
    float scale { 1.0f };
    uint32_t lastMidiParameterDrops { 0 };
    int timerTicks { 0 };

    std::array<LayerUi, 2> layers;
    std::vector<SwitchedCell> switchedCells;
    nsynth::Layer bottomLayer { nsynth::Layer::upper };
    Page page { Page::synth };

    juce::Label title, voiceMeter, layerHint;
    PresetDisplay presetDisplay;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, savePreset { "SAVE" };
    juce::TextButton upperSelect { "UPPER" }, lowerSelect { "LOWER" };
    juce::TextButton initLayer { "INIT" }, copyUpToLow { "COPY U>L" }, copyLowToUp { "COPY L>U" };
    juce::TextButton loadPatch { "LOAD PATCH" }, savePatch { "SAVE PATCH" }, comparePatch { "COMPARE" };
    juce::TextButton synthTab { "SYNTH" }, modTab { "MOD" }, fxTab { "FX" }, waveBrowserButton { "WAVE BANK" };
    juce::ComboBox sizeBox;
    std::unique_ptr<WaveBrowserPage> waveBrowserPage;
    std::unique_ptr<PresetBrowser> presetBrowser;
    std::unique_ptr<SectionPanel> performancePanel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NSynthAudioProcessorEditor)
};
