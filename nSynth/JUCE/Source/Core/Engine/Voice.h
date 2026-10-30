#pragma once

#include "LayerSnapshot.h"
#include "../DSP/FastMath.h"
#include "ModMatrix.h"
#include "../DSP/Dds1.h"
#include "../DSP/Dds2.h"
#include "../DSP/Envelope.h"
#include "../DSP/Lfo.h"
#include "../DSP/VoiceFilter.h"
#include "../DSP/Filter2.h"
#include "../DSP/Halfband.h"

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
        float width { 1.0f };         // left/right voices: 1 = hard left/right, 0 = centred
        double lfo1Phase { -1.0 };    // < 0: keep the free-running phase
        double lfo2Phase { 0.0 };     // layer LFO2 phase => voices start in sync
        bool retrigger { true };      // false: legato (no envelope retrigger)
        uint64_t age { 0 };
        float twinSign { 0.0f };      // binaural x2: -1 / +1 x BINAURAL DETUNE
        float detuneSemitones { 0.0f }; // unison spread of this stacked voice
        float gain { 1.0f };
        std::array<double, kMaxMsegs> msegPhase {}; // layer clock phases for MSEG FREE mode
    };

    // One full-level voice peaks around -5 dBFS; stacked chords are caught by the output limiter.
    static constexpr float kOutputGain = 0.6f;

    void prepare (double internalSampleRate, const WaveBank* bank, uint32_t seed);
    static void initialiseStaticTables();   // see SynthLayer::initialiseStaticTables()
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
    bool isAudible() const noexcept;
    int getNote() const noexcept { return note; }
    int getMidiChannel() const noexcept { return midiChannel; }
    uint64_t getAge() const noexcept { return age; }
    Side getSide() const noexcept { return side; }
    double getLfo1Phase() const noexcept { return lfo1.getPhase(); }
    float getCurrentPitch() const noexcept { return (float) currentPitch; }
    void setAftertouch (float v) noexcept { aftertouch = v; }
    // zoneBend / zoneBendSensitivity: the MPE zone master channel's bend (member channels only).
    void setMidiExpression (float bend, float bendSensitivity, float tuning, float pressure, float timbre,
                            bool mpeMember, float zoneBend = 0.0f, float zoneBendSensitivity = 2.0f) noexcept
    {
        midiPitchBend = bend;
        midiBendSensitivity = bendSensitivity;
        channelTuning = tuning;
        channelPressure = pressure;
        midiTimbre = timbre;
        isMpeMember = mpeMember;
        masterBend = zoneBend;
        masterBendSensitivity = zoneBendSensitivity;
    }
    const std::array<float, ModMatrixData::sourceCount>& getLastModSources() const noexcept { return lastModSources; }
    // Normalised offset the matrix added to each modulated per-voice parameter in the last block
    // (only the entries of ModMatrixData::modulated are current). For the editor's modulation rings.
    const std::array<float, kLayerParamCount>& getModOffsets() const noexcept { return modOffsets; }
    float getMsegPhase (int index) const noexcept { return (float) msegPhase[(size_t) juce::jlimit (0, kMaxMsegs - 1, index)]; }

    // Exposed for tests / UI metering.
    const Envelope& getEnv1() const noexcept { return env1; }
    const Envelope& getAmpEnvelope() const noexcept { return ampEnvUsesEnv2 ? env2 : fixedEnv; }

    static float rangeOffset (Dds1Range r) noexcept;
    static float rangeOffset (Dds2Range r) noexcept;
    static double noteToHz (double semitones) noexcept { return 440.0 * fastmath::exp2 ((semitones - 69.0) / 12.0); }
    static float portamentoSecondsPerOctave (float fader) noexcept { return 10.0f * fader * fader * fader; }
    static float triStateAmount (TriState t) noexcept { return t == TriState::off ? 0.0f : t == TriState::half ? 0.5f : 1.0f; }
    static float fastExp2 (float x) noexcept;
    static constexpr float kOpenStretchHz = 8000.0f;  // see openLadderCutoff()
    static constexpr float kMaxLinearIndex = 8.0f;    // linear CROSS MOD at 1 (quadratic taper)
    static constexpr float kLpfMaxHz = 25000.0f;      // top of the LPF CUTOFF range

private:
    // A stolen voice that is still sounding fades out over kStealFadeSeconds before it starts the
    // new note from silence: jumping to another pitch, level, pan or detune at full level clicks.
    static constexpr double kStealFadeSeconds = 0.004;
    bool needsStealFade (const StartInfo& info) const noexcept;
    void begin (const StartInfo& info, const LayerSnapshot& p, bool fromSilence);
    void applyEnvelopeSettings (const LayerSnapshot& p) noexcept;
    void triggerEnvelopes() noexcept;
    void restartMsegs (const LayerSnapshot& p, const std::array<double, kMaxMsegs>& freePhase) noexcept;
    float lfo1RateHz (const LayerSnapshot& p, float rateFader, const VoiceContext& ctx) const noexcept;
    float driftWalk (float& value, float& target, int& counter) noexcept;
    float pitchNoise (size_t oscillator) noexcept;
    float openLadderCutoff (float hz) noexcept;

    double sampleRate { 44100.0 };
    const WaveBank* waveBank { nullptr };
    juce::Random rng;

    Dds1 dds1;
    Dds2 dds2;
    VoiceFilter filter;
    Filter2 filter2;
    HalfbandResampler upsampler, downsampler; // the VCF and FILTER 2 run at twice the voice rate
    Envelope env1, env2, fixedEnv;
    Lfo lfo1, lfo2;
    DelayRamp lfo1Ramp, lfo2Ramp;

    bool active { false }, gate { false }, ampEnvUsesEnv2 { true };
    int note { 60 }, midiChannel { 1 };
    float velocity { 1.0f }, aftertouch { 0.0f };
    float midiPitchBend { 0.0f }, midiBendSensitivity { 2.0f }, channelTuning { 0.0f };
    float channelPressure { 0.0f }, midiTimbre { 0.0f };
    bool isMpeMember { false };
    float masterBend { 0.0f }, masterBendSensitivity { 2.0f };
    float pitchOffset { 0.0f };
    float twinSign { 0.0f }, voiceGain { 1.0f }, unisonDetune { 0.0f };
    // double: per-sample glide steps (~1e-5 st) would be distorted by float rounding.
    double currentPitch { 60.0 }, targetPitch { 60.0 };
    bool hasPlayed { false };
    Side side { Side::mono };
    float panL { 0.70710678f }, panR { 0.70710678f };
    uint64_t age { 0 };
    float envTimeScale { 1.0f };
    float lastDds1 { 0.0f };
    std::array<float, Dds2::kLatency> dds1Delay {}; // aligns DDS1 with the BLEP-delayed DDS2
    int dds1DelayIndex { 0 };
    float openTopHz { 25000.0f }, openExponent { 1.0f }, stretchInput { -1.0f }, stretchOutput { 0.0f };

    // drift random walks: dds1 pitch, dds2 pitch, cutoff
    float driftValue[3] {}, driftTarget[3] {};
    int driftCounter[3] {};

    // ANALOG: this voice's component tolerances (-1..1, scaled by ANALOG), fixed per voice slot
    // like the parts of a voice card, and the oscillators' 1/f pitch noise.
    struct Tolerances
    {
        float dds1Tune { 0.0f }, dds2Tune { 0.0f }, cutoff { 0.0f }, resonance { 0.0f }, level { 0.0f }, envelope { 0.0f };
    };
    static constexpr float kTuneSpreadCents = 4.0f, kCutoffSpreadOctaves = 0.12f, kResonanceSpread = 0.06f,
                           kLevelSpreadDb = 0.4f, kEnvelopeSpread = 0.08f, kPitchNoiseCents = 1.5f;
    static constexpr std::array<double, 3> kPitchNoiseBandsHz { 1.5, 7.0, 35.0 };
    // The pitch noise is band-limited to tens of Hz: it is computed every kNoiseInterval samples
    // and interpolated linearly in between.
    static constexpr int kNoiseInterval = 16;
    Tolerances tolerance;
    uint32_t noiseState { 1 };
    std::array<std::array<float, 3>, 2> noiseBands {};  // per oscillator, one low-pass per band
    std::array<float, 3> noiseCoefficient {}, noiseInputGain {};
    std::array<float, 2> noiseFrom {}, noiseTo {};
    int noiseCountdown { 0 };
    float levelAnalog { 0.0f }, levelGain { 1.0f };      // ANALOG level offset, recomputed when ANALOG moves

    SmoothedValue sLpf, sHpf, sRes, sMix, sVcaEnv, sMorph, sDetune, sCross, sTune, sF2Cutoff;
    std::array<float, ModMatrixData::sourceCount> lastModSources {};
    std::array<float, kLayerParamCount> modOffsets {};
    std::array<double, kMaxMsegs> msegPhase {};
    std::array<int, kMaxMsegs> msegHint {};
    float randomValue { 0.5f };   // per-note RANDOM source
    bool stealPending { false };
    StartInfo pendingStart;
    float stealGain { 1.0f };
    float lastLfo1 { 0.0f }, lastLfo2 { 0.0f };
    bool lfo1Unipolar { false };
};
} // namespace nsynth
