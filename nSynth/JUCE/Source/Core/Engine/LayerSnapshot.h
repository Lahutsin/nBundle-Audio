#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Params/ParamRegistry.h"

namespace nsynth
{
inline constexpr size_t kLayerParamCount = 220;

// MSEG settings default: the first value for MSEG 1, the second for every other one.
template <typename T>
constexpr std::array<T, kMaxMsegs> msegDefaults (T first, T others) noexcept
{
    std::array<T, kMaxMsegs> values {};
    values[0] = first;
    for (size_t i = 1; i < values.size(); ++i)
        values[i] = others;
    return values;
}

// Plain-value copy of one layer's parameters, refreshed once per audio block.
struct LayerSnapshot
{
    // DDS 1 / 2
    Dds1Wave dds1Wave { Dds1Wave::saw };
    Dds1Range dds1Range { Dds1Range::ft8 };
    int altA { 0 }, altB { 1 };
    Dds2Wave dds2Wave { Dds2Wave::saw };
    Dds2Range dds2Range { Dds2Range::ft8 };
    float dds2Tune { 0.0f };
    Dds2Mode dds2Mode { Dds2Mode::normOrSubOff };
    // Mixer
    float mix { 0.0f }, pan { 0.0f };
    // DDS modulator
    float modLfo1 { 0.0f }, modEnv1 { 0.0f };
    OscTarget modDest { OscTarget::both };
    SuperMode superMode { SuperMode::off };
    float pwDetune { 0.0f }, pwmWave { 0.0f };
    PwmSource pwmSource { PwmSource::manual };
    float crossMod { 0.0f }, drift { 0.0f }, analog { 0.0f };
    CrossModMode crossModMode { CrossModMode::exponential };
    float eqLowQ { 0.7071068f }, eqHighQ { 0.7071068f };   // version 4
    // VCF
    float hpfHz { 10.0f }, lpfHz { 25000.0f }, res { 0.0f };
    Drive drive { Drive::off };
    FilterType vcfType { FilterType::ladder24 };
    float vcfEnv { 0.0f };
    EnvSource vcfEnvSource { EnvSource::env1 };
    float vcfLfo1 { 0.0f }, vcfDds2 { 0.0f };
    TriState vcfKeytrack { TriState::off };
    // VCA
    float vcaEnv { 0.75f }, vcaLfo1 { 0.0f }, vcaDds2 { 0.0f };
    VcaEnvMode vcaEnvMode { VcaEnvMode::env2 };
    // Envelopes
    float env1AttackHold { 0.0f }, env1Attack { 0.001f }, env1DecayHold { 0.0f }, env1Decay { 0.5f },
          env1Sustain { 0.5f }, env1Release { 0.3f };
    Env1Mode env1Mode { Env1Mode::normal };
    TriState env1Keytrack { TriState::off };
    float env2Attack { 0.001f }, env2DecayHold { 0.0f }, env2Decay { 0.5f }, env2Sustain { 1.0f }, env2Release { 0.1f };
    // LFO 1
    Lfo1Wave lfo1Wave { Lfo1Wave::triangle };
    float lfo1Rate { 0.5f }, lfo1Delay { 0.0f }, lfo1LrPhase { 0.0f };
    Lfo1Mode lfo1Mode { Lfo1Mode::freeOrNorm };
    bool lfo1Sync { false };
    // Performance control
    Lfo2Wave lfo2Wave { Lfo2Wave::sine };
    float lfo2Rate { 0.5f }, lfo2Delay { 0.0f };
    Lfo2Trigger lfo2Trigger { Lfo2Trigger::trig };
    float lfo2RateMod { 0.0f }, lfo2Dds { 0.0f }, lfo2Vcf { 0.0f }, lfo2Vca { 0.0f };
    OscTarget perfDest { OscTarget::both };
    float bendDds { 2.0f }, bendVcf { 0.0f }, portamento { 0.0f };
    int octave { 0 };
    // Voice assign / velocity
    VoiceMode voiceMode { VoiceMode::poly1 };
    bool unison { false };
    UnisonSize unisonSize { UnisonSize::all };
    BinauralMode binaural { BinauralMode::x2 };
    float binauralDetune { 7.0f };   // cents
    float stereoWidth { 0.65f };     // binaural voice panning, 1 = hard left/right
    bool hold { false };
    VelocityMode velMode { VelocityMode::fixed };
    int velFixed { 100 };
    float velCurve { 0.0f }, velSens { 0.5f };
    // Chorus
    ChorusMode chorus { ChorusMode::off };
    float chorusRate { 0.5f }, chorusDepth { 0.5f }, chorusDelayMs { 3.5f }, chorusFeedback { 0.0f },
          chorusWidth { 1.0f }, chorusMix { 0.5f };
    // Delay
    bool delaySync { true };
    float delayMs { 350.0f };
    DelayDivision delayDivision { DelayDivision::d1_4 };
    float delayFeedback { 0.35f }, delayMix { 0.0f };
    bool delayPingPong { false };
    float delayLowCutHz { 120.0f }, delayHighCutHz { 9000.0f };
    bool delayFreeze { false };
    // Reverb
    float reverbMix { 0.0f }, reverbSize { 0.6f }, reverbDecay { 2.5f }, reverbPreDelayMs { 15.0f },
          reverbDampingHz { 9000.0f }, reverbLowCutHz { 150.0f }, reverbWidth { 1.0f }, reverbModulation { 0.3f };

    // ---- version 2 -----------------------------------------------------------------------
    std::array<float, 8> macros {};
    // MSEG 1 and 2 are version 2 parameters, MSEG 3..16 version 4.
    std::array<MsegMode, kMaxMsegs> msegMode { msegDefaults (MsegMode::envelope, MsegMode::loop) };
    std::array<bool, kMaxMsegs> msegSync { msegDefaults (false, true) };
    std::array<float, kMaxMsegs> msegTime { msegDefaults (1.0f, 1.0f) };
    std::array<MsegDivision, kMaxMsegs> msegDivision { msegDefaults (MsegDivision::bar1, MsegDivision::bar1) };
    Filter2Routing f2Routing { Filter2Routing::off };
    Filter2Type f2Type { Filter2Type::lp12 };
    float f2Cutoff { 2000.0f }, f2Res { 0.2f }, f2Env { 0.0f };
    EnvSource f2EnvSource { EnvSource::env2 };
    float f2Keytrack { 0.0f }, f2Drive { 0.0f }, f2Mix { 1.0f };
    bool delayOn { true };
    float delayDuck { 0.0f };
    bool reverbOn { true };
    float reverbDuck { 0.0f };
    bool distOn { false };
    DistortionType distType { DistortionType::soft };
    float distDrive { 0.3f }, distTone { 0.7f }, distMix { 1.0f };
    bool eqOn { false };
    float eqLowFreq { 150.0f }, eqLowGain { 0.0f }, eqMidFreq { 1000.0f }, eqMidGain { 0.0f }, eqMidQ { 0.7f },
          eqHighFreq { 6000.0f }, eqHighGain { 0.0f };
    bool compOn { false };
    float compThreshold { -18.0f }, compRatio { 4.0f }, compAttackMs { 5.0f }, compReleaseMs { 120.0f }, compMakeup { 0.0f };
    CompressorKey compKey { CompressorKey::self };
    float compMix { 1.0f };
    bool phaserOn { false };
    float phaserRate { 0.4f }, phaserDepth { 0.6f }, phaserFeedback { 0.4f }, phaserMix { 0.5f };
    bool flangerOn { false };
    float flangerRate { 0.25f }, flangerDepth { 0.6f }, flangerFeedback { 0.5f }, flangerMix { 0.5f };

    // The float field holding continuous layer parameter `specIndex` (getLayerParamSpecs() order),
    // nullptr for choice, int and bool parameters. Used to apply modulation to any parameter.
    float* floatField (int specIndex) noexcept;
    const float* floatField (int specIndex) const noexcept { return const_cast<LayerSnapshot*> (this)->floatField (specIndex); }
};

struct PerformanceSnapshot
{
    KeyboardMode keyboardMode { KeyboardMode::single };
    Layer singleLayer { Layer::upper };
    int splitPoint { 60 };
    float lowerDetune { 0.0f }, perfDetune { 0.0f };
    int transpose { 0 };
    float fineCents { 0.0f };
    float tempoParam { 120.0f };
    float masterDb { 0.0f };
    float ribbon { 0.0f }, modLever { 0.0f }, expression { 0.0f };
    MidiRouting midiRouting { MidiRouting::keyboardMode };
    bool dualPedal { false };
    bool hardwareCCMap { false };
};

// Caches the raw parameter pointers of one layer so the audio thread never does string or hash lookups.
class LayerParamReader
{
public:
    LayerParamReader (juce::AudioProcessorValueTreeState& state, Layer layer);
    void read (LayerSnapshot& out) const noexcept;

private:
    float get (size_t index) const noexcept
    {
        return values[index]->load (std::memory_order_relaxed);
    }
    std::array<std::atomic<float>*, kLayerParamCount> values {};
};

class PerformanceParamReader
{
public:
    explicit PerformanceParamReader (juce::AudioProcessorValueTreeState& state);
    void read (PerformanceSnapshot& out) const noexcept;

private:
    float get (size_t index) const noexcept
    {
        return values[index]->load (std::memory_order_relaxed);
    }
    std::array<std::atomic<float>*, 15> values {};
};
} // namespace nsynth
