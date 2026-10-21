#include "Voice.h"

namespace nsynth
{
namespace
{
float directParameterValue (const LayerSnapshot& p, DirectModTarget target) noexcept
{
    switch (target)
    {
        case DirectModTarget::dds2Tune: return p.dds2Tune;
        case DirectModTarget::lpfCutoff: return p.lpfHz;
        case DirectModTarget::vcfEnvAmount: return p.vcfEnv;
        case DirectModTarget::vcfLfo1Amount: return p.vcfLfo1;
        case DirectModTarget::vcfDds2Amount: return p.vcfDds2;
        case DirectModTarget::vcaEnvLevel: return p.vcaEnv;
        case DirectModTarget::vcaLfo1Amount: return p.vcaLfo1;
        case DirectModTarget::vcaDds2Amount: return p.vcaDds2;
        case DirectModTarget::env1Attack: return p.env1Attack;
        case DirectModTarget::env1Sustain: return p.env1Sustain;
        case DirectModTarget::env1Release: return p.env1Release;
        case DirectModTarget::env2Attack: return p.env2Attack;
        case DirectModTarget::env2Decay: return p.env2Decay;
        case DirectModTarget::env2Sustain: return p.env2Sustain;
        case DirectModTarget::env2Release: return p.env2Release;
        case DirectModTarget::lfo1Delay: return p.lfo1Delay;
        case DirectModTarget::lfo1LrPhase: return p.lfo1LrPhase;
        case DirectModTarget::lfo2Rate: return p.lfo2Rate;
        case DirectModTarget::lfo2Delay: return p.lfo2Delay;
        case DirectModTarget::ddsModLfo1Amount: return p.modLfo1;
        case DirectModTarget::pwDetune: return p.pwDetune;
        case DirectModTarget::portamento: return p.portamento;
        case DirectModTarget::delaySend: return p.delaySend;
        case DirectModTarget::delayFeedback: return p.delayFeedback;
        case DirectModTarget::count: break;
    }
    return 0.0f;
}

void setDirectParameterValue (LayerSnapshot& p, DirectModTarget target, float value) noexcept
{
    switch (target)
    {
        case DirectModTarget::dds2Tune: p.dds2Tune = value; break;
        case DirectModTarget::lpfCutoff: p.lpfHz = value; break;
        case DirectModTarget::vcfEnvAmount: p.vcfEnv = value; break;
        case DirectModTarget::vcfLfo1Amount: p.vcfLfo1 = value; break;
        case DirectModTarget::vcfDds2Amount: p.vcfDds2 = value; break;
        case DirectModTarget::vcaEnvLevel: p.vcaEnv = value; break;
        case DirectModTarget::vcaLfo1Amount: p.vcaLfo1 = value; break;
        case DirectModTarget::vcaDds2Amount: p.vcaDds2 = value; break;
        case DirectModTarget::env1Attack: p.env1Attack = value; break;
        case DirectModTarget::env1Sustain: p.env1Sustain = value; break;
        case DirectModTarget::env1Release: p.env1Release = value; break;
        case DirectModTarget::env2Attack: p.env2Attack = value; break;
        case DirectModTarget::env2Decay: p.env2Decay = value; break;
        case DirectModTarget::env2Sustain: p.env2Sustain = value; break;
        case DirectModTarget::env2Release: p.env2Release = value; break;
        case DirectModTarget::lfo1Delay: p.lfo1Delay = value; break;
        case DirectModTarget::lfo1LrPhase: p.lfo1LrPhase = value; break;
        case DirectModTarget::lfo2Rate: p.lfo2Rate = value; break;
        case DirectModTarget::lfo2Delay: p.lfo2Delay = value; break;
        case DirectModTarget::ddsModLfo1Amount: p.modLfo1 = value; break;
        case DirectModTarget::pwDetune: p.pwDetune = value; break;
        case DirectModTarget::portamento: p.portamento = value; break;
        case DirectModTarget::delaySend: p.delaySend = value; break;
        case DirectModTarget::delayFeedback: p.delayFeedback = value; break;
        case DirectModTarget::count: break;
    }
}

const std::array<const char*, ModMatrixData::directTargetCount> directParameterIds {
    pid::dds2Tune, pid::lpf, pid::vcfEnv, pid::vcfLfo1, pid::vcfDds2,
    pid::vcaEnv, pid::vcaLfo1, pid::vcaDds2, pid::env1Attack, pid::env1Sustain,
    pid::env1Release, pid::env2Attack, pid::env2Decay, pid::env2Sustain, pid::env2Release,
    pid::lfo1Delay, pid::lfo1LrPhase, pid::lfo2Rate, pid::lfo2Delay, pid::modLfo1,
    pid::pwDetune, pid::portamento, pid::delaySend, pid::delayFeedback
};
}

float Voice::rangeOffset (Dds1Range r) noexcept
{
    static constexpr float offsets[] = { -36.0f, -24.0f, -12.0f, 0.0f, 12.0f, 24.0f }; // 64'..2'
    return offsets[toInt (r)];
}

float Voice::rangeOffset (Dds2Range r) noexcept
{
    static constexpr float offsets[] = { 0.0f, -24.0f, -12.0f, 0.0f, 12.0f, 24.0f };   // LFO, 32'..2'
    return offsets[toInt (r)];
}

float Voice::fastExp2 (float x) noexcept
{
    x = juce::jlimit (-20.0f, 20.0f, x);
    const float fl = std::floor (x);
    const float f = x - fl;
    const int i = (int) fl;
    const float poly = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * 0.00961813f)));
    return std::ldexp (poly, i);
}

void Voice::prepare (double internalSampleRate, const WaveBank* bank, uint32_t seed)
{
    waveBank = bank;
    rng.setSeed ((juce::int64) seed);
    dds1.setWaveBank (bank);
    dds1.randomisePhases (rng);
    lfo1.setPhase (rng.nextDouble());
    for (int i = 0; i < 3; ++i)
        driftTarget[i] = rng.nextFloat() * 2.0f - 1.0f;
    setSampleRate (internalSampleRate);
}

void Voice::setSampleRate (double sr)
{
    sampleRate = sr;
    dds1.setSampleRate (sr);
    dds2.setSampleRate (sr);
    filter.setSampleRate (sr);
    filter.reset();
    for (auto* e : { &env1, &env2, &fixedEnv })
        e->setSampleRate (sr);
    lfo1.setSampleRate (sr);
    lfo2.setSampleRate (sr);
    lfo1Ramp.setSampleRate (sr);
    lfo2Ramp.setSampleRate (sr);
}

void Voice::applyEnvelopeSettings (const LayerSnapshot& p) noexcept
{
    // ENV1 keytrack shortens decay and release up the keyboard.
    const float kt = triStateAmount (p.env1Keytrack);
    const float keyScale = std::exp2 (-kt * ((float) note - 60.0f) / 12.0f);
    const float ts = envTimeScale;

    Envelope::Settings e1;
    e1.attackHold = p.env1AttackHold;
    e1.attack = p.env1Attack * ts;
    e1.decayHold = p.env1DecayHold;
    e1.decay = p.env1Decay * keyScale * ts;
    e1.sustain = p.env1Sustain;
    e1.release = p.env1Release * keyScale * ts;
    e1.loop = p.env1Mode == Env1Mode::loop;
    env1.setSettings (e1);

    Envelope::Settings e2;
    e2.attack = p.env2Attack * ts;
    e2.decayHold = p.env2DecayHold;
    e2.decay = p.env2Decay * ts;
    e2.sustain = p.env2Sustain;
    e2.release = p.env2Release * ts;
    env2.setSettings (e2);

    // Fixed VCA envelopes: 1 ms edges avoid clicks; Fixed 2 uses a 0.5 s release.
    Envelope::Settings f;
    f.attack = 0.001f;
    f.decay = 0.001f;
    f.sustain = 1.0f;
    f.release = p.vcaEnvMode == VcaEnvMode::fixed2 ? 0.5f : 0.001f;
    fixedEnv.setSettings (f);

    ampEnvUsesEnv2 = p.vcaEnvMode == VcaEnvMode::env2;
}

void Voice::triggerEnvelopes() noexcept
{
    env1.noteOn();
    env2.noteOn();
    fixedEnv.noteOn();
}

void Voice::start (const StartInfo& info, const LayerSnapshot& p)
{
    note = info.note;
    midiChannel = juce::jlimit (1, 16, info.midiChannel);
    velocity = info.velocity;
    aftertouch = 0.0f;
    midiPitchBend = channelTuning = channelPressure = midiTimbre = 0.0f;
    midiBendSensitivity = 2.0f;
    isMpeMember = false;
    pitchOffset = info.pitchOffset;
    side = info.side;
    age = info.age;
    lastModSources.fill (0.0f);
    lastModSources[(size_t) ModSource::velocity] = juce::jlimit (0.0f, 1.0f, velocity);
    lastModSources[(size_t) ModSource::noteNumber] = juce::jlimit (0.0f, 1.0f, (float) note / 127.0f);
    targetPitch = (double) note + (double) pitchOffset;

    if (p.portamento <= 0.0f || ! hasPlayed)
        currentPitch = targetPitch;
    hasPlayed = true;

    switch (side)
    {
        case Side::left:  panL = 1.0f; panR = 0.0f; break;
        case Side::right: panL = 0.0f; panR = 1.0f; break;
        case Side::mono:
        {
            const float angle = (juce::jlimit (-1.0f, 1.0f, info.pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            panL = std::cos (angle);
            panR = std::sin (angle);
            break;
        }
    }

    envTimeScale = 1.0f + p.drift * 0.1f * (rng.nextFloat() * 2.0f - 1.0f);

    if (info.retrigger || ! active)
    {
        dds1.noteOn (p.superMode);
        dds2.noteOn();
        if (info.lfo1Phase >= 0.0)
            lfo1.setPhase (info.lfo1Phase);
        lfo1.noteOn (false, p.lfo1Mode == Lfo1Mode::onceOrDds1 && toInt (p.lfo1Wave) < toInt (Lfo1Wave::hf), 0.0, rng);
        lfo2.setPhase (info.lfo2Phase);
        lfo1Ramp.noteOn();
        lfo2Ramp.noteOn();
        applyEnvelopeSettings (p);
        triggerEnvelopes();
    }

    active = true;
    gate = true;
}

void Voice::changeNote (int newNote, float newVelocity, bool retrigger, uint64_t newAge, const LayerSnapshot& p,
                        int newMidiChannel)
{
    note = newNote;
    midiChannel = juce::jlimit (1, 16, newMidiChannel);
    age = newAge;
    targetPitch = (double) note + (double) pitchOffset;
    if (p.portamento <= 0.0f)
        currentPitch = targetPitch;

    if (retrigger)
    {
        velocity = newVelocity;
        dds1.noteOn (p.superMode);
        dds2.noteOn();
        if (p.lfo1Mode != Lfo1Mode::freeOrNorm && toInt (p.lfo1Wave) < toInt (Lfo1Wave::hf))
        {
            lfo1.setPhase (side == Side::right ? p.lfo1LrPhase : 0.0);
            lfo1.noteOn (false, p.lfo1Mode == Lfo1Mode::onceOrDds1, 0.0, rng); // re-arm ONCE
        }
        lfo1Ramp.noteOn();
        lfo2Ramp.noteOn();
        applyEnvelopeSettings (p);
        triggerEnvelopes();
    }
    active = true;
    gate = true;
}

void Voice::release() noexcept
{
    gate = false;
    env1.noteOff();
    env2.noteOff();
    fixedEnv.noteOff();
}

void Voice::curtail() noexcept
{
    // POLY 2 curtails overlapping releases: a fast 5 ms release.
    Envelope::Settings fast;
    fast.release = 0.005f;
    fast.sustain = 1.0f;
    if (ampEnvUsesEnv2)
        env2.setSettings (fast);
    else
        fixedEnv.setSettings (fast);
    release();
}

void Voice::kill() noexcept
{
    active = false;
    gate = false;
    env1.reset();
    env2.reset();
    fixedEnv.reset();
    lastModSources.fill (0.0f);
}

float Voice::lfo1RateHz (const LayerSnapshot& p, const VoiceContext& ctx) const noexcept
{
    switch (p.lfo1Wave)
    {
        case Lfo1Wave::hf:      return (float) rates::highFrequencyHz (p.lfo1Rate);
        case Lfo1Wave::hfTrack: return (float) (noteToHz (currentPitch + (double) ctx.layerPitchOffset) * rates::hfTrackRatio (p.lfo1Rate));
        case Lfo1Wave::triangle:
        case Lfo1Wave::revSaw:
        case Lfo1Wave::sampleHold:
        case Lfo1Wave::square:  break;
    }

    if (p.clockSync)
    {
        const auto& beats = rates::lfo1SyncBeats();
        const double b = beats[(size_t) rates::divisionIndex (p.lfo1Rate, (int) beats.size())];
        return (float) (ctx.bpm / 60.0 / b);
    }
    return (float) rates::lowFrequencyHz (p.lfo1Rate);
}

float Voice::driftWalk (float& value, float& target, int& counter) noexcept
{
    // Slow random walk (~0.3 s segments) used by DRIFT.
    if (--counter <= 0)
    {
        counter = (int) (sampleRate * (0.2 + 0.3 * rng.nextDouble()));
        target = rng.nextFloat() * 2.0f - 1.0f;
    }
    value += (target - value) * (float) (1.0 / (0.25 * sampleRate));
    return value;
}

void Voice::render (float* outL, float* outR, int startSample, int numSamples, const LayerSnapshot& p,
                    const VoiceContext& ctx, const ModMatrixData& matrix) noexcept
{
    if (! active)
        return;

    if (gate)
        applyEnvelopeSettings (p); // live edits of times/sustain while the note is held

    const float smooth = (float) (1.0 - std::exp (-1.0 / (0.004 * sampleRate)));

    // ---- block-rate values -------------------------------------------------
    const bool lfo1Hf = toInt (p.lfo1Wave) >= toInt (Lfo1Wave::hf);
    LfoShape lfo1Shape = LfoShape::sine;
    switch (p.lfo1Wave)
    {
        case Lfo1Wave::triangle:   lfo1Shape = LfoShape::triangle; break;
        case Lfo1Wave::revSaw:     lfo1Shape = LfoShape::revSaw; break;
        case Lfo1Wave::sampleHold: lfo1Shape = p.lfo1Rate >= 0.99f && ! p.clockSync ? LfoShape::noise : LfoShape::sampleHold; break;
        case Lfo1Wave::square:     lfo1Shape = LfoShape::square; break;
        case Lfo1Wave::hf:
        case Lfo1Wave::hfTrack:    lfo1Shape = LfoShape::sine; break;
    }
    static constexpr LfoShape lfo2Shapes[] = { LfoShape::sine, LfoShape::revSaw, LfoShape::sampleHold,
                                              LfoShape::square, LfoShape::saw, LfoShape::noise };
    const LfoShape lfo2Shape = lfo2Shapes[toInt (p.lfo2Wave)];

    // LFO2 trigger logic
    const float at = juce::jmax (aftertouch, channelPressure, ctx.channelPressure);
    float lfo2Depth = ctx.modLever, lfo2Push = ctx.modLever, bendTotal = midiPitchBend;
    switch (p.lfo2Trigger)
    {
        case Lfo2Trigger::trig:       break;
        case Lfo2Trigger::atPlusTrig: lfo2Depth = lfo2Push = juce::jmax (ctx.modLever, at); break;
        case Lfo2Trigger::on:         lfo2Depth = 1.0f; lfo2Push = ctx.modLever; bendTotal = juce::jlimit (-1.0f, 1.0f, midiPitchBend + at); break;
    }
    const bool toDds1 = p.modDest != OscTarget::dds2, toDds2 = p.modDest != OscTarget::dds1;
    const bool perfToDds1 = p.perfDest != OscTarget::dds2, perfToDds2 = p.perfDest != OscTarget::dds1;
    const float env1PitchSt = p.modEnv1 * p.modEnv1 * 48.0f;
    const float lfo2PitchSt = p.lfo2Dds * p.lfo2Dds * 12.0f;
    const auto& ribbonRoutes = matrix.fixed[(size_t) ModSource::ribbon];
    const auto& ribbonDirectRoutes = matrix.direct[(size_t) ModSource::ribbon];
    const bool ribbonMapped = std::any_of (ribbonRoutes.begin(), ribbonRoutes.end(), [] (float x) { return x != 0.0f; })
                           || std::any_of (ribbonDirectRoutes.begin(), ribbonDirectRoutes.end(), [] (float x) { return x != 0.0f; });
    const float ribbon = gate && ! ribbonMapped ? ctx.ribbonSemitones : 0.0f; // assigned ribbon becomes a matrix-only source
    const float perfPitch = bendTotal * p.bendDds * (midiBendSensitivity / 2.0f);

    const float keyTrack = triStateAmount (p.vcfKeytrack);
    const float dyn = triStateAmount (p.vcaDynamics);
    const float velGain = 1.0f - dyn + dyn * velocity;
    const float velOct = dyn * (velocity - 1.0f) * 3.0f;
    const float range1 = rangeOffset (p.dds1Range);
    const bool dds2IsLfo = p.dds2Range == Dds2Range::lfo;
    const float range2 = rangeOffset (p.dds2Range);
    const bool ringMode = ! dds2IsLfo && p.dds2Mode == Dds2Mode::ringOrSubSquare;
    const bool syncMode = ! dds2IsLfo && p.dds2Mode == Dds2Mode::syncOrSubSine;
    const bool subOn = dds2IsLfo && p.dds2Mode != Dds2Mode::normOrSubOff;
    const bool subSine = p.dds2Mode == Dds2Mode::syncOrSubSine;
    static const auto hpfRange = makeRange (*findLayerSpec (pid::hpf));
    static const auto resRange = makeRange (*findLayerSpec (pid::res));
    static const auto env1DecayRange = makeRange (*findLayerSpec (pid::env1Decay));
    static const auto directRanges = [] 
    {
        std::array<juce::NormalisableRange<float>, ModMatrixData::directTargetCount> result;
        for (int i = 0; i < ModMatrixData::directTargetCount - 2; ++i)
            if (const auto* spec = findLayerSpec (directParameterIds[(size_t) i]))
                result[(size_t) i] = makeRange (*spec);
        for (int i = ModMatrixData::directTargetCount - 2; i < ModMatrixData::directTargetCount; ++i)
            if (const auto* spec = findLayerSpec (directParameterIds[(size_t) i]))
                result[(size_t) i] = makeRange (*spec);
        return result;
    }();
    std::array<bool, ModMatrixData::directTargetCount> hasDirectTarget {};
    bool hasAnyDirectTarget = false;
    bool hasEnvelopeDirectTarget = false;
    for (int target = 0; target < ModMatrixData::directTargetCount; ++target)
        for (int source = 0; source < ModMatrixData::sourceCount; ++source)
            if (matrix.direct[(size_t) source][(size_t) target] != 0.0f)
            {
                hasDirectTarget[(size_t) target] = true;
                hasAnyDirectTarget = true;
                hasEnvelopeDirectTarget |= target >= (int) DirectModTarget::env1Attack
                                        && target <= (int) DirectModTarget::env2Release;
            }
    std::array<bool, ModMatrixData::targetCount> hasFixedTarget {};
    for (int target = 0; target < ModMatrixData::targetCount; ++target)
        for (int source = 0; source < ModMatrixData::sourceCount; ++source)
            hasFixedTarget[(size_t) target] |= matrix.fixed[(size_t) source][(size_t) target] != 0.0f;
    const bool hasEnv1DecayRoute = hasFixedTarget[(size_t) ModTarget::env1Decay];
    const auto modulatedFader = [&] (ModTarget target, float base, const std::array<float, ModMatrixData::sourceCount>& sources)
    {
        if (! hasFixedTarget[(size_t) target])
            return juce::jlimit (0.0f, 1.0f, base);
        for (int source = 0; source < ModMatrixData::sourceCount; ++source)
            base += sources[(size_t) source] * matrix.fixed[(size_t) source][(size_t) target];
        return juce::jlimit (0.0f, 1.0f, base);
    };

    const float baseHpfNorm = hasFixedTarget[(size_t) ModTarget::hpfCutoff] ? hpfRange.convertTo0to1 (p.hpfHz) : 0.0f;
    const float baseResNorm = hasFixedTarget[(size_t) ModTarget::lpfResonance] ? resRange.convertTo0to1 (p.res) : 0.0f;
    const float lfo2RateScale = (float) std::exp2 (3.0 * p.lfo2RateMod * lfo2Push);
    const double baseGlideRate = p.portamento > 0.0f
                               ? 12.0 / ((double) portamentoSecondsPerOctave (p.portamento) * sampleRate) : 1.0e9;
    const double baseLfo2Hz = rates::lowFrequencyHz (p.lfo2Rate) * (double) lfo2RateScale;

    Envelope& ampEnv = ampEnvUsesEnv2 ? env2 : fixedEnv;

    Dds1::Settings d1s;
    d1s.wave = p.dds1Wave;
    d1s.superMode = p.superMode;
    d1s.altA = p.altA;
    d1s.altB = p.altB;
    d1s.mirrorDetune = side == Side::right;

    float l2 = 0.0f;
    for (int i = startSample; i < startSample + numSamples; ++i)
    {
        // ---- modulators ----------------------------------------------------
        if (hasEnv1DecayRoute && (i & 7) == 0)
        {
            LayerSnapshot envelopeSettings = p;
            float decay = env1DecayRange.convertTo0to1 (p.env1Decay);
            for (int source = 0; source < ModMatrixData::sourceCount; ++source)
                decay += lastModSources[(size_t) source] * matrix.fixed[(size_t) source][(size_t) ModTarget::env1Decay];
            envelopeSettings.env1Decay = env1DecayRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, decay));
            applyEnvelopeSettings (envelopeSettings);
        }
        const float e1raw = env1.process();
        const float e1 = p.env1Mode == Env1Mode::inverted ? -e1raw : e1raw;
        const float e2 = env2.process();
        if (! ampEnvUsesEnv2)
            fixedEnv.process();
        const float ampLevel = ampEnv.getValue();
        std::array<float, ModMatrixData::sourceCount> modSources {
            juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * dds2.getLastOutput()),
            juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * l2),
            juce::jlimit (0.0f, 1.0f, e1),
            juce::jlimit (0.0f, 1.0f, velocity),
            juce::jlimit (0.0f, 1.0f, at),
            juce::jlimit (0.0f, 1.0f, ctx.expression),
            juce::jlimit (0.0f, 1.0f, 0.5f + ctx.ribbonSemitones / 24.0f),
            juce::jlimit (0.0f, 1.0f, (float) note / 127.0f),
            isMpeMember ? juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * midiPitchBend) : 0.5f,
            isMpeMember ? juce::jlimit (0.0f, 1.0f, midiTimbre) : 0.0f
        };
        lastModSources = modSources;

        LayerSnapshot effective = p;
        if (hasAnyDirectTarget)
        {
            for (int target = 0; target < ModMatrixData::directTargetCount; ++target)
            {
                if (! hasDirectTarget[(size_t) target])
                    continue;
                const auto directTarget = (DirectModTarget) target;
                const auto& range = directRanges[(size_t) target];
                float normalized = range.convertTo0to1 (directParameterValue (p, directTarget));
                for (int source = 0; source < ModMatrixData::sourceCount; ++source)
                    normalized += modSources[(size_t) source] * matrix.direct[(size_t) source][(size_t) target];
                setDirectParameterValue (effective, directTarget, range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalized)));
            }
            if (hasEnvelopeDirectTarget && (i & 7) == 0)
                applyEnvelopeSettings (effective);
        }

        const double glideRate = hasDirectTarget[(size_t) DirectModTarget::portamento]
                               ? (effective.portamento > 0.0f
                                      ? 12.0 / ((double) portamentoSecondsPerOctave (effective.portamento) * sampleRate)
                                      : 1.0e9)
                               : baseGlideRate;
        const double lfo2Hz = hasDirectTarget[(size_t) DirectModTarget::lfo2Rate]
                            ? rates::lowFrequencyHz (effective.lfo2Rate) * (double) lfo2RateScale
                            : baseLfo2Hz;
        l2 = lfo2.process (lfo2Hz, lfo2Shape, rng) * lfo2Ramp.process (effective.lfo2Delay) * lfo2Depth;
        const float lfo1Fader = modulatedFader (ModTarget::lfo1Rate, effective.lfo1Rate, modSources);
        LayerSnapshot rateSnapshot = effective;
        rateSnapshot.lfo1Rate = lfo1Fader;
        const float lfo1Hz = lfo1RateHz (rateSnapshot, ctx);
        const float l1Raw = lfo1.process (lfo1Hz, lfo1Shape, rng);
        const float l1 = lfo1Hf ? l1Raw : l1Raw * lfo1Ramp.process (effective.lfo1Delay);
        const float l1Mod = lfo1Hf && p.lfo1Mode != Lfo1Mode::freeOrNorm ? 0.0f : l1; // routed HF LFO is audio, not CV

        // ---- pitch -----------------------------------------------------------
        if (currentPitch < targetPitch)      currentPitch = juce::jmin (targetPitch, currentPitch + glideRate);
        else if (currentPitch > targetPitch) currentPitch = juce::jmax (targetPitch, currentPitch - glideRate);

        const float driftAmt = p.drift;
        const float drift1 = driftAmt > 0.0f ? driftWalk (driftValue[0], driftTarget[0], driftCounter[0]) * driftAmt * 0.15f : 0.0f;
        const float drift2 = driftAmt > 0.0f ? driftWalk (driftValue[1], driftTarget[1], driftCounter[1]) * driftAmt * 0.15f : 0.0f;
        const float driftCut = driftAmt > 0.0f ? driftWalk (driftValue[2], driftTarget[2], driftCounter[2]) * driftAmt * 0.15f : 0.0f;

        const float lfo1PitchSt = effective.modLfo1 * effective.modLfo1 * 12.0f;
        const float oscMod = l1Mod * lfo1PitchSt + e1 * env1PitchSt;
        const float perfMod = perfPitch + l2 * lfo2PitchSt;
        const double base = currentPitch + (double) ctx.layerPitchOffset + (double) ribbon + (double) channelTuning;

        double pitch1 = base + range1 + drift1 + (toDds1 ? oscMod : 0.0f) + (perfToDds1 ? perfMod : 0.0f);
        double pitch2 = base + range2 + sTune.next (effective.dds2Tune, smooth) + drift2 + (toDds2 ? oscMod : 0.0f) + (perfToDds2 ? perfMod : 0.0f);

        // ---- DDS modulator ---------------------------------------------------
        const float morphFader = sMorph.next (modulatedFader (ModTarget::waveMod, effective.pwmWave, modSources), smooth);
        float morph = morphFader, pwm = morphFader;
        if (p.pwmSource == PwmSource::lfo1)
        {
            const float l1Uni = Lfo::isUnipolar (lfo1Shape) ? l1Mod : 0.5f * (l1Mod + 1.0f);
            morph = pwm = morphFader * l1Uni;
        }
        else if (p.pwmSource == PwmSource::env1)
        {
            morph = pwm = morphFader * juce::jmax (0.0f, e1);
        }

        const float detune = sDetune.next (effective.pwDetune, smooth);
        d1s.detune = detune;
        d1s.morph = juce::jlimit (0.0f, 1.0f, morph);
        const float pulseWidth = juce::jlimit (0.02f, 0.98f, 0.5f - 0.45f * (detune + pwm));

        const float crossOct = sCross.next (modulatedFader (ModTarget::crossMod, p.crossMod, modSources), smooth) * 4.0f;
        double f1 = noteToHz (pitch1);
        double f2 = dds2IsLfo ? rates::dds2LfoHz (effective.dds2Tune) * (double) fastExp2 ((float) ((pitch2 - base - range2 - effective.dds2Tune) / 12.0))
                              : noteToHz (pitch2);
        if (crossOct > 0.0f)
        {
            if (syncMode) f2 *= (double) fastExp2 (crossOct * lastDds1);          // reversed routing with SYNC
            else          f1 *= (double) fastExp2 (crossOct * dds2.getLastOutput()); // DDS2 -> DDS1 exponential FM
        }

        // ---- oscillators -----------------------------------------------------
        WrapEvent wrap;
        const float d1 = dds1.process (f1, d1s, rng, wrap);
        lastDds1 = d1;
        const float d1Aligned = delayedDds1; // DDS2 output is one sample late (BLEP), align DDS1 with it
        delayedDds1 = d1;

        const float d2 = dds2.process (f2, p.dds2Wave, pulseWidth, syncMode ? &wrap : nullptr, rng);
        const float sub = subOn ? dds2.processSub (subSine, dds1.centroidPhase(), wrap) : 0.0f;

        float ch1 = d1Aligned;
        float ch2 = dds2IsLfo ? sub : (ringMode ? d1Aligned * d2 : d2);
        if (lfo1Hf && p.lfo1Mode == Lfo1Mode::onceOrDds1) ch1 += l1Raw;
        if (lfo1Hf && p.lfo1Mode == Lfo1Mode::resetOrDds2) ch2 += l1Raw;

        const float m = juce::jlimit (0.0f, 1.0f, sMix.next (modulatedFader (ModTarget::oscillatorMix, p.mix, modSources), smooth));
        const float osc = ch1 * (1.0f - m) + ch2 * m;

        // ---- VCF -------------------------------------------------------------
        float envSrc = e1;
        if (p.vcfEnvSource == EnvSource::both) envSrc = e1 + e2;
        else if (p.vcfEnvSource == EnvSource::env2) envSrc = e2;

        const float octaves = effective.vcfEnv * 8.0f * envSrc
                            + effective.vcfLfo1 * 4.0f * l1Mod
                            + effective.vcfDds2 * 4.0f * d2
                            + p.lfo2Vcf * 4.0f * l2
                            + p.bendVcf * 10.0f * bendTotal
                            + keyTrack * ((float) currentPitch - 60.0f) / 12.0f
                            + velOct + driftCut;

        const float hpfHz = hasFixedTarget[(size_t) ModTarget::hpfCutoff]
                          ? hpfRange.convertFrom0to1 (modulatedFader (ModTarget::hpfCutoff, baseHpfNorm, modSources))
                          : p.hpfHz;
        const float res = hasFixedTarget[(size_t) ModTarget::lpfResonance]
                        ? resRange.convertFrom0to1 (modulatedFader (ModTarget::lpfResonance, baseResNorm, modSources))
                        : p.res;
        const float lpfBase = sLpf.next (effective.lpfHz, smooth);
        const float hpfBase = sHpf.next (hpfHz, smooth);
        filter.setParameters (hpfBase * fastExp2 (driftCut), lpfBase * fastExp2 (octaves), sRes.next (res, smooth), p.drive);
        const float filtered = filter.process (osc);

        // ---- VCA -------------------------------------------------------------
        // [ASSUMPTION] LFO/DDS2/LFO2 amounts act as multiplicative CV on the envelope level.
        float gain = sVcaEnv.next (effective.vcaEnv, smooth) * ampLevel * velGain;
        gain *= juce::jmax (0.0f, 1.0f + effective.vcaLfo1 * l1Mod);
        gain *= juce::jmax (0.0f, 1.0f + effective.vcaDds2 * d2);
        gain *= juce::jmax (0.0f, 1.0f + p.lfo2Vca * l2);

        const float out = filtered * gain * kOutputGain;
        outL[i] += out * panL;
        outR[i] += out * panR;

        if (! ampEnv.isActive())
        {
            active = false;
            break;
        }
    }
}
} // namespace nsynth
