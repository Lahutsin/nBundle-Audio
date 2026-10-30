#include "Voice.h"

namespace nsynth
{
namespace
{
// Parameter indices the render loop needs to know about, resolved once.
struct ParamIndices
{
    int lfo2Rate, lfo2RateMod, portamento;
    std::array<int, 2> msegTime;
    std::array<bool, kLayerParamCount> envelope {};

    ParamIndices()
        : lfo2Rate (findLayerSpecIndex (pid::lfo2Rate)),
          lfo2RateMod (findLayerSpecIndex (pid::lfo2RateMod)),
          portamento (findLayerSpecIndex (pid::portamento)),
          msegTime { findLayerSpecIndex (pid::mseg1Time), findLayerSpecIndex (pid::mseg2Time) }
    {
        for (auto* suffix : { pid::env1AttackHold, pid::env1Attack, pid::env1DecayHold, pid::env1Decay, pid::env1Sustain,
                              pid::env1Release, pid::env2Attack, pid::env2DecayHold, pid::env2Decay, pid::env2Sustain,
                              pid::env2Release })
            envelope[(size_t) findLayerSpecIndex (suffix)] = true;
    }
};

const ParamIndices& paramIndices()
{
    static const ParamIndices indices;
    return indices;
}

double msegSeconds (const LayerSnapshot& s, int m, double bpm) noexcept
{
    return s.msegSync[(size_t) m] ? rates::msegDivisionBeats (toInt (s.msegDivision[(size_t) m])) * 60.0 / juce::jmax (1.0, bpm)
                                  : (double) juce::jmax (0.001f, s.msegTime[(size_t) m]);
}
} // namespace

void Voice::initialiseStaticTables()
{
    paramIndices();
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

float Voice::openLadderCutoff (float hz) noexcept
{
    // A 4-pole ladder at 25 kHz still takes ~2.5 dB off at 10 kHz and ~5 dB at 15 kHz, which
    // made the "fully open" filter sound dull. Above kOpenStretchHz the cutoff is stretched
    // exponentially so the top of the LPF range reaches 0.45 x the internal rate and the open
    // filter is close to flat across the audible band; everything below is unchanged.
    if (hz <= kOpenStretchHz || openExponent <= 1.0f)
        return hz;
    if (hz != stretchInput)
    {
        stretchInput = hz;
        stretchOutput = juce::jmin (openTopHz, kOpenStretchHz * fastExp2 (openExponent * (float) fastmath::log2 ((double) (hz / kOpenStretchHz))));
    }
    return stretchOutput;
}

float Voice::fastExp2 (float x) noexcept
{
    return fastmath::exp2 (juce::jlimit (-20.0f, 20.0f, x));
}

void Voice::prepare (double internalSampleRate, const WaveBank* bank, uint32_t seed)
{
    waveBank = bank;
    // juce::Random is an LCG: consecutive seeds give linearly related first outputs, which made
    // the free-running start phases of the voices an arithmetic series (stacked voices partly
    // cancelled). A splitmix64 finaliser decorrelates the per-voice seeds.
    uint64_t z = (uint64_t) seed + 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    rng.setSeed ((juce::int64) (z ^ (z >> 31)));
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
    filter2.setSampleRate (sr);
    for (auto* e : { &env1, &env2, &fixedEnv })
        e->setSampleRate (sr);
    lfo1.setSampleRate (sr);
    lfo2.setSampleRate (sr);
    lfo1Ramp.setSampleRate (sr);
    lfo2Ramp.setSampleRate (sr);
    openTopHz = (float) (0.45 * sr);
    openExponent = openTopHz > kLpfMaxHz ? std::log (openTopHz / kOpenStretchHz) / std::log (kLpfMaxHz / kOpenStretchHz) : 1.0f;
    stretchInput = -1.0f;
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

void Voice::restartMsegs (const LayerSnapshot& p, const std::array<double, 2>& freePhase) noexcept
{
    // ENV and LOOP start from the beginning at every (re)trigger; FREE joins the layer clock.
    for (size_t m = 0; m < 2; ++m)
    {
        msegPhase[m] = p.msegMode[m] == MsegMode::free ? freePhase[m] - std::floor (freePhase[m]) : 0.0;
        msegHint[m] = 0;
    }
}

bool Voice::isAudible() const noexcept
{
    return active && getAmpEnvelope().getValue() > 1.0e-3f;
}

bool Voice::needsStealFade (const StartInfo& info) const noexcept
{
    if (! isAudible())
        return false;
    // Retriggering the same note with the same placement continues smoothly from the current level.
    const bool sameSound = info.note == note && juce::exactlyEqual (info.pitchOffset, pitchOffset)
                        && juce::exactlyEqual (info.detuneSemitones, unisonDetune) && juce::exactlyEqual (info.twinSign, twinSign)
                        && juce::exactlyEqual (info.gain, voiceGain) && info.side == side;
    return ! sameSound;
}

void Voice::start (const StartInfo& info, const LayerSnapshot& p)
{
    if (stealPending || needsStealFade (info))
    {
        // The new note owns the voice right away (note-offs, voice allocation); the old sound
        // fades out in render() and begin() runs once it is silent.
        if (! stealPending)
            stealGain = 1.0f;
        stealPending = true;
        pendingStart = info;
        note = info.note;
        midiChannel = juce::jlimit (1, 16, info.midiChannel);
        age = info.age;
        gate = true;
        return;
    }
    begin (info, p, false);
}

void Voice::begin (const StartInfo& info, const LayerSnapshot& p, bool fromSilence)
{
    // A retriggered voice that is still sounding keeps its oscillator phases: resetting them in
    // the middle of a cycle is an audible click. Silent voices restart from the defined phase.
    if (fromSilence)
    {
        env1.reset();
        env2.reset();
        fixedEnv.reset();
    }
    const bool keepPhases = ! fromSilence && isAudible();
    note = info.note;
    midiChannel = juce::jlimit (1, 16, info.midiChannel);
    velocity = info.velocity;
    aftertouch = 0.0f;
    midiPitchBend = channelTuning = channelPressure = midiTimbre = masterBend = 0.0f;
    midiBendSensitivity = masterBendSensitivity = 2.0f;
    isMpeMember = false;
    pitchOffset = info.pitchOffset;
    twinSign = info.twinSign;
    unisonDetune = info.detuneSemitones;
    voiceGain = info.gain;
    side = info.side;
    age = info.age;
    randomValue = rng.nextFloat();
    lastModSources.fill (0.0f);
    lastModSources[(size_t) ModSource::velocity] = juce::jlimit (0.0f, 1.0f, velocity);
    lastModSources[(size_t) ModSource::noteNumber] = juce::jlimit (0.0f, 1.0f, (float) note / 127.0f);
    lastModSources[(size_t) ModSource::random] = randomValue;
    targetPitch = (double) note + (double) pitchOffset;

    if (p.portamento <= 0.0f || ! hasPlayed)
        currentPitch = targetPitch;
    hasPlayed = true;

    // Binaural voices sit WIDTH apart with an equal-power law: fully decorrelated hard left/right
    // voices sound hollow and collapse in mono, a partial overlap keeps a solid centre.
    // A single (mono) voice uses a balance law, so a centred voice plays at full level on both sides.
    const float width = juce::jlimit (0.0f, 1.0f, info.width);
    const float position = side == Side::left ? -width : side == Side::right ? width : juce::jlimit (-1.0f, 1.0f, info.pan);
    const float angle = (position + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float law = side == Side::mono ? juce::MathConstants<float>::sqrt2 : 1.0f;
    panL = juce::jmin (1.0f, law * std::cos (angle));
    panR = juce::jmin (1.0f, law * std::sin (angle));
    if (position <= -1.0f) panR = 0.0f;
    if (position >= 1.0f)  panL = 0.0f;

    envTimeScale = 1.0f + p.drift * 0.1f * (rng.nextFloat() * 2.0f - 1.0f);

    if (info.retrigger || ! active)
    {
        if (! keepPhases)
        {
            dds1.noteOn (p.superMode);
            dds2.noteOn();
        }
        if (info.lfo1Phase >= 0.0)
            lfo1.setPhase (info.lfo1Phase);
        lfo1.noteOn (false, p.lfo1Mode == Lfo1Mode::onceOrDds1 && toInt (p.lfo1Wave) < toInt (Lfo1Wave::hf), 0.0, rng);
        lfo2.setPhase (info.lfo2Phase);
        lfo1Ramp.noteOn();
        lfo2Ramp.noteOn();
        restartMsegs (p, info.msegPhase);
        applyEnvelopeSettings (p);
        triggerEnvelopes();
    }

    active = true;
    gate = true;
}

void Voice::changeNote (int newNote, float newVelocity, bool retrigger, uint64_t newAge, const LayerSnapshot& p,
                        int newMidiChannel)
{
    if (stealPending)
    {
        // Still fading out the stolen sound: the pending start plays the latest note instead.
        pendingStart.note = newNote;
        pendingStart.midiChannel = newMidiChannel;
        pendingStart.age = newAge;
        if (retrigger)
            pendingStart.velocity = newVelocity;
        note = newNote;
        midiChannel = juce::jlimit (1, 16, newMidiChannel);
        age = newAge;
        gate = true;
        return;
    }
    note = newNote;
    midiChannel = juce::jlimit (1, 16, newMidiChannel);
    age = newAge;
    targetPitch = (double) note + (double) pitchOffset;
    if (p.portamento <= 0.0f)
        currentPitch = targetPitch;

    if (retrigger)
    {
        velocity = newVelocity;
        if (! isAudible())
        {
            dds1.noteOn (p.superMode);
            dds2.noteOn();
        }
        if (p.lfo1Mode != Lfo1Mode::freeOrNorm && toInt (p.lfo1Wave) < toInt (Lfo1Wave::hf))
        {
            lfo1.setPhase (side == Side::right ? p.lfo1LrPhase : 0.0);
            lfo1.noteOn (false, p.lfo1Mode == Lfo1Mode::onceOrDds1, 0.0, rng); // re-arm ONCE
        }
        lfo1Ramp.noteOn();
        lfo2Ramp.noteOn();
        for (size_t m = 0; m < 2; ++m)
            if (p.msegMode[m] != MsegMode::free)
            {
                msegPhase[m] = 0.0;
                msegHint[m] = 0;
            }
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
    stealPending = false;
    active = false;
    gate = false;
    env1.reset();
    env2.reset();
    fixedEnv.reset();
    lastModSources.fill (0.0f);
}

float Voice::lfo1RateHz (const LayerSnapshot& p, float rateFader, const VoiceContext& ctx) const noexcept
{
    switch (p.lfo1Wave)
    {
        case Lfo1Wave::hf:      return (float) rates::highFrequencyHz (rateFader);
        case Lfo1Wave::hfTrack: return (float) (noteToHz (currentPitch + (double) ctx.layerPitchOffset) * rates::hfTrackRatio (rateFader));
        case Lfo1Wave::triangle:
        case Lfo1Wave::revSaw:
        case Lfo1Wave::sampleHold:
        case Lfo1Wave::square:  break;
    }

    if (p.lfo1Sync)
    {
        const auto& beats = rates::lfo1SyncBeats();
        const double b = beats[(size_t) rates::divisionIndex (rateFader, (int) beats.size())];
        return (float) (ctx.bpm / 60.0 / b);
    }
    return (float) rates::lowFrequencyHz (rateFader);
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
        case Lfo1Wave::sampleHold: lfo1Shape = p.lfo1Rate >= 0.99f && ! p.lfo1Sync ? LfoShape::noise : LfoShape::sampleHold; break;
        case Lfo1Wave::square:     lfo1Shape = LfoShape::square; break;
        case Lfo1Wave::hf:
        case Lfo1Wave::hfTrack:    lfo1Shape = LfoShape::sine; break;
    }
    lfo1Unipolar = Lfo::isUnipolar (lfo1Shape);
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
    const bool ribbonMapped = matrix.sourceUsed[(size_t) ModSource::ribbon];
    const float ribbon = gate && ! ribbonMapped ? ctx.ribbonSemitones : 0.0f; // assigned ribbon becomes a matrix-only source

    const float keyTrack = triStateAmount (p.vcfKeytrack);
    const float range1 = rangeOffset (p.dds1Range);
    const bool dds2IsLfo = p.dds2Range == Dds2Range::lfo;
    const float range2 = rangeOffset (p.dds2Range);
    const bool ringMode = ! dds2IsLfo && p.dds2Mode == Dds2Mode::ringOrSubSquare;
    const bool syncMode = ! dds2IsLfo && p.dds2Mode == Dds2Mode::syncOrSubSine;
    const bool subOn = dds2IsLfo && p.dds2Mode != Dds2Mode::normOrSubOff;
    const bool subSine = p.dds2Mode == Dds2Mode::syncOrSubSine;
    const bool filter2On = p.f2Routing != Filter2Routing::off;
    const bool filter2Serial = p.f2Routing == Filter2Routing::serial;

    // ---- modulation slots ------------------------------------------------------
    // Every active slot with a per-voice destination adds shape(source) x amount to the normalised
    // value of its parameter. The modulated values live in `scratch`, a per-block copy of `p` whose
    // modulated fields are rewritten every sample; everything below reads the parameters from `e`.
    const auto& indices = paramIndices();
    std::array<uint8_t, ModMatrixData::kMaxSlots> slotList {};
    std::array<int8_t, ModMatrixData::kMaxSlots> slotDestination {};
    std::array<int, ModMatrixData::kMaxSlots> destinations {};
    std::array<float*, ModMatrixData::kMaxSlots> destinationField {};
    std::array<const juce::NormalisableRange<float>*, ModMatrixData::kMaxSlots> destinationRange {};
    std::array<float, ModMatrixData::kMaxSlots> baseNorm {}, offsets {};
    std::array<int8_t, kLayerParamCount> destinationPosition;
    int slotCount = 0, destinationCount = 0;
    LayerSnapshot scratch;
    const bool modulating = matrix.anyVoiceDestination;
    if (modulating)
    {
        scratch = p;
        destinationPosition.fill (-1);
        for (int k = 0; k < matrix.activeSlotCount; ++k)
        {
            const int s = matrix.activeSlots[(size_t) k];
            if (matrix.slotOnLayer[(size_t) s])
                continue;
            const int destination = matrix.slots[(size_t) s].destination;
            auto& position = destinationPosition[(size_t) destination];
            if (position < 0)
            {
                position = (int8_t) destinationCount;
                destinations[(size_t) destinationCount] = destination;
                destinationField[(size_t) destinationCount] = scratch.floatField (destination);
                destinationRange[(size_t) destinationCount] = &getLayerParamRange (destination);
                baseNorm[(size_t) destinationCount] = getLayerParamRange (destination).convertTo0to1 (*p.floatField (destination));
                ++destinationCount;
            }
            slotList[(size_t) slotCount] = (uint8_t) s;
            slotDestination[(size_t) slotCount] = position;
            ++slotCount;
        }
    }
    const LayerSnapshot& e = modulating ? scratch : p;
    auto isModulated = [&] (int index) { return modulating && destinationPosition[(size_t) index] >= 0; };
    bool envelopeTargets = false;
    for (int d = 0; d < destinationCount; ++d)
        envelopeTargets |= indices.envelope[(size_t) destinations[(size_t) d]];
    const bool glideModulated = isModulated (indices.portamento);
    const bool lfo2RateModulated = isModulated (indices.lfo2Rate) || isModulated (indices.lfo2RateMod);
    const std::array<bool, 2> msegTimeModulated { isModulated (indices.msegTime[0]), isModulated (indices.msegTime[1]) };
    const std::array<bool, 2> msegUsed { matrix.sourceUsed[(size_t) ModSource::mseg1], matrix.sourceUsed[(size_t) ModSource::mseg2] };

    const double baseGlideRate = p.portamento > 0.0f
                               ? 12.0 / ((double) portamentoSecondsPerOctave (p.portamento) * sampleRate) : 1.0e9;
    const double baseLfo2Hz = rates::lowFrequencyHz (p.lfo2Rate) * std::exp2 (3.0 * p.lfo2RateMod * lfo2Push);
    std::array<double, 2> msegIncrement { 1.0 / (msegSeconds (p, 0, ctx.bpm) * sampleRate),
                                          1.0 / (msegSeconds (p, 1, ctx.bpm) * sampleRate) };

    Envelope& ampEnv = ampEnvUsesEnv2 ? env2 : fixedEnv;

    Dds1::Settings d1s;
    d1s.wave = p.dds1Wave;
    d1s.superMode = p.superMode;
    d1s.altA = p.altA;
    d1s.altB = p.altB;
    d1s.mirrorDetune = side == Side::right;

    // The LFO1 rate (a pow() per sample) is only recomputed when its fader moves.
    const auto lfo1Wave = p.lfo1Wave;
    float lastLfo1Fader = -1.0f, lastLfo1Hz = 0.0f;

    // Sources are 0..1; LFOs, DDS2 and ribbon rest at 0.5. Evaluated per sample only while slots
    // modulate this voice, otherwise once per block (editor, effect slots, note-on routes).
    float lastE1 = 0.0f, lastE2 = 0.0f;
    std::array<float, 2> lastMseg {};
    auto makeSources = [&] () noexcept
    {
        return std::array<float, ModMatrixData::sourceCount> {
            juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * dds2.getLastOutput()),
            juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * lastLfo2),
            juce::jlimit (0.0f, 1.0f, lastE1),
            juce::jlimit (0.0f, 1.0f, velocity),
            juce::jlimit (0.0f, 1.0f, at),
            juce::jlimit (0.0f, 1.0f, ctx.expression),
            juce::jlimit (0.0f, 1.0f, 0.5f + ctx.ribbonSemitones / 24.0f),
            juce::jlimit (0.0f, 1.0f, (float) note / 127.0f),
            isMpeMember ? juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * midiPitchBend) : 0.5f,
            isMpeMember ? juce::jlimit (0.0f, 1.0f, midiTimbre) : 0.0f,
            juce::jlimit (0.0f, 1.0f, lastE2),
            juce::jlimit (0.0f, 1.0f, lfo1Unipolar ? lastLfo1 : 0.5f + 0.5f * lastLfo1),
            lastMseg[0],
            lastMseg[1],
            juce::jlimit (0.0f, 1.0f, ctx.modLever),
            randomValue,
            p.macros[0], p.macros[1], p.macros[2], p.macros[3], p.macros[4], p.macros[5], p.macros[6], p.macros[7]
        };
    };

    for (int i = startSample; i < startSample + numSamples; ++i)
    {
        // ---- modulators ----------------------------------------------------
        const float e1raw = env1.process();
        const float e1 = p.env1Mode == Env1Mode::inverted ? -e1raw : e1raw;
        const float e2 = env2.process();
        if (! ampEnvUsesEnv2)
            fixedEnv.process();
        const float ampLevel = ampEnv.getValue();

        std::array<float, 2> mseg {};
        for (size_t m = 0; m < 2; ++m)
        {
            if (msegTimeModulated[m])
                msegIncrement[m] = 1.0 / (msegSeconds (e, (int) m, ctx.bpm) * sampleRate);
            if (msegUsed[m])
                mseg[m] = matrix.msegs[m].evaluate ((float) msegPhase[m], msegHint[m]);
            msegPhase[m] += msegIncrement[m];
            if (msegPhase[m] >= 1.0)
                msegPhase[m] = p.msegMode[m] == MsegMode::envelope ? 1.0 : msegPhase[m] - std::floor (msegPhase[m]);
        }

        lastE1 = e1;
        lastE2 = e2;
        lastMseg = mseg;
        if (modulating)
        {
            const auto modSources = makeSources();
            std::fill (offsets.begin(), offsets.begin() + destinationCount, 0.0f);
            for (int k = 0; k < slotCount; ++k)
            {
                const auto& slot = matrix.slots[slotList[(size_t) k]];
                offsets[(size_t) slotDestination[(size_t) k]] += slot.shape (modSources[(size_t) slot.source]) * slot.amount;
            }
            for (int d = 0; d < destinationCount; ++d)
                *destinationField[(size_t) d] = destinationRange[(size_t) d]->convertFrom0to1 (
                    juce::jlimit (0.0f, 1.0f, baseNorm[(size_t) d] + offsets[(size_t) d]));
            if (envelopeTargets && (i & 7) == 0)
                applyEnvelopeSettings (e);
        }

        const double glideRate = glideModulated
                               ? (e.portamento > 0.0f ? 12.0 / ((double) portamentoSecondsPerOctave (e.portamento) * sampleRate) : 1.0e9)
                               : baseGlideRate;
        const double lfo2Hz = lfo2RateModulated
                            ? rates::lowFrequencyHz (e.lfo2Rate) * fastmath::exp2 (3.0 * e.lfo2RateMod * lfo2Push)
                            : baseLfo2Hz;
        const float l2 = lfo2.process (lfo2Hz, lfo2Shape, rng) * lfo2Ramp.process (e.lfo2Delay) * lfo2Depth;
        lastLfo2 = l2;
        const float lfo1Fader = juce::jlimit (0.0f, 1.0f, e.lfo1Rate);
        if (lfo1Fader != lastLfo1Fader || lfo1Wave == Lfo1Wave::hfTrack)
        {
            lastLfo1Fader = lfo1Fader;
            lastLfo1Hz = lfo1RateHz (e, lfo1Fader, ctx);
        }
        const float lfo1Hz = lastLfo1Hz;
        const float l1Raw = lfo1.process (lfo1Hz, lfo1Shape, rng);
        const float l1 = lfo1Hf ? l1Raw : l1Raw * lfo1Ramp.process (e.lfo1Delay);
        const float l1Mod = lfo1Hf && p.lfo1Mode != Lfo1Mode::freeOrNorm ? 0.0f : l1; // routed HF LFO is audio, not CV
        lastLfo1 = l1Mod;

        // ---- pitch -----------------------------------------------------------
        if (currentPitch < targetPitch)      currentPitch = juce::jmin (targetPitch, currentPitch + glideRate);
        else if (currentPitch > targetPitch) currentPitch = juce::jmax (targetPitch, currentPitch - glideRate);

        const float driftAmt = e.drift;
        const float drift1 = driftAmt > 0.0f ? driftWalk (driftValue[0], driftTarget[0], driftCounter[0]) * driftAmt * 0.15f : 0.0f;
        const float drift2 = driftAmt > 0.0f ? driftWalk (driftValue[1], driftTarget[1], driftCounter[1]) * driftAmt * 0.15f : 0.0f;
        const float driftCut = driftAmt > 0.0f ? driftWalk (driftValue[2], driftTarget[2], driftCounter[2]) * driftAmt * 0.15f : 0.0f;

        const float lfo1PitchSt = e.modLfo1 * e.modLfo1 * 12.0f;
        const float env1PitchSt = e.modEnv1 * e.modEnv1 * 48.0f;
        const float lfo2PitchSt = e.lfo2Dds * e.lfo2Dds * 12.0f;
        // MPE: a member channel's bend is the note's exact pitch (bend x its sensitivity in
        // semitones); BEND DDS scales the conventional bend and the zone master channel's bend.
        const float perfPitch = isMpeMember
            ? midiPitchBend * midiBendSensitivity
                  + ((bendTotal - midiPitchBend) + masterBend * (masterBendSensitivity / 2.0f)) * e.bendDds
            : bendTotal * e.bendDds * (midiBendSensitivity / 2.0f);
        const float oscMod = l1Mod * lfo1PitchSt + e1 * env1PitchSt;
        const float perfMod = perfPitch + l2 * lfo2PitchSt;
        const double base = currentPitch + (double) ctx.layerPitchOffset + (double) ribbon + (double) channelTuning
                          + (double) (twinSign * e.binauralDetune * 0.01f) + (double) unisonDetune;

        double pitch1 = base + range1 + drift1 + (toDds1 ? oscMod : 0.0f) + (perfToDds1 ? perfMod : 0.0f);
        double pitch2 = base + range2 + sTune.next (e.dds2Tune, smooth) + drift2 + (toDds2 ? oscMod : 0.0f) + (perfToDds2 ? perfMod : 0.0f);

        // ---- DDS modulator ---------------------------------------------------
        const float morphFader = sMorph.next (juce::jlimit (0.0f, 1.0f, e.pwmWave), smooth);
        float morph = morphFader, pwm = morphFader;
        if (p.pwmSource == PwmSource::lfo1)
        {
            const float l1Uni = lfo1Unipolar ? l1Mod : 0.5f * (l1Mod + 1.0f);
            morph = pwm = morphFader * l1Uni;
        }
        else if (p.pwmSource == PwmSource::env1)
        {
            morph = pwm = morphFader * juce::jmax (0.0f, e1);
        }

        const float detune = sDetune.next (e.pwDetune, smooth);
        d1s.detune = detune;
        d1s.morph = juce::jlimit (0.0f, 1.0f, morph);
        const float pulseWidth = juce::jlimit (0.02f, 0.98f, 0.5f - 0.45f * (detune + pwm));

        const float crossOct = sCross.next (juce::jlimit (0.0f, 1.0f, e.crossMod), smooth) * 4.0f;
        double f1 = noteToHz (pitch1);
        double f2 = dds2IsLfo ? rates::dds2LfoHz (e.dds2Tune) * (double) fastExp2 ((float) ((pitch2 - base - range2 - e.dds2Tune) / 12.0))
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

        const float m = juce::jlimit (0.0f, 1.0f, sMix.next (juce::jlimit (0.0f, 1.0f, e.mix), smooth));
        const float osc = ch1 * (1.0f - m) + ch2 * m;

        // ---- VCF -------------------------------------------------------------
        float envSrc = e1;
        if (p.vcfEnvSource == EnvSource::both) envSrc = e1 + e2;
        else if (p.vcfEnvSource == EnvSource::env2) envSrc = e2;

        const float dyn = juce::jlimit (0.0f, 1.0f, e.velSens);
        const float velOct = dyn * (velocity - 1.0f) * 3.0f;
        const float octaves = e.vcfEnv * 8.0f * envSrc
                            + e.vcfLfo1 * 4.0f * l1Mod
                            + e.vcfDds2 * 4.0f * d2
                            + e.lfo2Vcf * 4.0f * l2
                            + e.bendVcf * 10.0f * bendTotal
                            + keyTrack * ((float) currentPitch - 60.0f) / 12.0f
                            + velOct + driftCut;

        const float lpfBase = sLpf.next (e.lpfHz, smooth);
        const float hpfBase = sHpf.next (e.hpfHz, smooth);
        filter.setParameters (hpfBase * fastExp2 (driftCut), openLadderCutoff (lpfBase * fastExp2 (octaves)),
                              sRes.next (e.res, smooth), p.drive, p.vcfType);
        float filtered = filter.process (osc);
        if (! std::isfinite (filtered))
        {
            // Never let a numerical blow-up reach the FX feedback loops (it would silence the layer).
            filter.reset();
            filtered = 0.0f;
        }

        // ---- FILTER 2 (serial after the VCF or parallel to it) -----------------
        if (filter2On)
        {
            float envSrc2 = e1;
            if (p.f2EnvSource == EnvSource::both) envSrc2 = e1 + e2;
            else if (p.f2EnvSource == EnvSource::env2) envSrc2 = e2;
            const float octaves2 = e.f2Env * 8.0f * envSrc2 + e.f2Keytrack * ((float) currentPitch - 60.0f) / 12.0f + driftCut;
            const float cutoff2 = sF2Cutoff.next (e.f2Cutoff, smooth) * fastExp2 (octaves2);
            const float input2 = filter2Serial ? filtered : osc;
            float y2 = filter2.process (input2, p.f2Type, cutoff2, e.f2Res, e.f2Drive);
            if (! std::isfinite (y2))
            {
                filter2.reset();
                y2 = 0.0f;
            }
            filtered += (y2 - filtered) * juce::jlimit (0.0f, 1.0f, e.f2Mix);
        }

        // ---- VCA -------------------------------------------------------------
        // [ASSUMPTION] LFO/DDS2/LFO2 amounts act as multiplicative CV on the envelope level.
        const float velGain = 1.0f - dyn + dyn * velocity;
        float gain = sVcaEnv.next (e.vcaEnv, smooth) * ampLevel * velGain;
        gain *= juce::jmax (0.0f, 1.0f + e.vcaLfo1 * l1Mod);
        gain *= juce::jmax (0.0f, 1.0f + e.vcaDds2 * d2);
        gain *= juce::jmax (0.0f, 1.0f + e.lfo2Vca * l2);

        float out = filtered * gain * kOutputGain * voiceGain;
        bool startPending = false;
        if (stealPending)
        {
            // Raised-cosine fade: no slope corners at either end.
            stealGain -= (float) (1.0 / (kStealFadeSeconds * sampleRate));
            out *= stealGain > 0.0f ? (float) (0.5 - 0.5 * fastmath::sin2pi (0.25 + 0.5 * (double) stealGain)) : 0.0f;
            startPending = stealGain <= 0.0f;
        }
        outL[i] += out * panL;
        outR[i] += out * panR;

        if (startPending || (stealPending && ! ampEnv.isActive()))
        {
            // The old sound is silent: start the new note from zero (a note-off that arrived
            // during the fade is applied right after).
            const bool released = ! gate;
            stealPending = false;
            begin (pendingStart, p, true);
            if (released)
                release();
            d1s.mirrorDetune = side == Side::right;
            continue;
        }
        if (! ampEnv.isActive())
        {
            active = false;
            break;
        }
    }

    lastModSources = makeSources();
    for (int d = 0; d < destinationCount; ++d)
        modOffsets[(size_t) destinations[(size_t) d]] = offsets[(size_t) d];
}
} // namespace nsynth
