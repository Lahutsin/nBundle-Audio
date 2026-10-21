#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Voice.h"
#include "VoiceRenderPool.h"
#include "ModMatrix.h"
#include "SequenceBank.h"
#include "../DSP/Effects.h"

namespace nsynth
{
// One layer (UPPER or LOWER): note handling / voice assignment over the hardware
// voices it currently owns, oversampled rendering, pan and the chorus -> delay chain.
class SynthLayer
{
public:
    SynthLayer (juce::AudioProcessorValueTreeState& state, Layer id);

    void prepare (double hostSampleRate, int maxBlockSize, VoiceRenderPool& renderPool);
    void setQuality (Quality q);
    int getOversamplingFactor() const noexcept { return 1 << (toInt (quality) + 1); }
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
    void playSequenceStep (const SequenceStep& step, int transpose, bool accent);
    void releaseSequenceNotes() noexcept;

    // Render voices for host samples [hostStart, hostStart + hostNum) into the oversampled buffer.
    void renderSegment (int hostStart, int hostNum) noexcept;

    // Downsample, pan, FX -> dest (stereo, host rate).
    void finishBlock (juce::AudioBuffer<float>& dest, int numHostSamples);

    int getActiveVoiceCount() const noexcept;
    Layer getId() const noexcept { return id; }
    ModMatrix& getModMatrix() noexcept { return modMatrix; }
    const ModMatrix& getModMatrix() const noexcept { return modMatrix; }

    // Test hooks
    const std::vector<Voice*>& getVoices() const noexcept { return voices; }
    int getUnitSize() const noexcept { return snap.binaural ? 2 : 1; }

private:
    static constexpr int kMaxRenderVoices = 20;

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
    std::array<int, SequenceStep::kMaxNotes> sequenceUnits {}, sequenceNotes {};
    int sequenceUnitCount { 0 };
    bool pedal { false };
    bool lastBinaural { true };
    VoiceMode lastVoiceMode { VoiceMode::poly1 };
    int monoUnits { 0 };

    double hostRate { 44100.0 };
    int maxBlock { 512 };
    Quality quality { Quality::x4 };
    VoiceRenderPool* renderPool { nullptr };
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;
    juce::AudioBuffer<float> hostScratch;
    juce::AudioBuffer<float> parallelScratch;
    juce::dsp::AudioBlock<float> osBlock;
    double lfo2MasterPhase { 0.0 };

    StereoChorus chorus;
    StereoDelay delay;
    juce::Random rng;
};
} // namespace nsynth
