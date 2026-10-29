#include "SynthLayer.h"

namespace nsynth
{
namespace
{
double msegCycleSeconds (const LayerSnapshot& s, int m, double bpm) noexcept
{
    return s.msegSync[(size_t) m] ? rates::msegDivisionBeats (toInt (s.msegDivision[(size_t) m])) * 60.0 / juce::jmax (1.0, bpm)
                                  : (double) juce::jmax (0.001f, s.msegTime[(size_t) m]);
}
} // namespace

SynthLayer::SynthLayer (juce::AudioProcessorValueTreeState& state, Layer layerId)
    : id (layerId), reader (state, layerId), rng (layerId == Layer::upper ? 11 : 23)
{
    for (auto& offset : uiModOffset)
        offset.store (0.0f);
    for (auto& phase : uiMsegPhase)
        phase.store (-1.0f);
    layerSources.fill (0.0f);
}

float SynthLayer::getModulationOffset (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= (int) kLayerParamCount)
        return 0.0f;
    return uiModOffset[(size_t) parameterIndex].load (std::memory_order_relaxed);
}

void SynthLayer::prepare (double hostSampleRate, int maxBlockSize, VoiceRenderPool& pool)
{
    hostRate = hostSampleRate;
    maxBlock = maxBlockSize;
    renderPool = &pool;

    static_assert (kOversamplingFactor == 2, "one half-band stage");
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, (size_t) 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing ((size_t) maxBlockSize);
    oversampler->reset();

    hostScratch.setSize (2, maxBlockSize);
    keyBuffer.setSize (2, maxBlockSize);
    keyBuffer.clear();
    fxDry.setSize (2, maxBlockSize);
    parallelScratch.setSize (2 * (pool.getNumWorkers() + 1), maxBlockSize * kOversamplingFactor);
    air.prepare (hostSampleRate);
    chorus.prepare (hostSampleRate, maxBlockSize);
    delay.prepare (hostSampleRate, maxBlockSize);
    reverb.prepare (hostSampleRate, maxBlockSize);
    distortion.prepare (hostSampleRate);
    eq.prepare (hostSampleRate);
    compressor.prepare (hostSampleRate);
    phaser.prepare (hostSampleRate);
    flanger.prepare (hostSampleRate, maxBlockSize);
    fxFade.fill (-1.0f);
    fxIdle.fill (true);

    const double osRate = hostRate * kOversamplingFactor;
    for (auto* v : voices)
    {
        v->kill();
        v->setSampleRate (osRate);
    }
}

float SynthLayer::getLatencySamples() const noexcept
{
    return oversampler != nullptr ? oversampler->getLatencyInSamples() : 0.0f;
}

void SynthLayer::assignVoices (Voice* first, int count)
{
    voices.clear();
    const double osRate = hostRate * getOversamplingFactor();
    for (int i = 0; i < count; ++i)
    {
        first[i].kill();
        first[i].setSampleRate (osRate);
        voices.push_back (first + i);
    }
    heldNotes.clear();
    heldChannels.clear();
    heldVelocities.clear();
    sustainedNotes.clear();
    sustainedChannels.clear();
    monoUnits = 0;
}

void SynthLayer::beginBlock (int numHostSamples, const VoiceContext& context, uint64_t* sharedAgeCounter)
{
    const bool wasSustained = pedal || snap.hold;
    reader.read (snap);
    ctx = context;
    ctx.layerPitchOffset += (float) (snap.octave * 12);
    ageCounter = sharedAgeCounter != nullptr ? sharedAgeCounter : &localAge;
    blockModMatrix = modMatrix.snapshot();

    // Changing the voice structure re-pairs the hardware voices: start clean.
    const bool wasMono = lastVoiceMode == VoiceMode::solo || lastVoiceMode == VoiceMode::legato;
    if (snap.binaural != lastBinaural || wasMono != isMono())
        allNotesOff (true);
    lastBinaural = snap.binaural;
    lastVoiceMode = snap.voiceMode;

    if (wasSustained && ! (pedal || snap.hold))
        releaseHeldBySustainEnd();

    hostScratch.clear (0, numHostSamples);
    juce::dsp::AudioBlock<float> hostBlock (hostScratch);
    osBlock = oversampler->processSamplesUp (hostBlock.getSubBlock (0, (size_t) numHostSamples));
    osBlock.clear();

    lfo2MasterPhase += rates::lowFrequencyHz (snap.lfo2Rate) * numHostSamples / hostRate;
    lfo2MasterPhase -= std::floor (lfo2MasterPhase);
    for (int m = 0; m < 2; ++m)
    {
        auto& phase = msegMasterPhase[(size_t) m];
        phase += numHostSamples / (msegCycleSeconds (snap, m, ctx.bpm) * hostRate);
        phase -= std::floor (phase);
    }
}

void SynthLayer::renderSegment (int hostStart, int hostNum) noexcept
{
    if (hostNum <= 0)
        return;

    const int f = getOversamplingFactor();
    const int start = hostStart * f;
    const int count = hostNum * f;

    VoiceRenderTask task;
    task.layer = this;
    task.startSample = start;
    task.numSamples = count;
    for (auto* voice : voices)
        if (voice->isActive())
            task.activeVoices[(size_t) task.activeVoiceCount++] = voice;

    if (task.activeVoiceCount == 0)
        return;

    const int availableWorkers = renderPool != nullptr ? renderPool->getNumWorkers() : 0;
    task.laneCount = juce::jmin (availableWorkers + 1, task.activeVoiceCount);
    if (task.laneCount < 2 || task.activeVoiceCount < 2)
    {
        float* left = osBlock.getChannelPointer (0);
        float* right = osBlock.getChannelPointer (1);
        for (int i = 0; i < task.activeVoiceCount; ++i)
            task.activeVoices[(size_t) i]->render (left, right, start, count, snap, ctx, *blockModMatrix);
        return;
    }

    for (int lane = 0; lane < task.laneCount; ++lane)
    {
        parallelScratch.clear (lane * 2, start, count);
        parallelScratch.clear (lane * 2 + 1, start, count);
    }

    renderPool->renderItems (task.activeVoiceCount, task.laneCount - 1, &task, &SynthLayer::renderSingleVoice);
    for (int lane = 0; lane < task.laneCount; ++lane)
    {
        juce::FloatVectorOperations::add (osBlock.getChannelPointer (0) + start,
                                          parallelScratch.getReadPointer (lane * 2) + start, count);
        juce::FloatVectorOperations::add (osBlock.getChannelPointer (1) + start,
                                          parallelScratch.getReadPointer (lane * 2 + 1) + start, count);
    }
}

void SynthLayer::renderVoiceRange (void* rawContext, int lane) noexcept
{
    auto& task = *static_cast<VoiceRenderTask*> (rawContext);
    auto& layer = *task.layer;

    const int firstVoice = task.activeVoiceCount * lane / task.laneCount;
    const int lastVoice = task.activeVoiceCount * (lane + 1) / task.laneCount;
    float* left = layer.parallelScratch.getWritePointer (lane * 2);
    float* right = layer.parallelScratch.getWritePointer (lane * 2 + 1);

    for (int i = firstVoice; i < lastVoice; ++i)
        task.activeVoices[(size_t) i]->render (left, right, task.startSample, task.numSamples, layer.snap, layer.ctx,
                                               *layer.blockModMatrix);
}

void SynthLayer::renderSingleVoice (void* rawContext, int itemIndex, int lane) noexcept
{
    auto& task = *static_cast<VoiceRenderTask*> (rawContext);
    auto& layer = *task.layer;
    float* left = layer.parallelScratch.getWritePointer (lane * 2);
    float* right = layer.parallelScratch.getWritePointer (lane * 2 + 1);
    task.activeVoices[(size_t) itemIndex]->render (left, right, task.startSample, task.numSamples,
                                                   layer.snap, layer.ctx, *layer.blockModMatrix);
}

void SynthLayer::finishBlock (juce::AudioBuffer<float>& dest, int numHostSamples)
{
    finishVoices (numHostSamples);
    applyEffects (dest, numHostSamples, nullptr);
}

void SynthLayer::finishVoices (int numHostSamples)
{
    juce::dsp::AudioBlock<float> hostBlock (hostScratch);
    auto outBlock = hostBlock.getSubBlock (0, (size_t) numHostSamples);
    oversampler->processSamplesDown (outBlock);

    // A non-finite sample would circulate forever in the effect feedback paths and silence the
    // layer until reload: drop the block and restart the signal path cleanly.
    bool finite = true;
    for (int ch = 0; ch < 2 && finite; ++ch)
    {
        const float* d = hostScratch.getReadPointer (ch);
        for (int i = 0; i < numHostSamples && finite; ++i)
            finite = std::isfinite (d[i]);
    }
    if (! finite)
    {
        hostScratch.clear (0, numHostSamples);
        oversampler->reset();
        air.reset();
        for (int m = 0; m < ModMatrixData::kFxModules; ++m)
            resetFxModule ((FxModule) m);
    }

    // Layer PAN as a stereo balance control (modulated pan is applied with the effects).
    static const int panIndex = findLayerSpecIndex (pid::pan);
    if (blockModMatrix == nullptr || ! blockModMatrix->modulated[(size_t) panIndex])
    {
        const float gl = juce::jmin (1.0f, 1.0f - snap.pan), gr = juce::jmin (1.0f, 1.0f + snap.pan);
        hostScratch.applyGain (0, 0, numHostSamples, gl);
        hostScratch.applyGain (1, 0, numHostSamples, gr);
    }
    air.process (hostScratch, 0, numHostSamples);
    for (int ch = 0; ch < 2; ++ch)
        keyBuffer.copyFrom (ch, 0, hostScratch, ch, 0, numHostSamples);

    // Editor feedback: what the newest voice is doing.
    const Voice* newest = nullptr;
    for (auto* voice : voices)
        if (voice->isActive() && (newest == nullptr || voice->getAge() > newest->getAge()))
            newest = voice;
    if (newest != nullptr)
        layerSources = newest->getLastModSources();
    if (blockModMatrix != nullptr)
        for (size_t d = 0; d < kLayerParamCount; ++d)
            if (blockModMatrix->modulated[d] && ! ModMatrix::isLayerDestination ((int) d))
                uiModOffset[d].store (newest != nullptr ? newest->getModOffsets()[d] : 0.0f, std::memory_order_relaxed);
    for (int m = 0; m < 2; ++m)
        uiMsegPhase[(size_t) m].store (newest != nullptr ? newest->getMsegPhase (m) : -1.0f, std::memory_order_relaxed);
}

bool SynthLayer::fxModuleEnabled (FxModule module, const LayerSnapshot& fx) const noexcept
{
    switch (module)
    {
        case FxModule::distortion: return fx.distOn;
        case FxModule::eq:         return fx.eqOn;
        case FxModule::compressor: return fx.compOn;
        case FxModule::chorus:     return fx.chorus != ChorusMode::off;
        case FxModule::phaser:     return fx.phaserOn;
        case FxModule::flanger:    return fx.flangerOn;
        case FxModule::delay:      return fx.delayOn;
        case FxModule::reverb:     return fx.reverbOn;
        case FxModule::count:      break;
    }
    return false;
}

void SynthLayer::resetFxModule (FxModule module) noexcept
{
    switch (module)
    {
        case FxModule::distortion: distortion.reset(); break;
        case FxModule::eq:         eq.reset(); break;
        case FxModule::compressor: compressor.reset(); break;
        case FxModule::chorus:     chorus.reset(); break;
        case FxModule::phaser:     phaser.reset(); break;
        case FxModule::flanger:    flanger.reset(); break;
        case FxModule::delay:      delay.reset(); break;
        case FxModule::reverb:     reverb.reset(); break;
        case FxModule::count:      break;
    }
}

void SynthLayer::processFxModule (FxModule module, juce::AudioBuffer<float>& buffer, int n, const LayerSnapshot& fx,
                                  double delaySeconds, const juce::AudioBuffer<float>* key) noexcept
{
    switch (module)
    {
        case FxModule::distortion:
            distortion.process (buffer, 0, n, { fx.distType, fx.distDrive, fx.distTone, fx.distMix });
            break;
        case FxModule::eq:
            eq.process (buffer, 0, n, { fx.eqLowFreq, fx.eqLowGain, fx.eqMidFreq, fx.eqMidGain, fx.eqMidQ, fx.eqHighFreq, fx.eqHighGain });
            break;
        case FxModule::compressor:
            compressor.process (buffer, 0, n, { fx.compThreshold, fx.compRatio, fx.compAttackMs, fx.compReleaseMs, fx.compMakeup, fx.compMix },
                                fx.compKey == CompressorKey::otherLayer ? key : nullptr);
            break;
        case FxModule::chorus:
        {
            StereoChorus::Settings settings;
            settings.mode = fx.chorus;
            settings.rateHz = fx.chorusRate;
            settings.depth = fx.chorusDepth;
            settings.delayMs = fx.chorusDelayMs;
            settings.feedback = fx.chorusFeedback;
            settings.width = fx.chorusWidth;
            settings.mix = fx.chorusMix;
            chorus.process (buffer, 0, n, settings);
            break;
        }
        case FxModule::phaser:
            phaser.process (buffer, 0, n, { fx.phaserRate, fx.phaserDepth, fx.phaserFeedback, fx.phaserMix });
            break;
        case FxModule::flanger:
            flanger.process (buffer, 0, n, { fx.flangerRate, fx.flangerDepth, fx.flangerFeedback, fx.flangerMix });
            break;
        case FxModule::delay:
        {
            StereoDelay::Settings settings;
            settings.timeSeconds = delaySeconds;
            settings.feedback = juce::jlimit (0.0f, 1.0f, fx.delayFeedback);
            settings.mix = juce::jlimit (0.0f, 1.0f, fx.delayMix);
            settings.pingPong = fx.delayPingPong;
            settings.freeze = fx.delayFreeze;
            settings.lowCutHz = fx.delayLowCutHz;
            settings.highCutHz = fx.delayHighCutHz;
            settings.duck = fx.delayDuck;
            delay.process (buffer, 0, n, settings);
            break;
        }
        case FxModule::reverb:
        {
            StereoReverb::Settings settings;
            settings.mix = fx.reverbMix;
            settings.size = fx.reverbSize;
            settings.decaySeconds = fx.reverbDecay;
            settings.preDelayMs = fx.reverbPreDelayMs;
            settings.dampingHz = fx.reverbDampingHz;
            settings.lowCutHz = fx.reverbLowCutHz;
            settings.width = fx.reverbWidth;
            settings.modulation = fx.reverbModulation;
            settings.duck = fx.reverbDuck;
            reverb.process (buffer, 0, n, settings);
            break;
        }
        case FxModule::count:
            break;
    }
}

void SynthLayer::applyEffects (juce::AudioBuffer<float>& dest, int numHostSamples, const juce::AudioBuffer<float>* otherLayerKey)
{
    // Effect parameters with modulation slots: evaluated once per block from the sources of the
    // newest voice (the last ones heard while the tails ring out); macros, mod wheel and
    // expression always follow their current values.
    static const int delayMsIndex = findLayerSpecIndex (pid::delayMs);
    static const int panIndex = findLayerSpecIndex (pid::pan);
    LayerSnapshot fx = snap;
    float delayTimeOffset = 0.0f;
    if (blockModMatrix != nullptr && blockModMatrix->anyLayerDestination)
    {
        auto sources = layerSources;
        for (int m = 0; m < ModMatrixData::kMacros; ++m)
            sources[(size_t) ModSource::macro1 + (size_t) m] = snap.macros[(size_t) m];
        sources[(size_t) ModSource::modWheel] = juce::jlimit (0.0f, 1.0f, ctx.modLever);
        sources[(size_t) ModSource::expression] = juce::jlimit (0.0f, 1.0f, ctx.expression);

        std::array<float, kLayerParamCount> offsets {};
        const auto& matrix = *blockModMatrix;
        for (int k = 0; k < matrix.activeSlotCount; ++k)
        {
            const int s = matrix.activeSlots[(size_t) k];
            if (! matrix.slotOnLayer[(size_t) s])
                continue;
            const auto& slot = matrix.slots[(size_t) s];
            offsets[(size_t) slot.destination] += slot.shape (sources[(size_t) slot.source]) * slot.amount;
        }
        for (size_t d = 0; d < kLayerParamCount; ++d)
        {
            if (! matrix.modulated[d] || ! ModMatrix::isLayerDestination ((int) d))
                continue;
            const auto& range = getLayerParamRange ((int) d);
            *fx.floatField ((int) d) = range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, range.convertTo0to1 (*snap.floatField ((int) d)) + offsets[d]));
            uiModOffset[d].store (offsets[d], std::memory_order_relaxed);
        }
        delayTimeOffset = offsets[(size_t) delayMsIndex];

        if (matrix.modulated[(size_t) panIndex])
        {
            const float gl = juce::jmin (1.0f, 1.0f - fx.pan), gr = juce::jmin (1.0f, 1.0f + fx.pan);
            hostScratch.applyGain (0, 0, numHostSamples, gl);
            hostScratch.applyGain (1, 0, numHostSamples, gr);
        }
    }

    double delaySeconds = 0.0;
    if (snap.delaySync)
    {
        // DELAY TIME modulation moves through the divisions.
        constexpr int divisions = toInt (DelayDivision::d1_1) + 1;
        const float fader = ((float) toInt (snap.delayDivision) + 0.5f) / (float) divisions + delayTimeOffset;
        delaySeconds = rates::delayDivisionBeats (rates::divisionIndex (fader, divisions)) * 60.0 / ctx.bpm;
    }
    else
    {
        delaySeconds = 0.001 * (double) fx.delayMs;
    }

    // The modules in the patch's order. Switching one on or off crossfades over 10 ms; an
    // inactive module is reset once and then skipped. The chorus fades its MODE itself.
    const float fadeStep = (float) (1.0 / (0.01 * hostRate));
    for (const auto module : blockModMatrix != nullptr ? blockModMatrix->fxOrder : ModMatrixData::defaultFxOrder())
    {
        const auto index = (size_t) module;
        if (index >= fxFade.size())
            continue;
        const bool on = fxModuleEnabled (module, fx);
        if (module == FxModule::chorus)
        {
            processFxModule (module, hostScratch, numHostSamples, fx, delaySeconds, otherLayerKey);
            continue;
        }
        auto& fade = fxFade[index];
        if (fade < 0.0f)
            fade = on ? 1.0f : 0.0f;
        if (! on && fade <= 0.0f)
        {
            if (! fxIdle[index])
            {
                resetFxModule (module);
                fxIdle[index] = true;
            }
            continue;
        }
        fxIdle[index] = false;
        if (on && fade >= 1.0f)
        {
            processFxModule (module, hostScratch, numHostSamples, fx, delaySeconds, otherLayerKey);
            continue;
        }
        for (int ch = 0; ch < 2; ++ch)
            fxDry.copyFrom (ch, 0, hostScratch, ch, 0, numHostSamples);
        processFxModule (module, hostScratch, numHostSamples, fx, delaySeconds, otherLayerKey);
        for (int i = 0; i < numHostSamples; ++i)
        {
            fade = juce::jlimit (0.0f, 1.0f, fade + (on ? fadeStep : -fadeStep));
            for (int ch = 0; ch < 2; ++ch)
            {
                const float dry = fxDry.getSample (ch, i);
                hostScratch.setSample (ch, i, dry + fade * (hostScratch.getSample (ch, i) - dry));
            }
        }
    }
    uiCompReduction.store (fx.compOn ? compressor.getGainReductionDb() : 0.0f, std::memory_order_relaxed);

    for (int ch = 0; ch < 2; ++ch)
        dest.copyFrom (ch, 0, hostScratch, ch, 0, numHostSamples);
}

//==============================================================================
bool SynthLayer::unitActive (int unit) const noexcept
{
    for (int j = 0; j < getUnitSize(); ++j)
        if (voices[(size_t) (unit * getUnitSize() + j)]->isActive())
            return true;
    return false;
}

bool SynthLayer::unitGateOn (int unit) const noexcept
{
    return voices[(size_t) (unit * getUnitSize())]->isGateOn();
}

int SynthLayer::unitNote (int unit) const noexcept
{
    return voices[(size_t) (unit * getUnitSize())]->getNote();
}

uint64_t SynthLayer::unitAge (int unit) const noexcept
{
    return voices[(size_t) (unit * getUnitSize())]->getAge();
}

int SynthLayer::pickUnit (int) noexcept
{
    // Priority: idle (longest idle first) -> releasing (oldest) -> playing (oldest).
    int best = 0, bestClass = 3;
    uint64_t bestAge = std::numeric_limits<uint64_t>::max();
    for (int u = 0; u < numUnits(); ++u)
    {
        const int cls = ! unitActive (u) ? 0 : ! unitGateOn (u) ? 1 : 2;
        const auto a = unitAge (u);
        if (cls < bestClass || (cls == bestClass && a < bestAge))
        {
            best = u;
            bestClass = cls;
            bestAge = a;
        }
    }
    return best;
}

float SynthLayer::unisonOffset (int k) const noexcept
{
    if (! snap.unison)
        return 0.0f;
    switch (snap.unisonSize)
    {
        case UnisonSize::octave:      return (k % 2) == 0 ? 0.0f : 12.0f;
        case UnisonSize::fifthOctave: { static constexpr float o[] = { 0.0f, 7.0f, 12.0f }; return o[k % 3]; }
        case UnisonSize::half:
        case UnisonSize::all:         break;
    }
    return 0.0f;
}

int SynthLayer::availableUnisonUnits() const noexcept
{
    return snap.unisonSize == UnisonSize::half ? juce::jmax (1, numUnits() / 2) : numUnits();
}

void SynthLayer::startUnit (int unit, int note, float velocity, float pitchOffset, int stackIndex, int stackSize,
                            bool retrigger, int midiChannel)
{
    const uint64_t age = ++(*ageCounter);
    static const auto lrPhaseRange = [] { return makeRange (*findLayerSpec (pid::lfo1LrPhase)); }();
    const auto matrixData = modMatrix.snapshot();
    static const int lrPhaseIndex = findLayerSpecIndex (pid::lfo1LrPhase);
    float lrPhase = lrPhaseRange.convertTo0to1 (snap.lfo1LrPhase);
    if (matrixData->modulated[(size_t) lrPhaseIndex])
    {
        // LR PHASE / SPREAD is set when the note starts: the sources as they are now.
        std::array<float, ModMatrixData::sourceCount> sources {};
        sources.fill (0.0f);
        sources[(size_t) ModSource::dds2] = unitVoice (unit, 0).getLastModSources()[(size_t) ModSource::dds2];
        sources[(size_t) ModSource::lfo2] = juce::jlimit (0.0f, 1.0f, (float) (0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * lfo2MasterPhase)));
        sources[(size_t) ModSource::velocity] = juce::jlimit (0.0f, 1.0f, velocity);
        sources[(size_t) ModSource::expression] = juce::jlimit (0.0f, 1.0f, ctx.expression);
        sources[(size_t) ModSource::ribbon] = juce::jlimit (0.0f, 1.0f, 0.5f + ctx.ribbonSemitones / 24.0f);
        sources[(size_t) ModSource::noteNumber] = juce::jlimit (0.0f, 1.0f, (float) note / 127.0f);
        sources[(size_t) ModSource::perNotePitch] = 0.5f;
        sources[(size_t) ModSource::lfo1] = 0.5f;
        sources[(size_t) ModSource::modWheel] = juce::jlimit (0.0f, 1.0f, ctx.modLever);
        sources[(size_t) ModSource::random] = rng.nextFloat();
        for (int m = 0; m < ModMatrixData::kMacros; ++m)
            sources[(size_t) ModSource::macro1 + (size_t) m] = snap.macros[(size_t) m];
        for (int k = 0; k < matrixData->activeSlotCount; ++k)
        {
            const auto& slot = matrixData->slots[matrixData->activeSlots[(size_t) k]];
            if (slot.destination == lrPhaseIndex)
                lrPhase += slot.shape (sources[(size_t) slot.source]) * slot.amount;
        }
    }
    const float spread = lrPhaseRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, lrPhase));
    const bool lfo1Restarts = toInt (snap.lfo1Wave) < toInt (Lfo1Wave::hf) && snap.lfo1Mode != Lfo1Mode::freeOrNorm;

    Voice::StartInfo info;
    info.note = note;
    info.midiChannel = midiChannel;
    info.velocity = velocity;
    info.pitchOffset = pitchOffset;
    info.retrigger = retrigger;
    info.age = age;
    info.lfo2Phase = lfo2MasterPhase;
    info.msegPhase = msegMasterPhase;

    // UNISON stacks are spread evenly over +/- 1.5 x DETUNE and scaled to about the level of a
    // single voice (slightly above the power-preserving 1/sqrt(n), which the detune and stereo
    // fan-out lose again), so stacking thickens the sound instead of just phasing and clipping.
    const bool stacked = snap.unison && stackSize > 1;
    const float stackPosition = stacked ? 2.0f * (float) stackIndex / (float) (stackSize - 1) - 1.0f : 0.0f;
    const float stackGain = stacked ? std::pow ((float) stackSize, -0.4f) : 1.0f;
    info.detuneSemitones = stackPosition * snap.binauralDetune * 0.015f;

    if (snap.binaural != BinauralMode::off)
    {
        // Voices per unit: [left, right] (x1) or [left a, right a, left b, right b] (x2).
        // x2 detunes the two voices of each ear in opposite directions, mirrored between
        // the ears, and scales them by 1/sqrt(2) so x1 and x2 play at the same loudness.
        const bool doubled = snap.binaural == BinauralMode::x2;
        static constexpr float twinSigns[] = { -1.0f, 1.0f, 1.0f, -1.0f };
        info.gain = (doubled ? juce::MathConstants<float>::sqrt2 * 0.5f : 1.0f) * stackGain;
        info.width = snap.stereoWidth;
        auto& left = unitVoice (unit, 0);
        const double leftPhase = lfo1Restarts ? 0.0 : -1.0;
        for (int pair = 0; pair < (doubled ? 2 : 1); ++pair)
        {
            info.side = Voice::Side::left;
            info.twinSign = doubled ? twinSigns[pair * 2] : 0.0f;
            info.lfo1Phase = pair == 0 ? leftPhase : left.getLfo1Phase();
            unitVoice (unit, pair * 2).start (info, snap);

            // Right channel: LR PHASE offsets LFO1.
            info.side = Voice::Side::right;
            info.twinSign = doubled ? twinSigns[pair * 2 + 1] : 0.0f;
            info.lfo1Phase = (lfo1Restarts ? 0.0 : left.getLfo1Phase()) + spread;
            unitVoice (unit, pair * 2 + 1).start (info, snap);
        }
        return;
    }

    info.side = Voice::Side::mono;
    info.gain = stackGain;
    if (isMono())
    {
        // SOLO/LEGATO without binaural: unison stacks fan out over STEREO WIDTH, SPREAD offsets the LFO1 phases.
        info.pan = stackPosition * snap.stereoWidth;
        const double offset = spread * (double) stackIndex / (double) juce::jmax (1, stackSize);
        if (lfo1Restarts)
            info.lfo1Phase = offset;
        else
            info.lfo1Phase = stackIndex == 0 ? -1.0 : unitVoice (0, 0).getLfo1Phase() + offset;
    }
    else
    {
        // POLY without binaural: voices alternately panned by SPREAD, unison stacks fan out over WIDTH.
        info.pan = stacked ? stackPosition * snap.stereoWidth : (unit % 2 == 0 ? -spread : spread);
        info.lfo1Phase = lfo1Restarts ? 0.0 : -1.0;
    }
    unitVoice (unit, 0).start (info, snap);
}

void SynthLayer::releaseUnit (int unit) noexcept
{
    for (int j = 0; j < getUnitSize(); ++j)
        unitVoice (unit, j).release();
}

//==============================================================================
float SynthLayer::shapeVelocity (float velocity) const noexcept
{
    if (snap.velMode == VelocityMode::fixed)
        return (float) juce::jlimit (1, 127, snap.velFixed) / 127.0f;

    // Power curve: VEL CURVE -1 => exponent 0.35 (soft touch), +1 => 2.8 (hard touch).
    velocity = juce::jlimit (0.0f, 1.0f, velocity);
    const float curve = juce::jlimit (-1.0f, 1.0f, snap.velCurve);
    return curve == 0.0f ? velocity : std::pow (velocity, std::exp2 (1.5f * curve));
}

void SynthLayer::noteOn (int note, float velocity, int midiChannel)
{
    if (voices.empty() || numUnits() == 0)
        return;

    velocity = shapeVelocity (velocity);

    for (size_t i = 0; i < heldNotes.size(); ++i)
        if (heldNotes[i] == note && heldChannels[i] == midiChannel)
        {
            heldNotes.erase (heldNotes.begin() + (long) i);
            heldChannels.erase (heldChannels.begin() + (long) i);
            heldVelocities.erase (heldVelocities.begin() + (long) i);
            break;
        }
    heldNotes.push_back (note);
    heldChannels.push_back (midiChannel);
    heldVelocities.push_back (velocity);
    for (size_t i = sustainedNotes.size(); i > 0; --i)
        if (sustainedNotes[i - 1] == note && sustainedChannels[i - 1] == midiChannel)
        {
            sustainedNotes.erase (sustainedNotes.begin() + (long) i - 1);
            sustainedChannels.erase (sustainedChannels.begin() + (long) i - 1);
        }

    if (isMono())
    {
        monoNoteOn (note, velocity, midiChannel);
        return;
    }

    if (snap.voiceMode == VoiceMode::poly2)
        for (int u = 0; u < numUnits(); ++u)
            if (unitActive (u) && ! unitGateOn (u))
                for (int j = 0; j < getUnitSize(); ++j)
                    unitVoice (u, j).curtail();

    if (! snap.unison)
    {
        startUnit (pickUnit(), note, velocity, 0.0f, 0, 1, true, midiChannel);
        return;
    }

    // POLY + UNISON: available stacked voices are divided by the number of notes.
    std::vector<int> sounding;
    for (int u = 0; u < numUnits(); ++u)
        if (unitGateOn (u) && std::find (sounding.begin(), sounding.end(), unitNote (u)) == sounding.end())
            sounding.push_back (unitNote (u));

    const int avail = availableUnisonUnits();
    const int perNote = juce::jmax (1, avail / ((int) sounding.size() + 1));

    for (int n : sounding)
    {
        int count = 0;
        for (int u = 0; u < numUnits(); ++u)
            if (unitGateOn (u) && unitNote (u) == n && ++count > perNote)
                releaseUnit (u);
    }

    for (int k = 0; k < perNote; ++k)
        startUnit (pickUnit(), note, velocity, unisonOffset (k), k, perNote, true, midiChannel);
}

void SynthLayer::noteOff (int note, int midiChannel)
{
    bool wasHeld = false;
    for (size_t i = 0; i < heldNotes.size(); ++i)
        if (heldNotes[i] == note && heldChannels[i] == midiChannel)
        {
            heldNotes.erase (heldNotes.begin() + (long) i);
            heldChannels.erase (heldChannels.begin() + (long) i);
            heldVelocities.erase (heldVelocities.begin() + (long) i);
            wasHeld = true;
            break;
        }

    // Note-offs for notes this layer never received (split routing, stray MIDI) are ignored.
    if (! wasHeld)
        return;

    if (pedal || snap.hold)
    {
        bool alreadySustained = false;
        for (size_t i = 0; i < sustainedNotes.size(); ++i)
            alreadySustained |= sustainedNotes[i] == note && sustainedChannels[i] == midiChannel;
        if (! alreadySustained)
        {
            sustainedNotes.push_back (note);
            sustainedChannels.push_back (midiChannel);
        }
        return;
    }

    if (isMono())
    {
        monoNoteOff (note, midiChannel);
        return;
    }

    for (int u = 0; u < numUnits(); ++u)
        if (unitGateOn (u) && unitNote (u) == note && unitVoice (u, 0).getMidiChannel() == midiChannel)
            releaseUnit (u);
}

void SynthLayer::monoNoteOn (int note, float velocity, int midiChannel)
{
    const int n = snap.unison ? availableUnisonUnits() : 1;
    bool sounding = false;
    for (int u = 0; u < monoUnits; ++u)
        sounding = sounding || unitGateOn (u);

    if (sounding && monoUnits == n)
    {
        // SOLO retriggers the envelopes, LEGATO does not.
        const bool retrigger = snap.voiceMode == VoiceMode::solo;
        for (int u = 0; u < n; ++u)
            for (int j = 0; j < getUnitSize(); ++j)
                unitVoice (u, j).changeNote (note, velocity, retrigger, ++(*ageCounter), snap, midiChannel);
        return;
    }

    for (int u = 0; u < numUnits(); ++u)
        if (u >= n && unitActive (u))
            releaseUnit (u);

    for (int u = 0; u < n; ++u)
        startUnit (u, note, velocity, unisonOffset (u), u, n, true, midiChannel);
    monoUnits = n;
}

void SynthLayer::monoNoteOff (int note, int midiChannel)
{
    if (monoUnits == 0)
        return;

    if (! heldNotes.empty())
    {
        if (unitNote (0) == note && unitVoice (0, 0).getMidiChannel() == midiChannel)
        {
            const int back = heldNotes.back();
            const float vel = heldVelocities.back();
            const int channel = heldChannels.back();
            for (int u = 0; u < monoUnits; ++u)
                for (int j = 0; j < getUnitSize(); ++j)
                    unitVoice (u, j).changeNote (back, vel, false, ++(*ageCounter), snap, channel);
        }
        return;
    }

    for (int u = 0; u < monoUnits; ++u)
        releaseUnit (u);
}

void SynthLayer::releaseHeldBySustainEnd()
{
    auto pending = sustainedNotes;
    sustainedNotes.clear();
    for (size_t i = 0; i < pending.size(); ++i)
    {
        const int note = pending[i], channel = sustainedChannels[i];
        bool isHeld = false;
        for (size_t j = 0; j < heldNotes.size(); ++j)
            isHeld |= heldNotes[j] == note && heldChannels[j] == channel;
        if (! isHeld)
        {
            if (isMono())
                monoNoteOff (note, channel);
            else
                for (int u = 0; u < numUnits(); ++u)
                    if (unitGateOn (u) && unitNote (u) == note && unitVoice (u, 0).getMidiChannel() == channel)
                        releaseUnit (u);
        }
    }
    sustainedChannels.clear();
}

void SynthLayer::setSustain (bool on)
{
    const bool was = pedal || snap.hold;
    pedal = on;
    if (was && ! (pedal || snap.hold))
        releaseHeldBySustainEnd();
}

void SynthLayer::polyAftertouch (int note, float value, int midiChannel)
{
    for (auto* v : voices)
        if (v->isActive() && v->getNote() == note && (midiChannel == 0 || v->getMidiChannel() == midiChannel))
            v->setAftertouch (value);
}

void SynthLayer::setMidiExpression (int midiChannel, float bend, float bendSensitivity, float tuning,
                                    float pressure, float timbre, bool mpeMember)
{
    for (auto* v : voices)
        if (v->isActive() && v->getMidiChannel() == midiChannel)
            v->setMidiExpression (bend, bendSensitivity, tuning, pressure, timbre, mpeMember);
}

void SynthLayer::allNotesOff (bool immediate)
{
    heldNotes.clear();
    heldChannels.clear();
    heldVelocities.clear();
    sustainedNotes.clear();
    sustainedChannels.clear();
    monoUnits = 0;
    for (auto* v : voices)
    {
        if (immediate) v->kill();
        else           v->release();
    }
}

int SynthLayer::getActiveVoiceCount() const noexcept
{
    int n = 0;
    for (auto* v : voices)
        n += v->isActive() ? 1 : 0;
    return n;
}
} // namespace nsynth
