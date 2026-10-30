#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include "PluginProcessor.h"
#include "PresetBrowser.h"
#include "PresetDisplay.h"
#include "NSynthLookAndFeel.h"
#include "EditorComponents.h"
#include "ModulationUi.h"
#include "FxPage.h"
#include "WaveBrowserPage.h"

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
    // Shortcuts: Cmd/Ctrl+Z undo, Cmd/Ctrl+Shift+Z or Cmd/Ctrl+Y redo, Cmd/Ctrl+Left/Right previous / next preset.
    bool keyPressed (const juce::KeyPress&) override;

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
    // Segment buttons, captions and MIDI CCs of the controls on the shown page and the performance strip.
    void refreshControls();
    void undo();
    void redo();
    juce::Button& getUndoButton() noexcept { return undoButton; }
    juce::Button& getRedoButton() noexcept { return redoButton; }
    juce::Button& getQualityButton() noexcept { return *qualityButton; }

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
        std::array<std::vector<ParamControl*>, 3> pageControls;   // the layer's controls per Page
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
    void updateUndoButtons();
    void updateQualityButton();
    void showQualityMenu();
    void stepPreset (int delta);
    // Runs `change` (which replaces the target layer), after a confirmation when that layer has unsaved edits.
    void confirmLayerChange (nsynth::Layer target, const juce::String& title, const juce::String& question,
                             const juce::String& confirmText, std::function<void()> change);
    void savePresetAs (nSynth::PresetKind kind);
    void writePreset (bool patch, nsynth::Layer layer, const juce::File& file);

    NSynthAudioProcessor& synth;
    NSynthLookAndFeel lnf;
    juce::Component content;
    float scale { 1.0f };
    uint32_t lastMidiParameterDrops { 0 };
    int midiWarningTicks { 0 };   // refresh ticks the MIDI overflow warning stays up
    int timerTicks { 0 };
    Page buildingPage { Page::synth };   // the page whose controls makeControl() is creating

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
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    juce::TextButton* qualityButton { nullptr };   // in the performance strip's title row
    juce::ComboBox sizeBox;
    std::unique_ptr<WaveBrowserPage> waveBrowserPage;
    std::unique_ptr<PresetBrowser> presetBrowser;
    std::unique_ptr<SectionPanel> performancePanel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::ScopedMessageBox messageBox;   // closes an open confirmation with the editor

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NSynthAudioProcessorEditor)
};
