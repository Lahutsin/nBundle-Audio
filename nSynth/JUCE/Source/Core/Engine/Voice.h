#pragma once

#include "LayerSnapshot.h"
#include "ModMatrix.h"
#include "../DSP/Dds1.h"
#include "../DSP/Dds2.h"
#include "../DSP/Envelope.h"
#include "../DSP/Lfo.h"
#include "../DSP/VoiceFilter.h"

namespace nsynth
{
// Values shared by all voices of a layer for the current block.
struct VoiceContext
{
    double bpm { 120.0 };
    float layerPitchOffset { 0.0f };  // octave, transpose, detunes, fine tune (semitones)
    float bend { 0.0f };              // -1..1 (MIDI pitch bend)
    float ribbonSemitones { 0.0f };   // ribbon pitch bend, only for held notes
    float modLever { 0.0f };          // bender pushed up / CC1, 0..1
    float channelPressure { 0.0f };   // 0..1
    float expression { 0.0f };        // 0..1
};

struct SmoothedValue
{
    float current { 0.0f };
    bool initialised { false };
    float next (float target, float coef) noexcept
    {
        if (! initialised) { current = target; initialised = true; }
        current += (target - current) * coef;
        return current;
    }
};

// One hardware voice (a mono signal path). A binaural "super voice" is two (x1) or
// four (x2, a detuned pair per ear) of these playing the same note on the left and right channel.
class Voice
{
public:
    enum class Side { mono, left, right };

    struct StartInfo
    {
        int note { 60 };
        int midiChannel { 1 };
        float velocity { 1.0f };
        float pitchOffset { 0.0f };   // unison octave/fifth stacking
        Side side { Side::mono };
        float pan { 0.0f };           // -1..1, only for Side::mono
        double lfo1Phase { -1.0 };    // < 0: keep the free-running phase
        double lfo2Phase { 0.0 };     // layer LFO2 phase => voices start in sync
        bool retrigger { true };      // false: legato (no envelope retrigger)
        uint64_t age { 0 };
        float twinSign { 0.0f };      // binaural x2: -1 / +1 x BINAURAL DETUNE
        float gain { 1.0f };
    };

    static constexpr float kOutputGain = 0.2f;

    void prepare (double internalSampleRate, const WaveBank* bank, uint32_t seed);
    void setSampleRate (double internalSampleRate);
    void setWaveBank (const WaveBank* bank) noexcept { waveBank = bank; dds1.setWaveBank (bank); }

    void start (const StartInfo& info, const LayerSnapshot& p);
    // Mono modes: move to another note (glide), optionally retriggering envelopes.
    void changeNote (int note, float velocity, bool retrigger, uint64_t age, const LayerSnapshot& p,
                     int midiChannel = 1);
    void release() noexcept;
    void curtail() noexcept;   // POLY 2: shorten an overlapping release
    void kill() noexcept;

    void render (float* outL, float* outR, int startSample, int numSamples, const LayerSnapshot& p,
                 const VoiceContext& ctx, const ModMatrixData& matrix) noexcept;

    bool isActive() const noexcept { return active; }
    bool isGateOn() const noexcept { return active && gate; }
    int getNote() const noexcept { return note; }
    int getMidiChannel() const noexcept { return midiChannel; }
    uint64_t getAge() const noexcept { return age; }
    Side getSide() const noexcept { return side; }
    double getLfo1Phase() const noexcept { return lfo1.getPhase(); }
    float getCurrentPitch() const noexcept { return (float) currentPitch; }
    void setAftertouch (float v) noexcept { aftertouch = v; }
    void setMidiExpression (float bend, float bendSensitivity, float tuning, float pressure, float timbre,
                            bool mpeMember) noexcept
    {
        midiPitchBend = bend;
        midiBendSensitivity = bendSensitivity;
        channelTuning = tuning;
        channelPressure = pressure;
        midiTimbre = timbre;
        isMpeMember = mpeMember;
    }
    const std::array<float, ModMatrixData::sourceCount>& getLastModSources() const noexcept { return lastModSources; }

    // Exposed for tests / UI metering.
    const Envelope& getEnv1() const noexcept { return env1; }
    const Envelope& getAmpEnvelope() const noexcept { return ampEnvUsesEnv2 ? env2 : fixedEnv; }

    static float rangeOffset (Dds1Range r) noexcept;
    static float rangeOffset (Dds2Range r) noexcept;
    static double noteToHz (double semitones) noexcept { return 440.0 * std::exp2 ((semitones - 69.0) / 12.0); }
    static float portamentoSecondsPerOctave (float fader) noexcept { return 10.0f * fader * fader * fader; }
    static float triStateAmount (TriState t) noexcept { return t == TriState::off ? 0.0f : t == TriState::half ? 0.5f : 1.0f; }
    static float fastExp2 (float x) noexcept;
    static constexpr float kOpenStretchHz = 8000.0f;  // see openLadderCutoff()
    static constexpr float kLpfMaxHz = 25000.0f;      // top of the LPF CUTOFF range

private:
    void applyEnvelopeSettings (const LayerSnapshot& p) noexcept;
    void triggerEnvelopes() noexcept;
    float lfo1RateHz (const LayerSnapshot& p, const VoiceContext& ctx) const noexcept;
    float driftWalk (float& value, float& target, int& counter) noexcept;
    float openLadderCutoff (float hz) noexcept;

    double sampleRate { 44100.0 };
    const WaveBank* waveBank { nullptr };
    juce::Random rng;

    Dds1 dds1;
    Dds2 dds2;
    VoiceFilter filter;
    Envelope env1, env2, fixedEnv;
    Lfo lfo1, lfo2;
    DelayRamp lfo1Ramp, lfo2Ramp;

    bool active { false }, gate { false }, ampEnvUsesEnv2 { true };
    int note { 60 }, midiChannel { 1 };
    float velocity { 1.0f }, aftertouch { 0.0f };
    float midiPitchBend { 0.0f }, midiBendSensitivity { 2.0f }, channelTuning { 0.0f };
    float channelPressure { 0.0f }, midiTimbre { 0.0f };
    bool isMpeMember { false };
    float pitchOffset { 0.0f };
    float twinSign { 0.0f }, voiceGain { 1.0f };
    // double: per-sample glide steps (~1e-5 st) would be distorted by float rounding.
    double currentPitch { 60.0 }, targetPitch { 60.0 };
    bool hasPlayed { false };
    Side side { Side::mono };
    float panL { 0.70710678f }, panR { 0.70710678f };
    uint64_t age { 0 };
    float envTimeScale { 1.0f };
    float lastDds1 { 0.0f }, delayedDds1 { 0.0f };
    float openTopHz { 25000.0f }, openExponent { 1.0f }, stretchInput { -1.0f }, stretchOutput { 0.0f };

    // drift random walks: dds1 pitch, dds2 pitch, cutoff
    float driftValue[3] {}, driftTarget[3] {};
    int driftCounter[3] {};

    SmoothedValue sLpf, sHpf, sRes, sMix, sVcaEnv, sMorph, sDetune, sCross, sTune;
    std::array<float, ModMatrixData::sourceCount> lastModSources {};
};
} // namespace nsynth
