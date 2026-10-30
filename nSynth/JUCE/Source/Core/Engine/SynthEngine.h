#pragma once

#include "SynthLayer.h"
#include "RealtimeHandoff.h"
#include <atomic>
#include <mutex>

namespace nsynth
{
// Whole instrument: 128 voices shared by the UPPER and LOWER layers,
// keyboard modes, MIDI routing and the output mix.
class SynthEngine
{
public:
    static constexpr int kNumVoices = SynthLayer::kMaxVoices;
    enum class WaveImportState { idle, building, ready, failed };

    explicit SynthEngine (juce::AudioProcessorValueTreeState& state);

    void prepare (double sampleRate, int maxBlockSize, int requestedRenderWorkers = -1);
    int getRenderWorkerCount() const noexcept { return renderPool.getNumWorkers(); }
    void setAudioWorkgroup (const juce::AudioWorkgroup& workgroup) { renderPool.setWorkgroup (workgroup); }
    static constexpr int kControlGrid = 16;          // host samples: controller event resolution
    static constexpr int kMinParallelSamples = 32;   // host samples: shorter segments render serially

    // mix: main stereo output. upperOut/lowerOut: optional separate layer outputs (may be null).
    void process (juce::AudioBuffer<float>& mix, juce::AudioBuffer<float>* upperOut, juce::AudioBuffer<float>* lowerOut,
                  const juce::MidiBuffer& midi, double hostBpm);

    int getActiveVoices (Layer l) const noexcept { return layer (l).getActiveVoiceCount(); }
    const SynthLayer& layer (Layer l) const noexcept { return l == Layer::upper ? upper : lower; }
    SynthLayer& layer (Layer l) noexcept { return l == Layer::upper ? upper : lower; }
    ModMatrix& modMatrix (Layer l) noexcept { return layer (l).getModMatrix(); }
    const ModMatrix& modMatrix (Layer l) const noexcept { return layer (l).getModMatrix(); }
    void appendModMatrixState (juce::ValueTree& state) const;
    bool restoreModMatrixState (const juce::ValueTree& state, juce::String* error = nullptr);
    bool stageModMatrixState (const juce::ValueTree& state, std::array<ModMatrixData, 2>& staged, juce::String* error = nullptr);
    void applyModMatrixState (const std::array<ModMatrixData, 2>& staged);
    // User wavetables, one bank per layer (a patch carries its own waves).
    // Performance / session state: a USER_WAVES child per layer whose bank is not the factory bank.
    void appendWaveBankState (juce::ValueTree& state) const;
    bool restoreWaveBankState (const juce::ValueTree& state, juce::String* error = nullptr);
    // Validates and builds the banks of a state without applying them (null = factory bank).
    bool stageWaveBankState (const juce::ValueTree& state, std::array<WaveBankPtr, 2>& staged, juce::String* error = nullptr);
    // Patch state: USER_WAVES of one layer, only when it differs from the factory bank.
    void appendLayerWaveBankState (juce::ValueTree& patch, Layer l) const;
    // Builds a bank from a USER_WAVES tree (invalid tree = factory bank). Recently built banks are cached.
    WaveBankPtr buildWaveBank (const juce::ValueTree& userWaves, juce::String* error = nullptr);
    void setWaveBank (Layer l, WaveBankPtr bank);
    void appendMidiInputState (juce::ValueTree& state) const;
    // apply = false only validates.
    bool restoreMidiInputState (const juce::ValueTree& state, juce::String* error = nullptr, bool apply = true);
    bool requestMidiLearn (const juce::String& parameterId);
    void clearMidiLearn (const juce::String& parameterId);
    int getMidiLearnCC (const juce::String& parameterId) const;
    // Changes whenever a learned assignment is made, cleared or restored (any thread), so the
    // editor only looks the assignments up again after a change.
    uint32_t getMidiLearnGeneration() const noexcept { return midiLearnGeneration.load (std::memory_order_acquire); }
    struct MidiParameterChange
    {
        juce::RangedAudioParameter* parameter { nullptr };
        float normalizedValue { 0.0f };
    };
    bool popMidiParameterChange (MidiParameterChange& change) noexcept;
    bool hasPendingMidiParameterChanges() const noexcept;
    uint32_t getDroppedMidiParameterChanges() const noexcept { return droppedMidiParameterChanges.load(); }
    // The newest bank of a layer (the audio thread picks it up at its next block).
    WaveBankPtr getWaveBank (Layer l) const { return waveBanks[(size_t) toInt (l)].latest(); }
    bool requestWaveImport (Layer l, int slot, const juce::File& file, juce::String* error = nullptr);
    WaveImportState getWaveImportState() const noexcept { return waveImportState.load(); }
    void acknowledgeWaveImport() noexcept;   // ready/failed -> idle once the editor has shown it
    juce::String getWaveImportError() const;
    bool exportWave (Layer l, int slot, const juce::File& file, juce::String* error = nullptr) const;
    // Message thread: frees modulation snapshots and wave banks the audio thread has moved past.
    void collectGarbage();
    // Audio thread -> processor: true once after the audio thread switched to newer shared data.
    // MIDI program change received by the audio thread (-1 = none); the processor loads it.
    int takeProgramChange() noexcept { return pendingProgramChange.exchange (-1, std::memory_order_acq_rel); }
    bool hasPendingProgramChange() const noexcept { return pendingProgramChange.load (std::memory_order_acquire) >= 0; }
    bool takeGarbageNotice() noexcept { return garbageNotice.exchange (false, std::memory_order_acq_rel); }
    KeyboardMode getKeyboardMode() const noexcept { return currentMode; }

    // Preset loads (any non-audio thread). Between begin and end the affected layers keep the
    // parameters, modulation and wavetables of their last block and fade out; end restarts them
    // with the new state, so a load never renders a mix of the old and the new patch.
    enum StateScope : int { upperScope = 1, lowerScope = 2, performanceScope = 4, allScopes = 7 };
    static int scopeOf (Layer l) noexcept { return l == Layer::upper ? upperScope : lowerScope; }
    void beginStateChange (int scopeMask) noexcept;
    void endStateChange (int scopeMask) noexcept;
    class ScopedStateChange
    {
    public:
        ScopedStateChange (SynthEngine& e, int mask) noexcept : engine (e), scopes (mask) { engine.beginStateChange (scopes); }
        ~ScopedStateChange() { engine.endStateChange (scopes); }
        ScopedStateChange (const ScopedStateChange&) = delete;
        ScopedStateChange& operator= (const ScopedStateChange&) = delete;
    private:
        SynthEngine& engine;
        int scopes;
    };

private:
    void beginBlockState() noexcept;
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
    static bool isLearnableController (int cc) noexcept;
    bool isMpeMemberChannel (int channel) const noexcept;
    bool isMpeMasterChannel (int channel) const noexcept;

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
    PerformanceSnapshot perf, perfCandidate;
    // [upper, lower, performance]
    std::array<std::atomic<int>, 3> stateChangeDepth {};
    std::array<std::atomic<uint32_t>, 3> stateGeneration {};
    std::array<uint32_t, 3> seenStateGeneration {};
    std::atomic<uint32_t> stateEpoch { 0 };
    std::array<RealtimeHandoff<WaveBank>, 2> waveBanks;
    std::array<const WaveBank*, 2> audioWaveBanks {};   // audio thread: the banks of the current block
    std::atomic<bool> garbageNotice { false };
    std::atomic<int> pendingProgramChange { -1 };
    std::mutex waveCacheMutex;
    std::vector<std::pair<uint64_t, WaveBankPtr>> waveCache;   // (payload hash, bank), most recent first
    std::array<Voice, kNumVoices> voices;
    SynthLayer upper, lower;
    VoiceRenderPool renderPool;
    uint64_t ageCounter { 0 };

    KeyboardMode currentMode { KeyboardMode::single };
    Layer currentSingleLayer { Layer::upper };
    bool prepared { false };

    // live controller state from MIDI
    float midiModLever { 0.0f };
    float lastPanelModLever { -1.0f }, lastPanelExpression { -1.0f };
    float midiExpression { 0.0f }, midiRibbon { 0.0f };
    int ribbonMsb { 64 };
    std::array<MidiChannelState, 16> midiChannels {};
    std::array<std::atomic<float>, 16> bendSensitivity {}, channelFineTune {}, channelCoarseTune {};
    std::atomic<int> lowerMpeMembers { 0 }, upperMpeMembers { 0 };
    std::atomic<bool> midiStateRefresh { false };

    juce::AudioBuffer<float> upperBuffer, lowerBuffer;
    OutputLimiter limiter;
    float lastMasterGain { -1.0f };
    std::vector<ParameterBinding> parameterBindings;
    std::array<std::atomic<int>, 128> learnedCCBinding;
    std::atomic<int> pendingMidiLearnBinding { -1 };
    std::atomic<uint32_t> midiLearnGeneration { 0 };
    static constexpr uint32_t kMidiParameterQueueSize = 512;
    std::array<MidiParameterChange, kMidiParameterQueueSize> midiParameterQueue {};
    std::atomic<uint32_t> midiParameterWrite { 0 }, midiParameterRead { 0 }, droppedMidiParameterChanges { 0 };
    juce::ThreadPool waveImportPool { 1 };
    std::atomic<WaveImportState> waveImportState { WaveImportState::idle };
    mutable std::mutex waveImportMutex;
    juce::String waveImportError;
};
} // namespace nsynth
