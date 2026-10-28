#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
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

    NSynthAudioProcessor();
    ~NSynthAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "nSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
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
    juce::String getPatchCategory (nsynth::Layer layer) const { return patchCategories[(size_t) nsynth::toInt (layer)]; }
    // PresetEntry::key() of the loaded patch / performance, empty when it did not come from the library.
    juce::String getPatchKey (nsynth::Layer layer) const { return patchKeys[(size_t) nsynth::toInt (layer)]; }
    juce::String getPerformanceKey() const { return performanceKey; }

    // Performance presets = whole plugin state.
    bool loadPerformance (const juce::File& file, juce::String* error = nullptr);
    bool loadPerformance (const nSynth::PresetEntry& entry, juce::String* error = nullptr);
    bool savePerformance (const juce::File& file, juce::String* error = nullptr);
    juce::String getCurrentPresetName() const { return currentPresetName; }

    // Editor size persistence (uniform scale factor) - deliberately not a host parameter.
    float getSavedEditorScale() const noexcept { return editorScale.load(); }
    void setSavedEditorScale (float s) noexcept { editorScale = s; }

    int getActiveVoices (nsynth::Layer l) const noexcept { return activeVoices[(size_t) nsynth::toInt (l)].load(); }

private:
    void handleAsyncUpdate() override;
    static BusesProperties makeBuses();
    std::vector<float> captureLayerValues (nsynth::Layer layer) const;
    void applyLayerValues (nsynth::Layer layer, const std::vector<float>& values);
    juce::ValueTree getWaveBankState (const juce::ValueTree& root) const;
    bool loadPerformanceState (juce::ValueTree tree, const juce::String& name, juce::String* error);
    static void removeLegacyChildren (juce::ValueTree& root);

    juce::AudioProcessorValueTreeState apvts;
    nsynth::SynthEngine engine;
    std::unique_ptr<nSynth::PresetManager> presetManager;
    nSynth::PresetLibrary presetLibrary;
    juce::File patchDirectory;
    juce::String currentPresetName { "Init" }, performanceKey;
    std::atomic<float> editorScale { 1.0f };
    std::atomic<int> pendingLatency { 0 };
    std::array<std::atomic<int>, 2> activeVoices {};
    std::array<std::vector<float>, 2> patchBaseline, patchCompareSnapshot;
    std::array<nsynth::ModMatrixData, 2> patchMatrixBaseline, patchMatrixCompareSnapshot;
    std::array<juce::ValueTree, 2> patchWaveBaseline, patchWaveCompareSnapshot;
    std::array<juce::String, 2> patchNames { "Init", "Init" }, patchCategories, patchKeys;
    std::array<bool, 2> patchCompareActive {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NSynthAudioProcessor)
};
