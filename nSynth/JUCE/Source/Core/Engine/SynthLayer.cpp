#include "SynthLayer.h"

namespace nsynth
{
SynthLayer::SynthLayer (juce::AudioProcessorValueTreeState& state, Layer layerId)
    : id (layerId), reader (state, layerId), rng (layerId == Layer::upper ? 11 : 23)
{
}

void SynthLayer::prepare (double hostSampleRate, int maxBlockSize, VoiceRenderPool& pool)
{
    hostRate = hostSampleRate;
    maxBlock = maxBlockSize;
    renderPool = &pool;

    for (int q = 0; q < 3; ++q)
    {
        oversamplers[(size_t) q] = std::make_unique<juce::dsp::Oversampling<float>> (
            2, (size_t) (q + 1), juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversamplers[(size_t) q]->initProcessing ((size_t) maxBlockSize);
    }

    hostScratch.setSize (2, maxBlockSize);
    parallelScratch.setSize (2 * (pool.getNumWorkers() + 1), maxBlockSize * 8);
    chorus.prepare (hostSampleRate, maxBlockSize);
    delay.prepare (hostSampleRate, maxBlockSize);
    setQuality (quality);
}

void SynthLayer::setQuality (Quality q)
{
    quality = q;
    const double osRate = hostRate * getOversamplingFactor();
    for (auto* v : voices)
    {
        v->kill();
        v->setSampleRate (osRate);
    }
    if (auto& os = oversamplers[(size_t) toInt (q)])
        os->reset();
}

float SynthLayer::getLatencySamples() const noexcept
{
    auto& os = oversamplers[(size_t) toInt (quality)];
    return os != nullptr ? os->getLatencyInSamples() : 0.0f;
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
    sequenceUnitCount = 0;
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

    auto& os = *oversamplers[(size_t) toInt (quality)];
    hostScratch.clear (0, numHostSamples);
    juce::dsp::AudioBlock<float> hostBlock (hostScratch);
    osBlock = os.processSamplesUp (hostBlock.getSubBlock (0, (size_t) numHostSamples));
    osBlock.clear();

    lfo2MasterPhase += rates::lowFrequencyHz (snap.lfo2Rate) * numHostSamples / hostRate;
    lfo2MasterPhase -= std::floor (lfo2MasterPhase);
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
    auto& os = *oversamplers[(size_t) toInt (quality)];
    juce::dsp::AudioBlock<float> hostBlock (hostScratch);
    auto outBlock = hostBlock.getSubBlock (0, (size_t) numHostSamples);
    os.processSamplesDown (outBlock);

    // Layer PAN as a stereo balance control.
    const float gl = juce::jmin (1.0f, 1.0f - snap.pan), gr = juce::jmin (1.0f, 1.0f + snap.pan);
    hostScratch.applyGain (0, 0, numHostSamples, gl);
    hostScratch.applyGain (1, 0, numHostSamples, gr);

    // Chorus -> delay, in series.
    chorus.process (hostScratch, 0, numHostSamples, snap.chorus);
    double delaySeconds = StereoDelay::faderToSeconds (snap.delayTime);
    const std::array<float, ModMatrixData::sourceCount>* sources = nullptr;
    for (auto* voice : voices)
        if (voice->isActive())
            sources = &voice->getLastModSources();

    float delayTimeFader = snap.delayTime;
    float delaySend = snap.delaySend;
    float delayFeedback = snap.delayFeedback;
    if (sources != nullptr && blockModMatrix != nullptr)
    {
        for (int source = 0; source < ModMatrixData::sourceCount; ++source)
        {
            delayTimeFader += (*sources)[(size_t) source]
                            * blockModMatrix->fixed[(size_t) source][(size_t) ModTarget::delayTime];
            delaySend += (*sources)[(size_t) source]
                       * blockModMatrix->direct[(size_t) source][(size_t) DirectModTarget::delaySend];
            delayFeedback += (*sources)[(size_t) source]
                           * blockModMatrix->direct[(size_t) source][(size_t) DirectModTarget::delayFeedback];
        }
    }
    delayTimeFader = juce::jlimit (0.0f, 1.0f, delayTimeFader);
    delaySend = juce::jlimit (0.0f, 1.0f, delaySend);
    delayFeedback = juce::jlimit (0.0f, 1.0f, delayFeedback);
    if (snap.clockSync)
    {
        const auto& beats = rates::delaySyncBeats();
        delaySeconds = beats[(size_t) rates::divisionIndex (delayTimeFader, (int) beats.size())] * 60.0 / ctx.bpm;
    }
    else
        delaySeconds = StereoDelay::faderToSeconds (delayTimeFader);
    delay.process (hostScratch, 0, numHostSamples, delaySeconds, delaySend, delayFeedback, snap.delayFreeze);

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
    float lrPhase = lrPhaseRange.convertTo0to1 (snap.lfo1LrPhase);
    std::array<float, ModMatrixData::sourceCount> sources {
        unitVoice (unit, 0).getLastModSources()[(size_t) ModSource::dds2],
        juce::jlimit (0.0f, 1.0f, (float) (0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * lfo2MasterPhase))),
        0.0f,
        juce::jlimit (0.0f, 1.0f, velocity),
        0.0f,
        juce::jlimit (0.0f, 1.0f, ctx.expression),
        juce::jlimit (0.0f, 1.0f, 0.5f + ctx.ribbonSemitones / 24.0f),
        juce::jlimit (0.0f, 1.0f, (float) note / 127.0f),
        0.5f,
        0.0f
    };
    for (int source = 0; source < ModMatrixData::sourceCount; ++source)
        lrPhase += sources[(size_t) source] * matrixData->direct[(size_t) source][(size_t) DirectModTarget::lfo1LrPhase];
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

    if (snap.binaural)
    {
        auto& left = unitVoice (unit, 0);
        info.side = Voice::Side::left;
        info.lfo1Phase = lfo1Restarts ? 0.0 : -1.0;
        left.start (info, snap);

        // Right channel: LR PHASE offsets LFO1.
        info.side = Voice::Side::right;
        info.lfo1Phase = (lfo1Restarts ? 0.0 : left.getLfo1Phase()) + spread;
        unitVoice (unit, 1).start (info, snap);
        return;
    }

    info.side = Voice::Side::mono;
    if (isMono())
    {
        // SOLO/LEGATO without binaural: no panning, SPREAD offsets the LFO1 phases.
        info.pan = 0.0f;
        const double offset = spread * (double) stackIndex / (double) juce::jmax (1, stackSize);
        if (lfo1Restarts)
            info.lfo1Phase = offset;
        else
            info.lfo1Phase = stackIndex == 0 ? -1.0 : unitVoice (0, 0).getLfo1Phase() + offset;
    }
    else
    {
        // POLY without binaural: voices alternately panned by SPREAD.
        info.pan = (unit % 2 == 0 ? -spread : spread);
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
void SynthLayer::noteOn (int note, float velocity, int midiChannel)
{
    if (voices.empty() || numUnits() == 0)
        return;

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

void SynthLayer::releaseSequenceNotes() noexcept
{
    for (int i = 0; i < sequenceUnitCount; ++i)
    {
        const int unit = sequenceUnits[(size_t) i];
        if (unit >= 0 && unit < numUnits() && unitActive (unit) && unitGateOn (unit)
            && unitNote (unit) == sequenceNotes[(size_t) i])
            releaseUnit (unit);
    }
    sequenceUnitCount = 0;
}

void SynthLayer::playSequenceStep (const SequenceStep& step, int transpose, bool accent)
{
    if (step.rest || step.noteCount == 0 || voices.empty() || numUnits() == 0)
    {
        releaseSequenceNotes();
        return;
    }

    const int notesToPlay = juce::jmin ((int) step.noteCount, SequenceStep::kMaxNotes);
    const float velocity = accent ? 1.0f : 0.78f;
    int preserved = 0;
    if (step.slide)
    {
        preserved = juce::jmin (sequenceUnitCount, notesToPlay);
        for (int i = 0; i < preserved; ++i)
        {
            const int unit = sequenceUnits[(size_t) i];
            const int note = juce::jlimit (0, 127, 60 + (int) step.notes[(size_t) i] + transpose);
            if (unit >= 0 && unit < numUnits() && unitGateOn (unit)
                && unitNote (unit) == sequenceNotes[(size_t) i])
            {
                for (int side = 0; side < getUnitSize(); ++side)
                    unitVoice (unit, side).changeNote (note, velocity, false, ++(*ageCounter), snap);
                sequenceNotes[(size_t) i] = note;
            }
            else
            {
                sequenceUnits[(size_t) i] = -1;
                preserved = i;
                break;
            }
        }
    }

    for (int i = preserved; i < sequenceUnitCount; ++i)
    {
        const int unit = sequenceUnits[(size_t) i];
        if (unit >= 0 && unit < numUnits() && unitActive (unit) && unitGateOn (unit)
            && unitNote (unit) == sequenceNotes[(size_t) i])
            releaseUnit (unit);
    }
    sequenceUnitCount = preserved;

    for (int i = preserved; i < notesToPlay; ++i)
    {
        const int unit = pickUnit();
        const int note = juce::jlimit (0, 127, 60 + (int) step.notes[(size_t) i] + transpose);
        startUnit (unit, note, velocity, 0.0f, 0, 1, true);
        sequenceUnits[(size_t) i] = unit;
        sequenceNotes[(size_t) i] = note;
        ++sequenceUnitCount;
    }
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
    sequenceUnitCount = 0;
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
