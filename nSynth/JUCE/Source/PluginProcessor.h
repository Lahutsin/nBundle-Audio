#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <mutex>
#include "Core/Params/ParamRegistry.h"
#include "Core/Engine/SynthEngine.h"
#include "PresetManager.h"
#include "PresetLibrary.h"

class NSynthAudioProcessor : public juce::AudioProcessor,
                             private juce::AsyncUpdater
{
public:
    static constexpr int kDesignWidth = 1360;
    static constexpr int kDesignHeight = 820;
    // 7: one USER_WAVES bank per layer ("layer" property), factory banks are not stored.
    static constexpr int kStateVersion = 7;
    static constexpr int kPatchStateVersion = 4;

    NSynthAudioProcessor();
    ~NSynthAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    void audioWorkgroupContextChanged (const juce::AudioWorkgroup& workgroup) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "nSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // REVERB DECAY (RT60) reaches 20 s: offline renders keep the whole tail (a frozen delay never ends).
    double getTailLengthSeconds() const override { return 20.0; }

    // Programs: 0 = the current sound, 1..N = the factory performances (MIDI program change n
    // loads performance n + 1).
    int getNumPrograms() override { return 1 + (int) factoryPerformances.size(); }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    nsynth::SynthEngine& getEngine() noexcept { return engine; }
    nSynth::PresetManager& getPresetManager() noexcept { return *presetManager; }
    nSynth::PresetLibrary& getPresetLibrary() noexcept { return presetLibrary; }
    juce::File getUserPatchDirectory() const { return patchDirectory; }

    // Layer utilities (message thread).
    void initLayer (nsynth::Layer layer);                    // init patch: single sawtooth
    void copyLayer (nsynth::Layer from, nsynth::Layer to);
    // Patches are layer independent: a patch saved from either layer loads into both.
    bool loadPatch (nsynth::Layer layer, const juce::File& file, juce::String* error = nullptr);
    bool loadPatch (nsynth::Layer layer, const nSynth::PresetEntry& entry, juce::String* error = nullptr);
    bool loadPatchState (nsynth::Layer layer, const juce::ValueTree& patch, const juce::String& name,
                         juce::String* error = nullptr);
    bool savePatch (nsynth::Layer layer, const juce::File& file, juce::String* error = nullptr);
    bool isPatchDirty (nsynth::Layer layer) const;
    bool setPatchCompare (nsynth::Layer layer, bool enabled);
    bool isPatchCompareActive (nsynth::Layer layer) const noexcept;
    juce::String getPatchName (nsynth::Layer layer) const;
    juce::String getPatchCategory (nsynth::Layer layer) const;
    // PresetEntry::key() of the loaded patch / performance, empty when it did not come from the library.
    juce::String getPatchKey (nsynth::Layer layer) const;
    juce::String getPerformanceKey() const;

    // Performance presets = whole plugin state.
    bool loadPerformance (const juce::File& file, juce::String* error = nullptr);
    bool loadPerformance (const nSynth::PresetEntry& entry, juce::String* error = nullptr);
    bool savePerformance (const juce::File& file, juce::String* error = nullptr);
    juce::String getCurrentPresetName() const;
    bool isPerformanceDirty() const;

    // Undo / redo of everything a patch or performance stores (message thread). The editor calls
    // captureUndoPoint() regularly; a change becomes an undo step once it has settled (no change
    // since the previous call), so a knob drag is one step.
    void captureUndoPoint();
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    // Oversampling of the voices (session setting, not a host parameter): 1 (eco), 2 (standard)
    // or 4 (high). Offline renders can use 4x regardless. Changing it re-prepares the engine.
    int getOversamplingQuality() const noexcept { return oversamplingQuality.load(); }
    void setOversamplingQuality (int factor);
    bool getOfflineHighQuality() const noexcept { return offlineHighQuality.load(); }
    void setOfflineHighQuality (bool enabled);

    // Editor size persistence (uniform scale factor) - deliberately not a host parameter.
    float getSavedEditorScale() const noexcept { return editorScale.load(); }
    void setSavedEditorScale (float s) noexcept { editorScale = s; }

    int getActiveVoices (nsynth::Layer l) const noexcept { return activeVoices[(size_t) nsynth::toInt (l)].load(); }

private:
    // Everything a layer patch stores.
    struct LayerState
    {
        std::vector<float> values;   // normalised, in getLayerParamSpecs() order
        nsynth::ModMatrixData matrix;
        nsynth::WaveBankPtr waves;
        bool sameAs (const LayerState& other) const;
    };
    // Everything a performance stores (undo steps).
    struct FullState
    {
        std::vector<float> values;   // normalised, all host parameters
        std::array<nsynth::ModMatrixData, 2> matrices;
        std::array<nsynth::WaveBankPtr, 2> waves;
        bool sameAs (const FullState& other) const;
    };

    void handleAsyncUpdate() override;
    static BusesProperties makeBuses();
    LayerState captureLayerState (nsynth::Layer layer) const;
    void applyLayerState (nsynth::Layer layer, const LayerState& state);
    std::vector<float> captureLayerValues (nsynth::Layer layer) const;
    void applyLayerValues (nsynth::Layer layer, const std::vector<float>& values);
    FullState captureFullState() const;
    void applyFullState (const FullState& state);
    static void setParameterWithGesture (juce::RangedAudioParameter& parameter, float normalisedValue);
    bool validateParameterValues (const juce::ValueTree& tree, juce::String* error) const;
    // Validates everything first, then applies the whole state at once (performance / session).
    bool restoreFullState (juce::ValueTree tree, bool keepMasterVolume, juce::String* error);
    bool loadPerformanceState (juce::ValueTree tree, const juce::String& name, juce::String* error);
    void resetBaselines (const juce::ValueTree& tree, const juce::String& fallbackPatchName);
    static void removeLegacyChildren (juce::ValueTree& root);
    void loadProgram (int index);

    juce::AudioProcessorValueTreeState apvts;
    nsynth::SynthEngine engine;
    std::unique_ptr<nSynth::PresetManager> presetManager;
    nSynth::PresetLibrary presetLibrary;
    std::vector<nSynth::PresetEntry> factoryPerformances;
    juce::File patchDirectory;
    std::array<std::vector<juce::RangedAudioParameter*>, 2> layerParameters;
    std::vector<juce::RangedAudioParameter*> allParameters;

    // Guards the preset bookkeeping below: hosts may call get/setStateInformation off the message thread.
    mutable std::recursive_mutex stateLock;
    juce::String currentPresetName { "Init" }, performanceKey;
    std::array<LayerState, 2> patchBaseline, patchCompareSnapshot;
    std::array<juce::String, 2> patchNames { "Init", "Init" }, patchCategories, patchKeys;
    std::array<bool, 2> patchCompareActive {};
    FullState performanceBaseline;

    std::vector<FullState> undoHistory;
    size_t undoPosition { 0 };
    FullState lastObservedState;
    static constexpr size_t kMaxUndoSteps = 100;

    int chooseOversamplingFactor() const noexcept;
    void applyOversamplingChange();

    std::atomic<float> editorScale { 1.0f };
    std::atomic<int> oversamplingQuality { nsynth::SynthLayer::kDefaultOversamplingFactor };
    std::atomic<bool> offlineHighQuality { true };
    std::atomic<int> pendingLatency { 0 };
    std::atomic<int> currentProgram { 0 }, pendingProgram { -1 };
    std::array<std::atomic<int>, 2> activeVoices {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NSynthAudioProcessor)
};
