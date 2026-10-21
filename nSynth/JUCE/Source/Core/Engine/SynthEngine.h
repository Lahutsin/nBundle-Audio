#pragma once

#include "SynthLayer.h"
#include "SequenceBank.h"
#include <atomic>
#include <mutex>

namespace nsynth
{
// Whole instrument: 20 voices shared by the UPPER and LOWER layers,
// keyboard modes, MIDI routing and the output mix.
class SynthEngine
{
public:
    static constexpr int kNumVoices = 20;
    enum class WaveImportState { idle, building, ready, failed };

    explicit SynthEngine (juce::AudioProcessorValueTreeState& state);

    void prepare (double sampleRate, int maxBlockSize, int requestedRenderWorkers = -1);
    int getRenderWorkerCount() const noexcept { return renderPool.getNumWorkers(); }

    // mix: main stereo output. upperOut/lowerOut: optional separate layer outputs (may be null).
    void process (juce::AudioBuffer<float>& mix, juce::AudioBuffer<float>* upperOut, juce::AudioBuffer<float>* lowerOut,
                  const juce::MidiBuffer& midi, double hostBpm);

    float getLatencySamples() const noexcept { return upper.getLatencySamples(); }
    int getActiveVoices (Layer l) const noexcept { return layer (l).getActiveVoiceCount(); }
    const SynthLayer& layer (Layer l) const noexcept { return l == Layer::upper ? upper : lower; }
    SynthLayer& layer (Layer l) noexcept { return l == Layer::upper ? upper : lower; }
    ModMatrix& modMatrix (Layer l) noexcept { return layer (l).getModMatrix(); }
    const ModMatrix& modMatrix (Layer l) const noexcept { return layer (l).getModMatrix(); }
    void appendModMatrixState (juce::ValueTree& state) const;
    bool restoreModMatrixState (const juce::ValueTree& state, juce::String* error = nullptr);
    void appendSequenceState (juce::ValueTree& state) const;
    void appendSequenceState (juce::ValueTree& state, Layer layer) const;
    bool restoreSequenceState (const juce::ValueTree& state, juce::String* error = nullptr);
    void appendWaveBankState (juce::ValueTree& state) const;
    bool restoreWaveBankState (const juce::ValueTree& state, juce::String* error = nullptr);
    void appendMidiInputState (juce::ValueTree& state) const;
    bool restoreMidiInputState (const juce::ValueTree& state, juce::String* error = nullptr);
    bool requestMidiLearn (const juce::String& parameterId);
    void clearMidiLearn (const juce::String& parameterId);
    int getMidiLearnCC (const juce::String& parameterId) const;
    struct MidiParameterChange
    {
        juce::RangedAudioParameter* parameter { nullptr };
        float normalizedValue { 0.0f };
    };
    bool popMidiParameterChange (MidiParameterChange& change) noexcept;
    bool hasPendingMidiParameterChanges() const noexcept;
    uint32_t getDroppedMidiParameterChanges() const noexcept { return droppedMidiParameterChanges.load(); }
    const WaveBank& getWaveBank() const noexcept { return *std::atomic_load (&waveBank); }
    SequenceBank& sequenceBank (Layer layer) noexcept { return sequenceBanks[(size_t) toInt (layer)]; }
    const SequenceBank& sequenceBank (Layer layer) const noexcept { return sequenceBanks[(size_t) toInt (layer)]; }
    bool requestWaveImport (int slot, const juce::File& file, juce::String* error = nullptr);
    WaveImportState getWaveImportState() const noexcept { return waveImportState.load(); }
    juce::String getWaveImportError() const;
    bool exportWave (int slot, const juce::File& file, juce::String* error = nullptr) const;
    KeyboardMode getKeyboardMode() const noexcept { return currentMode; }

private:
    void renderSegmentsCombined (int hostStart, int hostNum) noexcept;
    static void renderCombinedVoiceItem (void* context, int itemIndex, int lane) noexcept;
    void assignVoices (KeyboardMode mode, Layer singleLayer);
    void handleMidi (const juce::MidiMessage& m);
    void routeNote (const juce::MidiMessage& m, bool on);
    void updateChannelExpression (int channel) noexcept;
    void handleRpnDataEntry (int channel, bool lsbUpdated) noexcept;
    void applyHardwareController (int channel, int cc, int value, int maximum);
    void queueParameterValue (int binding, int value, int maximum) noexcept;
    int findBinding (const juce::String& parameterId) const noexcept;
    bool isMpeMemberChannel (int channel) const noexcept;

    struct ParameterBinding
    {
        juce::String id;
        juce::RangedAudioParameter* parameter { nullptr };
        const ParamSpec* spec { nullptr };
        int layer { -1 };
    };

    struct MidiChannelState
    {
        int rpnMsb { 127 }, rpnLsb { 127 }, nrpnMsb { 127 }, nrpnLsb { 127 };
        int dataEntryMsb { 0 }, dataEntryLsb { 0 };
        float pitchBend { 0.0f }, pressure { 0.0f }, timbre { 0.0f };
    };

    PerformanceParamReader perfReader;
    PerformanceSnapshot perf;
    std::shared_ptr<const WaveBank> waveBank;
    std::shared_ptr<const WaveBank> pendingWaveBank;
    std::shared_ptr<const WaveBank> retiredWaveBank;
    std::array<Voice, kNumVoices> voices;
    SynthLayer upper, lower;
    std::array<SequenceBank, 2> sequenceBanks;
    VoiceRenderPool renderPool;
    uint64_t ageCounter { 0 };

    KeyboardMode currentMode { KeyboardMode::single };
    Layer currentSingleLayer { Layer::upper };
    Quality currentQuality { Quality::x4 };
    bool prepared { false };

    // live controller state from MIDI
    float midiModLever { 0.0f };
    float midiExpression { 0.0f }, midiRibbon { 0.0f };
    int ribbonMsb { 64 };
    std::array<MidiChannelState, 16> midiChannels {};
    std::array<std::atomic<float>, 16> bendSensitivity {}, channelFineTune {}, channelCoarseTune {};
    std::atomic<int> lowerMpeMembers { 0 }, upperMpeMembers { 0 };
    std::atomic<bool> midiStateRefresh { false };
    struct ArpHeldNote
    {
        int note { 60 };
        float velocity { 1.0f };
        int channel { 1 };
    };
    struct ArpRuntimeState
    {
        std::vector<ArpHeldNote> physicalNotes;
        std::vector<ArpHeldNote> latchedNotes;
        bool active { false };
        int stepCounter { 0 };
        int cursor { 0 };
        int direction { 1 };
        double samplesToNext { 0.0 };
        int gateSamples { -1 };
        int playingNote { -1 };
        int playingChannel { 1 };
        juce::Random rng;
    };
    std::array<ArpRuntimeState, 2> arpStates {};
    std::array<int, 2> sequenceTranspose {}, sequenceTransposeNote { -1, -1 };
    std::array<bool, 2> sequencePlaying {};
    std::array<int, 2> sequenceStepIndex {}, sequenceSlotIndex {}, sequenceGateSamples { -1, -1 };
    std::array<double, 2> sequenceSamplesToNext {};
    std::array<std::shared_ptr<const SequenceBankData>, 2> blockSequences;
    double hostSampleRate { 44100.0 };

    juce::AudioBuffer<float> upperBuffer, lowerBuffer;
    std::vector<ParameterBinding> parameterBindings;
    std::array<std::atomic<int>, 128> learnedCCBinding;
    std::atomic<int> pendingMidiLearnBinding { -1 };
    static constexpr uint32_t kMidiParameterQueueSize = 512;
    std::array<MidiParameterChange, kMidiParameterQueueSize> midiParameterQueue {};
    std::atomic<uint32_t> midiParameterWrite { 0 }, midiParameterRead { 0 }, droppedMidiParameterChanges { 0 };
    juce::ThreadPool waveImportPool { 1 };
    std::atomic<WaveImportState> waveImportState { WaveImportState::idle };
    mutable std::mutex waveImportMutex;
    juce::String waveImportError;
};
} // namespace nsynth
