#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include "Parameters.h"
#include "NCompProcessor.h"

class NCompAudioProcessor : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::AsyncUpdater
{
public:
    enum class PresetKind
    {
        manual = 0,
        factory = 1,
        user = 2
    };

    NCompAudioProcessor();
    ~NCompAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "nComp"; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    PresetKind getCurrentPresetKind() const noexcept;
    bool isCurrentPresetDirty() const noexcept { return currentPresetDirty.load(); }
    juce::String getCurrentPresetName() const;
    juce::File getCurrentPresetFile() const;
    juce::File getUserPresetDirectory() const;
    std::vector<juce::File> getUserPresetFiles() const;
    int getCurrentABSlot() const noexcept { return currentABSlot; }
    void selectABSlot(int slot);
    bool loadPresetFromFile(const juce::File& file, juce::String* errorMessage = nullptr);
    bool savePresetToFile(const juce::File& file, juce::String* errorMessage = nullptr);
    bool deletePresetFile(const juce::File& file, juce::String* errorMessage = nullptr);
    void markCurrentStateAsManual();
    void copyActiveABSlotToInactive();

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    std::atomic<float>& getMeterL() noexcept { return meterL; }
    std::atomic<float>& getMeterR() noexcept { return meterR; }
    std::atomic<float>& getTransferInputDb() noexcept { return transferInputDb; }
    std::atomic<float>& getTransferOutputDb() noexcept { return transferOutputDb; }
    std::atomic<float>& getTransferReductionDb() noexcept { return transferReductionDb; }

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void mirrorParameterValue(const char* sourceId, const char* targetId);
    bool isLinkOn() const;
    int getChoiceIndex(const char* id) const;
    static PresetKind presetKindFromInt(int value) noexcept;
    void setCurrentPresetInfo(PresetKind kind, int factoryIndex, const juce::File& userPresetFile, bool dirty);
    void applyPresetMetadata(juce::ValueTree& state, PresetKind kind, int factoryIndex, const juce::File& userPresetFile, bool dirty) const;
    void restorePresetMetadata(const juce::ValueTree& state);
    void captureABSlot(int slot);
    void restoreABSlot(int slot);
    void initialiseABSlotsFromCurrentState();
    static bool readPresetStateFromFile(const juce::File& file, juce::ValueTree& outState, juce::String& errorMessage);

    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager undoManager;
    NComp::NCompProcessor dsp;
    std::atomic<float> meterL { 0.0f };
    std::atomic<float> meterR { 0.0f };
    std::atomic<float> transferInputDb { -60.0f };
    std::atomic<float> transferOutputDb { -60.0f };
    std::atomic<float> transferReductionDb { 0.0f };

    std::array<juce::ValueTree, 2> abSlots;
    int currentABSlot { 0 };
    std::atomic<int> currentProgramIndex { 0 };
    std::atomic<int> currentPresetKind { static_cast<int>(PresetKind::manual) };
    std::atomic<bool> currentPresetDirty { false };
    juce::String currentUserPresetPath;
    std::atomic<bool> suppressCallbacks { false };
    std::atomic<bool> applyingFactoryPreset { false };
    std::atomic<bool> pendingLinkSyncAll { false };
    std::atomic<int> pendingMirrorPairIndex { -1 };
    std::atomic<int> pendingMirrorDirection { 0 };
    juce::CriticalSection stateLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NCompAudioProcessor)
};
