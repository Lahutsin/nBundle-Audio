#include "SynthLayer.h"

namespace nsynth
{
namespace
{
// Parameter lookups the audio code needs, resolved once (they build juce::Strings, so they must not
// be first evaluated on the audio thread: SynthLayer::initialiseStaticTables() runs them early).
struct LayerTables
{
    int pan { findLayerSpecIndex (pid::pan) };
    int delayMs { findLayerSpecIndex (pid::delayMs) };
    int lrPhase { findLayerSpecIndex (pid::lfo1LrPhase) };
    juce::NormalisableRange<float> lrPhaseRange { makeRange (*findLayerSpec (pid::lfo1LrPhase)) };
    std::vector<int> layerDestinations;   // continuous parameters applied on the layer output

    LayerTables()
    {
        for (int d = 0; d < (int) kLayerParamCount; ++d)
            if (ModMatrix::isModulatable (d) && ModMatrix::isLayerDestination (d))
                layerDestinations.push_back (d);
    }
};

const LayerTables& tables()
{
    static const LayerTables instance;
    return instance;
}

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
    voices.reserve (kMaxVoices);   // assignVoices() runs on the audio thread (keyboard mode changes)

    // Note bookkeeping runs on the audio thread: reserve for every (note, channel) pair up front,
    // so it never allocates there.
    for (auto* list : { &heldNotes, &heldChannels, &sustainedNotes, &sustainedChannels, &sustainScratch })
        list->reserve (kMaxNoteKeys);
    heldVelocities.reserve (kMaxNoteKeys);
}

float SynthLayer::getModulationOffset (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= (int) kLayerParamCount)
        return 0.0f;
    return uiModOffset[(size_t) parameterIndex].load (std::memory_order_relaxed);
}

void SynthLayer::prepare (double hostSampleRate, int maxBlockSize, VoiceRenderPool& pool, int factor)
{
    hostRate = hostSampleRate;
    maxBlock = maxBlockSize;
    renderPool = &pool;
    jassert (factor == 1 || factor == 2 || factor == 4);
    oversamplingFactor = factor >= 4 ? 4 : factor >= 2 ? 2 : 1;

    // One half-band stage per doubling (none at 1x: the block passes straight through).
    const auto stages = (size_t) (oversamplingFactor == 4 ? 2 : oversamplingFactor == 2 ? 1 : 0);
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, stages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing ((size_t) maxBlockSize);
    oversampler->reset();

    hostScratch.setSize (2, maxBlockSize);
    keyBuffer.setSize (2, maxBlockSize);
    keyBuffer.clear();
    fxDry.setSize (2, maxBlockSize);
    parallelScratch.setSize (2 * (pool.getNumWorkers() + 1), maxBlockSize * oversamplingFactor);
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
    fxTargetsValid = false;
    panGainValid = false;
    asleep = false;
    quietSamples = 0;

    const double osRate = hostRate * oversamplingFactor;
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
    voicesReassigned = true;
    heldNotes.clear();
    heldChannels.clear();
    heldVelocities.clear();
    sustainedNotes.clear();
    sustainedChannels.clear();
    monoUnits = 0;
}

void SynthLayer::readParameters() noexcept
{
    reader.read (candidate);
}

void SynthLayer::commitParameters (const WaveBank* bank) noexcept
{
    sustainBeforeCommit = pedal || snap.hold;
    snap = candidate;
    blockModMatrix = modMatrix.acquireForAudio();
    if (bank != nullptr && (bank != voiceWaveBank || voicesReassigned))
    {
        for (auto* voice : voices)
            voice->setWaveBank (bank);
        voiceWaveBank = bank;
        voicesReassigned = false;
    }
}

void SynthLayer::beginBlock (int numHostSamples, const VoiceContext& context, uint64_t* sharedAgeCounter,
                             const WaveBank* bank)
{
    readParameters();
    commitParameters (bank);
    startBlock (numHostSamples, context, sharedAgeCounter);
}

void SynthLayer::restart() noexcept
{
    allNotesOff (true);
    if (oversampler != nullptr)
        oversampler->reset();
    air.reset();
    for (int m = 0; m < ModMatrixData::kFxModules; ++m)
        resetFxModule ((FxModule) m);
    fxFade.fill (-1.0f);
    fxIdle.fill (true);
    keyBuffer.clear();
    suspendGain = 1.0f;
    asleep = false;
    quietSamples = 0;
    fxTargetsValid = false;
    panGainValid = false;
}

void SynthLayer::startBlock (int numHostSamples, const VoiceContext& context, uint64_t* sharedAgeCounter)
{
    const bool wasSustained = sustainBeforeCommit;
    sustainBeforeCommit = pedal || snap.hold;
    ctx = context;
    ctx.layerPitchOffset += (float) (snap.octave * 12);
    ageCounter = sharedAgeCounter != nullptr ? sharedAgeCounter : &localAge;
    if (blockModMatrix == nullptr)
        blockModMatrix = modMatrix.acquireForAudio();

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

void SynthLayer::finishVoices (int numHostSamples)
{
    if (isSilenced())
    {
        hostScratch.clear (0, numHostSamples);
        keyBuffer.clear (0, numHostSamples);
        return;
    }

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
    const int panIndex = tables().pan;
    if (blockModMatrix == nullptr || ! blockModMatrix->modulated[(size_t) panIndex])
        applyPanGain (snap.pan, numHostSamples);
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

void SynthLayer::processFxModule (FxModule module, juce::AudioBuffer<float>& buffer, int start, int n, const LayerSnapshot& fx,
                                  double delaySeconds, const juce::AudioBuffer<float>* key) noexcept
{
    switch (module)
    {
        case FxModule::distortion:
            distortion.process (buffer, start, n, { fx.distType, fx.distDrive, fx.distTone, fx.distMix });
            break;
        case FxModule::eq:
            eq.process (buffer, start, n, { fx.eqLowFreq, fx.eqLowGain, fx.eqMidFreq, fx.eqMidGain, fx.eqMidQ, fx.eqHighFreq, fx.eqHighGain });
            break;
        case FxModule::compressor:
            compressor.process (buffer, start, n, { fx.compThreshold, fx.compRatio, fx.compAttackMs, fx.compReleaseMs, fx.compMakeup, fx.compMix },
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
            chorus.process (buffer, start, n, settings);
            break;
        }
        case FxModule::phaser:
            phaser.process (buffer, start, n, { fx.phaserRate, fx.phaserDepth, fx.phaserFeedback, fx.phaserMix });
            break;
        case FxModule::flanger:
            flanger.process (buffer, start, n, { fx.flangerRate, fx.flangerDepth, fx.flangerFeedback, fx.flangerMix });
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
            delay.process (buffer, start, n, settings);
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
            reverb.process (buffer, start, n, settings);
            break;
        }
        case FxModule::count:
            break;
    }
}

void SynthLayer::updateSleep (const juce::AudioBuffer<float>& output, int numHostSamples) noexcept
{
    // A layer without voices whose output stayed below -140 dBFS for 250 ms (reverb / delay tails
    // included) goes to sleep: no oversampling, no effects until the next note. Its effects are
    // reset, so it wakes up clean. In SINGLE mode this is the unused layer after its tails.
    const float level = juce::jmax (output.getMagnitude (0, 0, numHostSamples), output.getMagnitude (1, 0, numHostSamples));
    if (level > 1.0e-7f || getActiveVoiceCount() > 0)
    {
        quietSamples = 0;
        return;
    }
    quietSamples += numHostSamples;
    if (quietSamples < (int) (0.25 * hostRate))
        return;
    asleep = true;
    if (oversampler != nullptr)
        oversampler->reset();
    air.reset();
    for (int m = 0; m < ModMatrixData::kFxModules; ++m)
        resetFxModule ((FxModule) m);
    fxFade.fill (-1.0f);
    fxIdle.fill (true);
}

void SynthLayer::applyPanGain (float pan, int numHostSamples) noexcept
{
    // Balance law; ramped over the block, so PAN automation and modulation never step.
    const float gl = juce::jmin (1.0f, 1.0f - pan), gr = juce::jmin (1.0f, 1.0f + pan);
    if (! panGainValid)
    {
        panGain = { gl, gr };
        panGainValid = true;
    }
    hostScratch.applyGainRamp (0, 0, numHostSamples, panGain[0], gl);
    hostScratch.applyGainRamp (1, 0, numHostSamples, panGain[1], gr);
    panGain = { gl, gr };
}

void SynthLayer::initialiseStaticTables()
{
    tables();
    for (int d = 0; d < (int) kLayerParamCount; ++d)
        getLayerParamRange (d);
    LayerSnapshot probe;
    probe.floatField (0);
    Voice::initialiseStaticTables();
}

void SynthLayer::applyEffects (juce::AudioBuffer<float>& dest, int numHostSamples, const juce::AudioBuffer<float>* otherLayerKey)
{
    if (isSilenced())
    {
        for (int ch = 0; ch < 2; ++ch)
            dest.clear (ch, 0, numHostSamples);
        return;
    }

    // Effect parameters with modulation slots: evaluated once per block from the sources of the
    // newest voice (the last ones heard while the tails ring out); macros, mod wheel and
    // expression always follow their current values.
    const int delayMsIndex = tables().delayMs;
    const int panIndex = tables().pan;
    std::array<float, kLayerParamCount> offsets {};
    const bool layerModulation = blockModMatrix != nullptr && blockModMatrix->anyLayerDestination;
    if (layerModulation)
    {
        auto sources = layerSources;
        for (int m = 0; m < ModMatrixData::kMacros; ++m)
            sources[(size_t) ModSource::macro1 + (size_t) m] = snap.macros[(size_t) m];
        sources[(size_t) ModSource::modWheel] = juce::jlimit (0.0f, 1.0f, ctx.modLever);
        sources[(size_t) ModSource::expression] = juce::jlimit (0.0f, 1.0f, ctx.expression);

        const auto& matrix = *blockModMatrix;
        for (int k = 0; k < matrix.activeSlotCount; ++k)
        {
            const int s = matrix.activeSlots[(size_t) k];
            if (! matrix.slotOnLayer[(size_t) s])
                continue;
            const auto& slot = matrix.slots[(size_t) s];
            offsets[(size_t) slot.destination] += slot.shape (sources[(size_t) slot.source]) * slot.amount;
        }
    }

    // Targets of this block (normalised). Values that moved since the last block (automation,
    // modulation) glide there in kFxControlBlock-sample steps instead of jumping at the block start.
    LayerSnapshot fx = snap;
    std::array<int, kLayerParamCount> moving {};
    int movingCount = 0;
    for (const int d : tables().layerDestinations)
    {
        const bool modulated = layerModulation && blockModMatrix->modulated[(size_t) d];
        const auto& range = getLayerParamRange (d);
        const float target = juce::jlimit (0.0f, 1.0f, range.convertTo0to1 (*snap.floatField (d)) + offsets[(size_t) d]);
        if (modulated)
            uiModOffset[(size_t) d].store (offsets[(size_t) d], std::memory_order_relaxed);
        if (! fxTargetsValid)
            fxPrevious[(size_t) d] = target;
        if (std::abs (target - fxPrevious[(size_t) d]) > 1.0e-6f)
            moving[(size_t) movingCount++] = d;
        else if (modulated)
            *fx.floatField (d) = range.convertFrom0to1 (target);
        fxTarget[(size_t) d] = target;
    }
    fxTargetsValid = true;
    const float delayTimeOffset = offsets[(size_t) delayMsIndex];

    if (layerModulation && blockModMatrix->modulated[(size_t) panIndex])
        applyPanGain (getLayerParamRange (panIndex).convertFrom0to1 (fxTarget[(size_t) panIndex]), numHostSamples);

    // The modules in the patch's order. Switching one on or off crossfades over 10 ms; an
    // inactive module is reset once and then skipped. The chorus fades its MODE itself.
    const float fadeStep = (float) (1.0 / (0.01 * hostRate));
    const auto& order = blockModMatrix != nullptr ? blockModMatrix->fxOrder : ModMatrixData::defaultFxOrder();
    const int chunk = movingCount > 0 ? kFxControlBlock : numHostSamples;
    for (int start = 0; start < numHostSamples; start += chunk)
    {
        const int num = juce::jmin (chunk, numHostSamples - start);
        if (movingCount > 0)
        {
            const float position = (float) (start + num) / (float) numHostSamples;
            for (int k = 0; k < movingCount; ++k)
            {
                const int d = moving[(size_t) k];
                const float value = fxPrevious[(size_t) d] + (fxTarget[(size_t) d] - fxPrevious[(size_t) d]) * position;
                *fx.floatField (d) = getLayerParamRange (d).convertFrom0to1 (value);
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

        for (const auto module : order)
        {
            const auto index = (size_t) module;
            if (index >= fxFade.size())
                continue;
            const bool on = fxModuleEnabled (module, fx);
            if (module == FxModule::chorus)
            {
                processFxModule (module, hostScratch, start, num, fx, delaySeconds, otherLayerKey);
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
                processFxModule (module, hostScratch, start, num, fx, delaySeconds, otherLayerKey);
                continue;
            }
            for (int ch = 0; ch < 2; ++ch)
                fxDry.copyFrom (ch, start, hostScratch, ch, start, num);
            processFxModule (module, hostScratch, start, num, fx, delaySeconds, otherLayerKey);
            for (int i = start; i < start + num; ++i)
            {
                fade = juce::jlimit (0.0f, 1.0f, fade + (on ? fadeStep : -fadeStep));
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float dry = fxDry.getSample (ch, i);
                    hostScratch.setSample (ch, i, dry + fade * (hostScratch.getSample (ch, i) - dry));
                }
            }
        }
    }
    for (int k = 0; k < movingCount; ++k)
        fxPrevious[(size_t) moving[(size_t) k]] = fxTarget[(size_t) moving[(size_t) k]];
    uiCompReduction.store (fx.compOn ? compressor.getGainReductionDb() : 0.0f, std::memory_order_relaxed);

    for (int ch = 0; ch < 2; ++ch)
        dest.copyFrom (ch, 0, hostScratch, ch, 0, numHostSamples);
    updateSleep (dest, numHostSamples);

    // A preset is being loaded: fade the old sound out over 10 ms, then stay silent until restart().
    if (suspended)
    {
        const float step = (float) (1.0 / (0.01 * hostRate));
        float* left = dest.getWritePointer (0);
        float* right = dest.getWritePointer (1);
        for (int i = 0; i < numHostSamples; ++i)
        {
            suspendGain = juce::jmax (0.0f, suspendGain - step);
            left[i] *= suspendGain;
            right[i] *= suspendGain;
        }
    }
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

int SynthLayer::findUnitPlaying (int note, int midiChannel) const noexcept
{
    // The newest unit still sounding this note on this channel (held or releasing).
    int found = -1;
    uint64_t newest = 0;
    for (int u = 0; u < numUnits(); ++u)
        if (unitActive (u) && unitNote (u) == note && voices[(size_t) (u * getUnitSize())]->getMidiChannel() == midiChannel
            && (found < 0 || unitAge (u) > newest))
        {
            found = u;
            newest = unitAge (u);
        }
    return found;
}

int SynthLayer::pickUnit() noexcept
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
    const auto& lrPhaseRange = tables().lrPhaseRange;
    if (blockModMatrix == nullptr)
        blockModMatrix = modMatrix.acquireForAudio();
    const auto* matrixData = blockModMatrix;
    const int lrPhaseIndex = tables().lrPhase;
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
    asleep = false;   // render from this note on, even in the middle of the block
    quietSamples = 0;

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
        // Repeating a note retriggers the unit that still plays it (from its current level, see
        // Voice::start) instead of stacking another copy that phases against the releasing one.
        const int playing = findUnitPlaying (note, midiChannel);
        startUnit (playing >= 0 ? playing : pickUnit(), note, velocity, 0.0f, 0, 1, true, midiChannel);
        return;
    }

    // POLY + UNISON: available stacked voices are divided by the number of notes.
    std::array<int, kMaxVoices> sounding;
    int soundingCount = 0;
    for (int u = 0; u < numUnits(); ++u)
        if (unitGateOn (u) && std::find (sounding.begin(), sounding.begin() + soundingCount, unitNote (u)) == sounding.begin() + soundingCount)
            sounding[(size_t) soundingCount++] = unitNote (u);

    const int avail = availableUnisonUnits();
    const int perNote = juce::jmax (1, avail / (soundingCount + 1));

    for (int s = 0; s < soundingCount; ++s)
    {
        const int n = sounding[(size_t) s];
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
    auto& pending = sustainScratch;
    pending.assign (sustainedNotes.begin(), sustainedNotes.end());   // within the reserved capacity
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
                                    float pressure, float timbre, bool mpeMember, float zoneBend,
                                    float zoneBendSensitivity)
{
    for (auto* v : voices)
        if (v->isActive() && v->getMidiChannel() == midiChannel)
            v->setMidiExpression (bend, bendSensitivity, tuning, pressure, timbre, mpeMember, zoneBend, zoneBendSensitivity);
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
