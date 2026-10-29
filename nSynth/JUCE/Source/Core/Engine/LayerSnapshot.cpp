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
    o.altA = e();
    o.altB = e();
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
    o.vcfType = (FilterType) e();
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
    o.stereoWidth = f();
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

    for (auto& macro : o.macros)
        macro = f();
    for (size_t m = 0; m < 2; ++m)
    {
        o.msegMode[m] = (MsegMode) e();
        o.msegSync[m] = b();
        o.msegTime[m] = f();
        o.msegDivision[m] = (MsegDivision) e();
    }
    o.f2Routing = (Filter2Routing) e();
    o.f2Type = (Filter2Type) e();
    o.f2Cutoff = f();
    o.f2Res = f();
    o.f2Env = f();
    o.f2EnvSource = (EnvSource) e();
    o.f2Keytrack = f();
    o.f2Drive = f();
    o.f2Mix = f();

    o.delayOn = b();
    o.delayDuck = f();
    o.reverbOn = b();
    o.reverbDuck = f();

    o.distOn = b();
    o.distType = (DistortionType) e();
    o.distDrive = f();
    o.distTone = f();
    o.distMix = f();

    o.eqOn = b();
    o.eqLowFreq = f();
    o.eqLowGain = f();
    o.eqMidFreq = f();
    o.eqMidGain = f();
    o.eqMidQ = f();
    o.eqHighFreq = f();
    o.eqHighGain = f();

    o.compOn = b();
    o.compThreshold = f();
    o.compRatio = f();
    o.compAttackMs = f();
    o.compReleaseMs = f();
    o.compMakeup = f();
    o.compKey = (CompressorKey) e();
    o.compMix = f();

    o.phaserOn = b();
    o.phaserRate = f();
    o.phaserDepth = f();
    o.phaserFeedback = f();
    o.phaserMix = f();

    o.flangerOn = b();
    o.flangerRate = f();
    o.flangerDepth = f();
    o.flangerFeedback = f();
    o.flangerMix = f();
    jassert (idx == values.size());
}

float* LayerSnapshot::floatField (int specIndex) noexcept
{
    // Byte offsets of the float fields, in getLayerParamSpecs() order (-1: not a float field).
    // Built once by matching the parameter suffixes; ParamTests checks every float parameter.
    static const auto offsets = []
    {
        std::array<int, kLayerParamCount> table;
        table.fill (-1);
        using S = LayerSnapshot;
        const std::pair<const char*, size_t> fields[] = {
            { pid::dds2Tune, offsetof (S, dds2Tune) }, { pid::mix, offsetof (S, mix) }, { pid::pan, offsetof (S, pan) },
            { pid::modLfo1, offsetof (S, modLfo1) }, { pid::modEnv1, offsetof (S, modEnv1) },
            { pid::pwDetune, offsetof (S, pwDetune) }, { pid::pwmWave, offsetof (S, pwmWave) },
            { pid::crossMod, offsetof (S, crossMod) }, { pid::drift, offsetof (S, drift) },
            { pid::hpf, offsetof (S, hpfHz) }, { pid::lpf, offsetof (S, lpfHz) }, { pid::res, offsetof (S, res) },
            { pid::vcfEnv, offsetof (S, vcfEnv) }, { pid::vcfLfo1, offsetof (S, vcfLfo1) }, { pid::vcfDds2, offsetof (S, vcfDds2) },
            { pid::vcaEnv, offsetof (S, vcaEnv) }, { pid::vcaLfo1, offsetof (S, vcaLfo1) }, { pid::vcaDds2, offsetof (S, vcaDds2) },
            { pid::env1AttackHold, offsetof (S, env1AttackHold) }, { pid::env1Attack, offsetof (S, env1Attack) },
            { pid::env1DecayHold, offsetof (S, env1DecayHold) }, { pid::env1Decay, offsetof (S, env1Decay) },
            { pid::env1Sustain, offsetof (S, env1Sustain) }, { pid::env1Release, offsetof (S, env1Release) },
            { pid::env2Attack, offsetof (S, env2Attack) }, { pid::env2DecayHold, offsetof (S, env2DecayHold) },
            { pid::env2Decay, offsetof (S, env2Decay) }, { pid::env2Sustain, offsetof (S, env2Sustain) },
            { pid::env2Release, offsetof (S, env2Release) },
            { pid::lfo1Rate, offsetof (S, lfo1Rate) }, { pid::lfo1Delay, offsetof (S, lfo1Delay) },
            { pid::lfo1LrPhase, offsetof (S, lfo1LrPhase) },
            { pid::lfo2Rate, offsetof (S, lfo2Rate) }, { pid::lfo2Delay, offsetof (S, lfo2Delay) },
            { pid::lfo2RateMod, offsetof (S, lfo2RateMod) }, { pid::lfo2Dds, offsetof (S, lfo2Dds) },
            { pid::lfo2Vcf, offsetof (S, lfo2Vcf) }, { pid::lfo2Vca, offsetof (S, lfo2Vca) },
            { pid::bendDds, offsetof (S, bendDds) }, { pid::bendVcf, offsetof (S, bendVcf) },
            { pid::portamento, offsetof (S, portamento) },
            { pid::binauralDetune, offsetof (S, binauralDetune) }, { pid::stereoWidth, offsetof (S, stereoWidth) },
            { pid::velCurve, offsetof (S, velCurve) }, { pid::velSens, offsetof (S, velSens) },
            { pid::chorusRate, offsetof (S, chorusRate) }, { pid::chorusDepth, offsetof (S, chorusDepth) },
            { pid::chorusDelay, offsetof (S, chorusDelayMs) }, { pid::chorusFeedback, offsetof (S, chorusFeedback) },
            { pid::chorusWidth, offsetof (S, chorusWidth) }, { pid::chorusMix, offsetof (S, chorusMix) },
            { pid::delayMs, offsetof (S, delayMs) }, { pid::delayFeedback, offsetof (S, delayFeedback) },
            { pid::delayMix, offsetof (S, delayMix) }, { pid::delayLowCut, offsetof (S, delayLowCutHz) },
            { pid::delayHighCut, offsetof (S, delayHighCutHz) },
            { pid::reverbMix, offsetof (S, reverbMix) }, { pid::reverbSize, offsetof (S, reverbSize) },
            { pid::reverbDecay, offsetof (S, reverbDecay) }, { pid::reverbPreDelay, offsetof (S, reverbPreDelayMs) },
            { pid::reverbDamping, offsetof (S, reverbDampingHz) }, { pid::reverbLowCut, offsetof (S, reverbLowCutHz) },
            { pid::reverbWidth, offsetof (S, reverbWidth) }, { pid::reverbModulation, offsetof (S, reverbModulation) },
            { pid::macro1, offsetof (S, macros) + 0 * sizeof (float) }, { pid::macro2, offsetof (S, macros) + 1 * sizeof (float) },
            { pid::macro3, offsetof (S, macros) + 2 * sizeof (float) }, { pid::macro4, offsetof (S, macros) + 3 * sizeof (float) },
            { pid::macro5, offsetof (S, macros) + 4 * sizeof (float) }, { pid::macro6, offsetof (S, macros) + 5 * sizeof (float) },
            { pid::macro7, offsetof (S, macros) + 6 * sizeof (float) }, { pid::macro8, offsetof (S, macros) + 7 * sizeof (float) },
            { pid::mseg1Time, offsetof (S, msegTime) }, { pid::mseg2Time, offsetof (S, msegTime) + sizeof (float) },
            { pid::filter2Cutoff, offsetof (S, f2Cutoff) }, { pid::filter2Res, offsetof (S, f2Res) },
            { pid::filter2Env, offsetof (S, f2Env) }, { pid::filter2Keytrack, offsetof (S, f2Keytrack) },
            { pid::filter2Drive, offsetof (S, f2Drive) }, { pid::filter2Mix, offsetof (S, f2Mix) },
            { pid::delayDuck, offsetof (S, delayDuck) }, { pid::reverbDuck, offsetof (S, reverbDuck) },
            { pid::distDrive, offsetof (S, distDrive) }, { pid::distTone, offsetof (S, distTone) }, { pid::distMix, offsetof (S, distMix) },
            { pid::eqLowFreq, offsetof (S, eqLowFreq) }, { pid::eqLowGain, offsetof (S, eqLowGain) },
            { pid::eqMidFreq, offsetof (S, eqMidFreq) }, { pid::eqMidGain, offsetof (S, eqMidGain) },
            { pid::eqMidQ, offsetof (S, eqMidQ) }, { pid::eqHighFreq, offsetof (S, eqHighFreq) },
            { pid::eqHighGain, offsetof (S, eqHighGain) },
            { pid::compThreshold, offsetof (S, compThreshold) }, { pid::compRatio, offsetof (S, compRatio) },
            { pid::compAttack, offsetof (S, compAttackMs) }, { pid::compRelease, offsetof (S, compReleaseMs) },
            { pid::compMakeup, offsetof (S, compMakeup) }, { pid::compMix, offsetof (S, compMix) },
            { pid::phaserRate, offsetof (S, phaserRate) }, { pid::phaserDepth, offsetof (S, phaserDepth) },
            { pid::phaserFeedback, offsetof (S, phaserFeedback) }, { pid::phaserMix, offsetof (S, phaserMix) },
            { pid::flangerRate, offsetof (S, flangerRate) }, { pid::flangerDepth, offsetof (S, flangerDepth) },
            { pid::flangerFeedback, offsetof (S, flangerFeedback) }, { pid::flangerMix, offsetof (S, flangerMix) },
        };
        for (const auto& [suffix, offset] : fields)
        {
            const int index = findLayerSpecIndex (suffix);
            jassert (index >= 0 && getLayerParamSpecs()[(size_t) index].kind == Kind::floatParam);
            if (index >= 0)
                table[(size_t) index] = (int) offset;
        }
        return table;
    }();

    if (specIndex < 0 || specIndex >= (int) kLayerParamCount || offsets[(size_t) specIndex] < 0)
        return nullptr;
    return reinterpret_cast<float*> (reinterpret_cast<char*> (this) + offsets[(size_t) specIndex]);
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

