#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Voice.h"
#include "VoiceRenderPool.h"
#include "ModMatrix.h"
#include "../DSP/Effects.h"

namespace nsynth
{
// One layer (UPPER or LOWER): note handling / voice assignment over the hardware
// voices it currently owns, 2x oversampled rendering, pan, air shelf and the effect
// chain (eight modules in the patch's order).
class SynthLayer
{
public:
    static constexpr int kDefaultOversamplingFactor = 2;
    static constexpr int kMaxVoices = 128;

    SynthLayer (juce::AudioProcessorValueTreeState& state, Layer id);
    // Builds the lookup tables the audio code uses (call off the audio thread, before processing).
    static void initialiseStaticTables();

    // oversamplingFactor: 1, 2 or 4 (the voices run at hostSampleRate x factor).
    void prepare (double hostSampleRate, int maxBlockSize, VoiceRenderPool& renderPool,
                  int oversamplingFactor = kDefaultOversamplingFactor);
    int getOversamplingFactor() const noexcept { return oversamplingFactor; }
    float getLatencySamples() const noexcept;

    // Voices owned by this layer (changes with SINGLE/DUAL/SPLIT). Kills current notes.
    void assignVoices (Voice* first, int count);
    int getVoiceCount() const noexcept { return (int) voices.size(); }

    // Block start in three steps, so the engine can discard a parameter read that overlapped a
    // preset load: readParameters() reads the host parameters into a candidate, commitParameters()
    // makes it (and the newest modulation matrix and wave bank) current, startBlock() starts the block.
    void readParameters() noexcept;
    void commitParameters (const WaveBank* bank) noexcept;
    void startBlock (int numHostSamples, const VoiceContext& context, uint64_t* sharedAgeCounter);
    // All three (tests and tools). bank: this layer's wavetables (null keeps the voices' bank).
    void beginBlock (int numHostSamples, const VoiceContext& context, uint64_t* sharedAgeCounter,
                     const WaveBank* bank = nullptr);

    // Preset loads: while suspended the layer keeps its last parameters and fades out over 10 ms;
    // restart() then drops the voices and effect tails of the old sound.
    void setSuspended (bool shouldBeSuspended) noexcept { suspended = shouldBeSuspended; }
    // Silent because of a preset load, or asleep: no voices and the effect tails have died away.
    bool isSilenced() const noexcept { return (suspended && suspendGain <= 0.0f) || asleep; }
    void restart() noexcept;
    const LayerSnapshot& snapshot() const noexcept { return snap; }

    void noteOn (int note, float velocity, int midiChannel = 1);
    void noteOff (int note, int midiChannel = 1);
    void setSustain (bool on);
    void polyAftertouch (int note, float value, int midiChannel = 0);
    void setMidiExpression (int midiChannel, float bend, float bendSensitivity, float tuning,
                            float pressure, float timbre, bool mpeMember, float zoneBend = 0.0f,
                            float zoneBendSensitivity = 2.0f);
    void allNotesOff (bool immediate);
    // Velocity mode / curve applied to an incoming 0..1 note velocity.
    float shapeVelocity (float velocity) const noexcept;

    // Downsample, pan, FX -> dest (stereo, host rate), in two steps so each layer's compressor can
    // key on the other layer: finishVoices() leaves the dry layer signal in getKeyBuffer(),
    // applyEffects() runs the chain.
    void finishVoices (int numHostSamples);
    void applyEffects (juce::AudioBuffer<float>& dest, int numHostSamples, const juce::AudioBuffer<float>* otherLayerKey);
    const juce::AudioBuffer<float>& getKeyBuffer() const noexcept { return keyBuffer; }

    // Editor feedback (any thread): the normalised modulation offset of a parameter as last
    // applied (newest voice / layer effects), the MSEG play position of the newest voice
    // (-1 while silent) and the compressor's gain reduction.
    float getModulationOffset (int parameterIndex) const noexcept;
    float getMsegPosition (int index) const noexcept { return uiMsegPhase[(size_t) juce::jlimit (0, 1, index)].load (std::memory_order_relaxed); }
    float getCompressorReductionDb() const noexcept { return uiCompReduction.load (std::memory_order_relaxed); }

    int getActiveVoiceCount() const noexcept;
    Layer getId() const noexcept { return id; }
    ModMatrix& getModMatrix() noexcept { return modMatrix; }
    const ModMatrix& getModMatrix() const noexcept { return modMatrix; }

    // Test hooks
    const std::vector<Voice*>& getVoices() const noexcept { return voices; }
    int getUnitSize() const noexcept { return snap.binaural == BinauralMode::x2 ? 4 : snap.binaural == BinauralMode::x1 ? 2 : 1; }

private:
    friend class SynthEngine;
    int numUnits() const noexcept { return (int) voices.size() / getUnitSize(); }
    Voice& unitVoice (int unit, int sideIndex) noexcept { return *voices[(size_t) (unit * getUnitSize() + sideIndex)]; }
    bool unitActive (int unit) const noexcept;
    bool unitGateOn (int unit) const noexcept;
    int unitNote (int unit) const noexcept;
    uint64_t unitAge (int unit) const noexcept;
    int pickUnit() noexcept;
    int findUnitPlaying (int note, int midiChannel) const noexcept;
    void startUnit (int unit, int note, float velocity, float pitchOffset, int stackIndex, int stackSize,
                    bool retrigger, int midiChannel = 1);
    void releaseUnit (int unit) noexcept;
    float unisonOffset (int k) const noexcept;
    int availableUnisonUnits() const noexcept;
    bool isMono() const noexcept { return snap.voiceMode == VoiceMode::solo || snap.voiceMode == VoiceMode::legato; }
    void monoNoteOn (int note, float velocity, int midiChannel);
    void monoNoteOff (int note, int midiChannel);
    void releaseHeldBySustainEnd();
    bool fxModuleEnabled (FxModule module, const LayerSnapshot& fx) const noexcept;
    void processFxModule (FxModule module, juce::AudioBuffer<float>& buffer, int start, int numHostSamples,
                          const LayerSnapshot& fx, double delaySeconds, const juce::AudioBuffer<float>* key) noexcept;
    void applyPanGain (float pan, int numHostSamples) noexcept;
    static constexpr int kFxControlBlock = 32;   // host samples between effect parameter updates
    void resetFxModule (FxModule module) noexcept;

    Layer id;
    ModMatrix modMatrix;
    const ModMatrixData* blockModMatrix { nullptr };   // acquired once per block (audio thread)
    LayerParamReader reader;
    LayerSnapshot snap;
    VoiceContext ctx;
    std::vector<Voice*> voices;
    uint64_t* ageCounter { nullptr };
    const WaveBank* voiceWaveBank { nullptr };
    bool voicesReassigned { true };
    LayerSnapshot candidate;
    bool sustainBeforeCommit { false };
    bool suspended { false };
    float suspendGain { 1.0f };
    int oversamplingFactor { kDefaultOversamplingFactor };
    bool asleep { false };
    int quietSamples { 0 };
    void updateSleep (const juce::AudioBuffer<float>& output, int numHostSamples) noexcept;
    uint64_t localAge { 0 };

    std::vector<int> heldNotes;        // physically held, in press order
    std::vector<int> heldChannels;
    std::vector<int> sustainedNotes;   // released while sustain/hold active
    std::vector<int> sustainedChannels;
    std::vector<float> heldVelocities;
    std::vector<int> sustainScratch;
    static constexpr size_t kMaxNoteKeys = 128 * 16;   // every note on every MIDI channel
    bool pedal { false };
    BinauralMode lastBinaural { BinauralMode::x2 };
    VoiceMode lastVoiceMode { VoiceMode::poly1 };
    int monoUnits { 0 };

    double hostRate { 44100.0 };
    int maxBlock { 512 };
    VoiceRenderPool* renderPool { nullptr };
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> hostScratch;
    juce::AudioBuffer<float> parallelScratch;
    juce::dsp::AudioBlock<float> osBlock;
    double lfo2MasterPhase { 0.0 };
    std::array<double, 2> msegMasterPhase {};

    AirShelf air;
    StereoChorus chorus;
    StereoDelay delay;
    StereoReverb reverb;
    Distortion distortion;
    ThreeBandEq eq;
    Compressor compressor;
    Phaser phaser;
    Flanger flanger;
    juce::AudioBuffer<float> keyBuffer, fxDry;
    std::array<float, ModMatrixData::kFxModules> fxFade {};   // on/off crossfade, -1 = not started
    std::array<bool, ModMatrixData::kFxModules> fxIdle {};
    std::array<float, ModMatrixData::sourceCount> layerSources {};   // modulation sources of the effects
    std::array<float, kLayerParamCount> fxPrevious {}, fxTarget {};    // normalised layer destinations
    bool fxTargetsValid { false };
    std::array<float, 2> panGain { 1.0f, 1.0f };
    bool panGainValid { false };
    juce::Random rng;

    std::array<std::atomic<float>, kLayerParamCount> uiModOffset {};
    std::array<std::atomic<float>, 2> uiMsegPhase {};
    std::atomic<float> uiCompReduction { 0.0f };
};
} // namespace nsynth
