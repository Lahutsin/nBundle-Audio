#include "LayerSnapshot.h"

namespace nsynth
{
LayerParamReader::LayerParamReader (juce::AudioProcessorValueTreeState& state, Layer layer)
{
    const auto& specs = getLayerParamSpecs();
    jassert (specs.size() == values.size());
    for (size_t i = 0; i < specs.size() && i < values.size(); ++i)
    {
        auto* raw = state.getRawParameterValue (layerParamId (layer, specs[i].suffix));
        jassert (raw != nullptr);
        values[i] = raw;
    }
}

void LayerParamReader::read (LayerSnapshot& o) const noexcept
{
    size_t idx = 0;
    auto f = [&] () noexcept { return get (idx++); };
    auto e = [&] () noexcept { return (int) std::lround (f()); };
    auto b = [&] () noexcept { return f() > 0.5f; };

    o.dds1Wave = (Dds1Wave) e();
    o.dds1Range = (Dds1Range) e();
    o.altA = e() - 1;
    o.altB = e() - 1;
    o.dds2Wave = (Dds2Wave) e();
    o.dds2Range = (Dds2Range) e();
    o.dds2Tune = f();
    o.dds2Mode = (Dds2Mode) e();

    o.mix = f();
    o.pan = f();

    o.modLfo1 = f();
    o.modEnv1 = f();
    o.modDest = (OscTarget) e();
    o.superMode = (SuperMode) e();
    o.pwDetune = f();
    o.pwmWave = f();
    o.pwmSource = (PwmSource) e();
    o.crossMod = f();
    o.drift = f();

    o.hpfHz = f();
    o.lpfHz = f();
    o.res = f();
    o.drive = (Drive) e();
    o.vcfEnv = f();
    o.vcfEnvSource = (EnvSource) e();
    o.vcfLfo1 = f();
    o.vcfDds2 = f();
    o.vcfKeytrack = (TriState) e();

    o.vcaEnv = f();
    o.vcaLfo1 = f();
    o.vcaDds2 = f();
    o.vcaEnvMode = (VcaEnvMode) e();

    o.env1AttackHold = f();
    o.env1Attack = f();
    o.env1DecayHold = f();
    o.env1Decay = f();
    o.env1Sustain = f();
    o.env1Release = f();
    o.env1Mode = (Env1Mode) e();
    o.env1Keytrack = (TriState) e();
    o.env2Attack = f();
    o.env2DecayHold = f();
    o.env2Decay = f();
    o.env2Sustain = f();
    o.env2Release = f();

    o.lfo1Wave = (Lfo1Wave) e();
    o.lfo1Rate = f();
    o.lfo1Delay = f();
    o.lfo1LrPhase = f();
    o.lfo1Mode = (Lfo1Mode) e();
    o.lfo1Sync = b();

    o.lfo2Wave = (Lfo2Wave) e();
    o.lfo2Rate = f();
    o.lfo2Delay = f();
    o.lfo2Trigger = (Lfo2Trigger) e();
    o.lfo2RateMod = f();
    o.lfo2Dds = f();
    o.lfo2Vcf = f();
    o.lfo2Vca = f();
    o.perfDest = (OscTarget) e();
    o.bendDds = f();
    o.bendVcf = f();
    o.portamento = f();
    o.octave = e();

    o.voiceMode = (VoiceMode) e();
    o.unison = b();
    o.unisonSize = (UnisonSize) e();
    o.binaural = (BinauralMode) e();
    o.binauralDetune = f();
    o.hold = b();
    o.velMode = (VelocityMode) e();
    o.velFixed = e();
    o.velCurve = f();
    o.velSens = f();

    o.chorus = (ChorusMode) e();
    o.chorusRate = f();
    o.chorusDepth = f();
    o.chorusDelayMs = f();
    o.chorusFeedback = f();
    o.chorusWidth = f();
    o.chorusMix = f();

    o.delaySync = b();
    o.delayMs = f();
    o.delayDivision = (DelayDivision) e();
    o.delayFeedback = f();
    o.delayMix = f();
    o.delayPingPong = b();
    o.delayFreeze = b();
    o.delayLowCutHz = f();
    o.delayHighCutHz = f();

    o.reverbMix = f();
    o.reverbSize = f();
    o.reverbDecay = f();
    o.reverbPreDelayMs = f();
    o.reverbDampingHz = f();
    o.reverbLowCutHz = f();
    o.reverbWidth = f();
    o.reverbModulation = f();
    jassert (idx == values.size());
}

//==============================================================================
PerformanceParamReader::PerformanceParamReader (juce::AudioProcessorValueTreeState& state)
{
    const auto& specs = getPerformanceParamSpecs();
    jassert (specs.size() == values.size());
    for (size_t i = 0; i < specs.size() && i < values.size(); ++i)
    {
        auto* raw = state.getRawParameterValue (perfParamId (specs[i].suffix));
        jassert (raw != nullptr);
        values[i] = raw;
    }
}

void PerformanceParamReader::read (PerformanceSnapshot& o) const noexcept
{
    size_t idx = 0;
    auto f = [&] () noexcept { return get (idx++); };
    auto e = [&] () noexcept { return (int) std::lround (f()); };

    o.keyboardMode = (KeyboardMode) e();
    o.singleLayer = (Layer) e();
    o.splitPoint = e();
    o.lowerDetune = f();
    o.perfDetune = f();
    o.transpose = e();
    o.fineCents = f();
    o.tempoParam = f();
    o.masterDb = f();
    o.ribbon = f();
    o.modLever = f();
    o.expression = f();
    o.midiRouting = (MidiRouting) e();
    o.dualPedal = f() > 0.5f;
    o.hardwareCCMap = f() > 0.5f;
    jassert (idx == values.size());
}
} // namespace nsynth

