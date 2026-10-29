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
    static constexpr int kOversamplingFactor = 2;
    static constexpr int kMaxVoices = 128;

    SynthLayer (juce::AudioProcessorValueTreeState& state, Layer id);

    void prepare (double hostSampleRate, int maxBlockSize, VoiceRenderPool& renderPool);
    static constexpr int getOversamplingFactor() noexcept { return kOversamplingFactor; }
    float getLatencySamples() const noexcept;

    // Voices owned by this layer (changes with SINGLE/DUAL/SPLIT). Kills current notes.
    void assignVoices (Voice* first, int count);
    int getVoiceCount() const noexcept { return (int) voices.size(); }

    void beginBlock (int numHostSamples, const VoiceContext& context, uint64_t* sharedAgeCounter);
    const LayerSnapshot& snapshot() const noexcept { return snap; }

    void noteOn (int note, float velocity, int midiChannel = 1);
    void noteOff (int note, int midiChannel = 1);
    void setSustain (bool on);
    void polyAftertouch (int note, float value, int midiChannel = 0);
    void setMidiExpression (int midiChannel, float bend, float bendSensitivity, float tuning,
                            float pressure, float timbre, bool mpeMember);
    void allNotesOff (bool immediate);
    // Velocity mode / curve applied to an incoming 0..1 note velocity.
    float shapeVelocity (float velocity) const noexcept;

    // Render voices for host samples [hostStart, hostStart + hostNum) into the oversampled buffer.
    void renderSegment (int hostStart, int hostNum) noexcept;

    // Downsample, pan, FX -> dest (stereo, host rate).
    void finishBlock (juce::AudioBuffer<float>& dest, int numHostSamples);
    // finishBlock() in two steps, so each layer's compressor can key on the other layer:
    // finishVoices() leaves the dry layer signal in getKeyBuffer(), applyEffects() runs the chain.
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
    static constexpr int kMaxRenderVoices = kMaxVoices;

    struct Unit { int index; };
    struct VoiceRenderTask
    {
        SynthLayer* layer { nullptr };
        std::array<Voice*, kMaxRenderVoices> activeVoices {};
        int activeVoiceCount { 0 };
        int laneCount { 0 };
        int startSample { 0 };
        int numSamples { 0 };
    };

    static void renderVoiceRange (void* context, int lane) noexcept;
    static void renderSingleVoice (void* context, int itemIndex, int lane) noexcept;
    friend class SynthEngine;
    int numUnits() const noexcept { return (int) voices.size() / getUnitSize(); }
    Voice& unitVoice (int unit, int sideIndex) noexcept { return *voices[(size_t) (unit * getUnitSize() + sideIndex)]; }
    bool unitActive (int unit) const noexcept;
    bool unitGateOn (int unit) const noexcept;
    int unitNote (int unit) const noexcept;
    uint64_t unitAge (int unit) const noexcept;
    int pickUnit (int excludeNote = -1) noexcept;
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
    void processFxModule (FxModule module, juce::AudioBuffer<float>& buffer, int numHostSamples, const LayerSnapshot& fx,
                          double delaySeconds, const juce::AudioBuffer<float>* key) noexcept;
    void resetFxModule (FxModule module) noexcept;

    Layer id;
    ModMatrix modMatrix;
    std::shared_ptr<const ModMatrixData> blockModMatrix;
    LayerParamReader reader;
    LayerSnapshot snap;
    VoiceContext ctx;
    std::vector<Voice*> voices;
    uint64_t* ageCounter { nullptr };
    uint64_t localAge { 0 };

    std::vector<int> heldNotes;        // physically held, in press order
    std::vector<int> heldChannels;
    std::vector<int> sustainedNotes;   // released while sustain/hold active
    std::vector<int> sustainedChannels;
    std::vector<float> heldVelocities;
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
    juce::Random rng;

    std::array<std::atomic<float>, kLayerParamCount> uiModOffset {};
    std::array<std::atomic<float>, 2> uiMsegPhase {};
    std::atomic<float> uiCompReduction { 0.0f };
};
} // namespace nsynth
