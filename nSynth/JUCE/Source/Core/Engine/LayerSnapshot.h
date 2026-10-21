#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Params/ParamRegistry.h"

namespace nsynth
{
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
    float crossMod { 0.0f }, drift { 0.0f };
    // VCF
    float hpfHz { 10.0f }, lpfHz { 25000.0f }, res { 0.0f };
    Drive drive { Drive::off };
    float vcfEnv { 0.0f };
    EnvSource vcfEnvSource { EnvSource::env1 };
    float vcfLfo1 { 0.0f }, vcfDds2 { 0.0f };
    TriState vcfKeytrack { TriState::off };
    // VCA
    float vcaEnv { 1.0f }, vcaLfo1 { 0.0f }, vcaDds2 { 0.0f };
    VcaEnvMode vcaEnvMode { VcaEnvMode::env2 };
    TriState vcaDynamics { TriState::off };
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
    // Performance control
    Lfo2Wave lfo2Wave { Lfo2Wave::sine };
    float lfo2Rate { 0.5f }, lfo2Delay { 0.0f };
    Lfo2Trigger lfo2Trigger { Lfo2Trigger::trig };
    float lfo2RateMod { 0.0f }, lfo2Dds { 0.0f }, lfo2Vcf { 0.0f }, lfo2Vca { 0.0f };
    OscTarget perfDest { OscTarget::both };
    float bendDds { 2.0f }, bendVcf { 0.0f }, portamento { 0.0f };
    int octave { 0 };
    // Voice assign
    VoiceMode voiceMode { VoiceMode::poly1 };
    bool unison { false };
    UnisonSize unisonSize { UnisonSize::all };
    bool binaural { true };
    // Arp / seq
    bool arpOn { false };
    ArpMode arpMode { ArpMode::up };
    int arpRange { 1 };
    ClockDiv clockDiv { ClockDiv::sixteenth };
    bool clockSync { false };
    int swing { 0 }, seqSlot { 1 };
    bool hold { false };
    // FX
    ChorusMode chorus { ChorusMode::off };
    float delayTime { 0.5f }, delayFeedback { 0.3f }, delaySend { 0.0f };
    bool delayFreeze { false };
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
    Quality quality { Quality::x4 };
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
    std::array<std::atomic<float>*, 81> values {};
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
    std::array<std::atomic<float>*, 16> values {};
};
} // namespace nsynth
