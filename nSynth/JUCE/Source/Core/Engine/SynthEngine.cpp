#include "SynthEngine.h"

namespace nsynth
{
namespace
{
class WaveImportJob final : public juce::ThreadPoolJob
{
public:
    explicit WaveImportJob (std::function<void()> task)
        : juce::ThreadPoolJob ("nSynth user wavetable import"), work (std::move (task)) {}

    JobStatus runJob() override
    {
        work();
        return jobHasFinished;
    }

private:
    std::function<void()> work;
};
} // namespace

SynthEngine::SynthEngine (juce::AudioProcessorValueTreeState& state)
    : perfReader (state),
      upper (state, Layer::upper),
      lower (state, Layer::lower)
{
    SynthLayer::initialiseStaticTables();
    for (auto& bank : waveBanks)
        bank.publish (WaveBank::factory());
    for (auto& mapping : learnedCCBinding)
        mapping.store (-1);
    for (size_t channel = 0; channel < 16; ++channel)
    {
        bendSensitivity[channel].store (2.0f);
        channelFineTune[channel].store (0.0f);
        channelCoarseTune[channel].store (0.0f);
    }

    auto addBinding = [&] (const juce::String& id, const ParamSpec& spec, int layerIndex)
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (id));
        jassert (parameter != nullptr);
        parameterBindings.push_back ({ id, parameter, &spec, layerIndex });
    };
    for (auto layer : { Layer::upper, Layer::lower })
        for (const auto& spec : getLayerParamSpecs())
            addBinding (layerParamId (layer, spec.suffix), spec, toInt (layer));
    for (const auto& spec : getPerformanceParamSpecs())
        addBinding (perfParamId (spec.suffix), spec, -1);
}

void SynthEngine::prepare (double sampleRate, int maxBlockSize, int requestedRenderWorkers)
{
    renderPool.prepare (requestedRenderWorkers, maxBlockSize, sampleRate);
    perfReader.read (perf);

    for (int i = 0; i < kNumVoices; ++i)
        voices[(size_t) i].prepare (sampleRate, WaveBank::factory().get(), (uint32_t) (1000 + 7919 * i));
    audioWaveBanks.fill (nullptr);

    for (auto* l : { &upper, &lower })
        l->prepare (sampleRate, maxBlockSize, renderPool);

    assignVoices (perf.keyboardMode, perf.singleLayer);
    lastMasterGain = -1.0f;
    upperBuffer.setSize (2, maxBlockSize);
    lowerBuffer.setSize (2, maxBlockSize);
    limiter.prepare (sampleRate);
    prepared = true;
}

void SynthEngine::assignVoices (KeyboardMode mode, Layer singleLayer)
{
    currentMode = mode;
    currentSingleLayer = singleLayer;

    if (mode == KeyboardMode::single)
    {
        // SINGLE: one layer gets the whole pool of voices.
        auto& active = layer (singleLayer);
        auto& inactive = layer (singleLayer == Layer::upper ? Layer::lower : Layer::upper);
        inactive.assignVoices (voices.data(), 0);
        active.assignVoices (voices.data(), kNumVoices);
        return;
    }

    // DUAL / SPLIT: voices split evenly between the layers.
    upper.assignVoices (voices.data(), kNumVoices / 2);
    lower.assignVoices (voices.data() + kNumVoices / 2, kNumVoices / 2);
}

void SynthEngine::appendModMatrixState (juce::ValueTree& state) const
{
    for (int i = state.getNumChildren() - 1; i >= 0; --i)
        if (state.getChild (i).hasType ("MOD_MATRIX"))
            state.removeChild (i, nullptr);
    state.addChild (upper.getModMatrix().toValueTree (toInt (Layer::upper)), -1, nullptr);
    state.addChild (lower.getModMatrix().toValueTree (toInt (Layer::lower)), -1, nullptr);
}

bool SynthEngine::restoreModMatrixState (const juce::ValueTree& state, juce::String* error)
{
    std::array<ModMatrixData, 2> staged;
    if (! stageModMatrixState (state, staged, error))
        return false;
    applyModMatrixState (staged);
    return true;
}

void SynthEngine::applyModMatrixState (const std::array<ModMatrixData, 2>& staged)
{
    for (auto layerId : { Layer::upper, Layer::lower })
        modMatrix (layerId).restoreSnapshot (staged[(size_t) toInt (layerId)]);
}

bool SynthEngine::stageModMatrixState (const juce::ValueTree& state, std::array<ModMatrixData, 2>& result, juce::String* error)
{
    std::array<ModMatrix, 2> staged;
    std::array<bool, 2> found {};
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        if (! child.hasType ("MOD_MATRIX"))
            continue;
        const int layerIndex = (int) child.getProperty ("layer", -1);
        if (layerIndex < 0 || layerIndex >= 2 || found[(size_t) layerIndex])
        {
            if (error) *error = "State contains an invalid or duplicate layer modulation matrix.";
            return false;
        }
        if (! staged[(size_t) layerIndex].restoreFromValueTree (child, error))
            return false;
        found[(size_t) layerIndex] = true;
    }

    for (size_t i = 0; i < 2; ++i)
        result[i] = *staged[i].snapshot();
    return true;
}

void SynthEngine::process (juce::AudioBuffer<float>& mix, juce::AudioBuffer<float>* upperOut, juce::AudioBuffer<float>* lowerOut,
                           const juce::MidiBuffer& midi, double hostBpm)
{
    jassert (prepared);
    const int n = mix.getNumSamples();
    beginBlockState();
    if (midiStateRefresh.exchange (false, std::memory_order_acq_rel))
        for (int channel = 1; channel <= 16; ++channel)
            updateChannelExpression (channel);

    if (perf.keyboardMode != currentMode || (perf.keyboardMode == KeyboardMode::single && perf.singleLayer != currentSingleLayer))
        assignVoices (perf.keyboardMode, perf.singleLayer);

    VoiceContext ctx;
    ctx.bpm = hostBpm > 0.0 ? hostBpm : (double) perf.tempoParam;
    ctx.ribbonSemitones = 12.0f * (std::abs (perf.ribbon) > 1.0e-4f ? perf.ribbon : midiRibbon);
    // MOD LEVER / EXPRESSION: the panel control and the MIDI controller share one value, the one
    // moved last wins (a parked panel knob does not mask the wheel below its position).
    if (! juce::exactlyEqual (perf.modLever, lastPanelModLever))
    {
        lastPanelModLever = perf.modLever;
        midiModLever = perf.modLever;
    }
    if (! juce::exactlyEqual (perf.expression, lastPanelExpression))
    {
        lastPanelExpression = perf.expression;
        midiExpression = perf.expression;
    }
    ctx.modLever = midiModLever;
    ctx.expression = midiExpression;
    ctx.layerPitchOffset = (float) perf.transpose + perf.perfDetune + perf.fineCents / 100.0f;

    VoiceContext lowerCtx = ctx;
    lowerCtx.layerPitchOffset += perf.lowerDetune; // LOWER LAYER DETUNE

    upper.startBlock (n, ctx, &ageCounter);
    lower.startBlock (n, lowerCtx, &ageCounter);

    // Notes start sample-accurately. Controllers (dense MPE / CC streams) are applied on a
    // kControlGrid-sample grid, so they never split a block into dozens of tiny render passes.
    int pos = 0;
    for (const auto meta : midi)
    {
        const auto message = meta.getMessage();
        int t = juce::jlimit (0, n, meta.samplePosition);
        if (! message.isNoteOnOrOff())
            t = juce::jmax (pos, t - t % kControlGrid);
        renderSegmentsCombined (pos, t - pos);
        pos = t;
        handleMidi (message);
    }
    renderSegmentsCombined (pos, n - pos);

    // Both dry layer signals first: either compressor may key on the other layer.
    upper.finishVoices (n);
    lower.finishVoices (n);
    upper.applyEffects (upperBuffer, n, &lower.getKeyBuffer());
    lower.applyEffects (lowerBuffer, n, &upper.getKeyBuffer());

    // MASTER VOLUME ramps across the block: automation never steps.
    const float master = juce::Decibels::decibelsToGain (perf.masterDb, -60.0f);
    const float startMaster = lastMasterGain < 0.0f ? master : lastMasterGain;
    lastMasterGain = master;
    for (int ch = 0; ch < juce::jmin (2, mix.getNumChannels()); ++ch)
    {
        mix.clear (ch, 0, n);
        mix.addFromWithRamp (ch, 0, upperBuffer.getReadPointer (ch), n, startMaster, master);
        mix.addFromWithRamp (ch, 0, lowerBuffer.getReadPointer (ch), n, startMaster, master);
    }
    if (mix.getNumChannels() >= 2)
        limiter.process (mix.getWritePointer (0), mix.getWritePointer (1), n);
    for (int ch = 2; ch < mix.getNumChannels(); ++ch)
        mix.clear (ch, 0, n);

    auto copyLayer = [&] (juce::AudioBuffer<float>* dest, const juce::AudioBuffer<float>& src)
    {
        if (dest == nullptr)
            return;
        for (int ch = 0; ch < juce::jmin (2, dest->getNumChannels()); ++ch)
        {
            dest->clear (ch, 0, n);
            dest->addFromWithRamp (ch, 0, src.getReadPointer (ch), n, startMaster, master);
        }
    };
    copyLayer (upperOut, upperBuffer);
    copyLayer (lowerOut, lowerBuffer);
}

void SynthEngine::beginStateChange (int scopeMask) noexcept
{
    for (int i = 0; i < 3; ++i)
        if ((scopeMask & (1 << i)) != 0)
            stateChangeDepth[(size_t) i].fetch_add (1, std::memory_order_seq_cst);
    stateEpoch.fetch_add (1, std::memory_order_seq_cst);
}

void SynthEngine::endStateChange (int scopeMask) noexcept
{
    stateEpoch.fetch_add (1, std::memory_order_seq_cst);
    for (int i = 0; i < 3; ++i)
        if ((scopeMask & (1 << i)) != 0)
        {
            stateGeneration[(size_t) i].fetch_add (1, std::memory_order_seq_cst);
            stateChangeDepth[(size_t) i].fetch_sub (1, std::memory_order_seq_cst);
        }
}

void SynthEngine::beginBlockState() noexcept
{
    // Seqlock-style read: parameters read while a preset load was in progress (the epoch moved)
    // are discarded and the block runs with the previous ones.
    const auto epoch = stateEpoch.load (std::memory_order_seq_cst);
    std::array<bool, 3> frozen {};
    for (size_t i = 0; i < 3; ++i)
        frozen[i] = stateChangeDepth[i].load (std::memory_order_seq_cst) > 0;

    if (! frozen[2])
        perfReader.read (perfCandidate);
    for (auto* l : { &upper, &lower })
        if (! frozen[(size_t) toInt (l->getId())])
            l->readParameters();
    const bool valid = stateEpoch.load (std::memory_order_seq_cst) == epoch;

    if (valid && ! frozen[2])
        perf = perfCandidate;
    for (auto* l : { &upper, &lower })
    {
        const auto index = (size_t) toInt (l->getId());
        l->setSuspended (frozen[index] || frozen[2]);
        if (! valid || frozen[index])
            continue;
        const auto* bank = waveBanks[index].acquire();
        if (bank != audioWaveBanks[index])
        {
            audioWaveBanks[index] = bank;
            garbageNotice.store (true, std::memory_order_release);
        }
        l->commitParameters (bank);
    }

    if (! valid)
        return;
    for (size_t i = 0; i < 3; ++i)
    {
        const auto generation = stateGeneration[i].load (std::memory_order_seq_cst);
        if (frozen[i] || generation == seenStateGeneration[i])
            continue;
        seenStateGeneration[i] = generation;
        if (i == 2)
        {
            upper.restart();
            lower.restart();
            midiStateRefresh.store (true, std::memory_order_release);
        }
        else
        {
            layer (i == 0 ? Layer::upper : Layer::lower).restart();
        }
    }
}

namespace
{
struct CombinedVoiceTask
{
    struct Item
    {
        SynthLayer* layer { nullptr };
        Voice* voice { nullptr };
        int startSample { 0 };
        int numSamples { 0 };
    };
    std::array<Item, SynthEngine::kNumVoices> items {};
    int itemCount { 0 };
};
} // namespace

void SynthEngine::renderCombinedVoiceItem (void* rawContext, int itemIndex, int lane) noexcept
{
    auto& task = *static_cast<CombinedVoiceTask*> (rawContext);
    auto& item = task.items[(size_t) itemIndex];
    auto& layer = *item.layer;
    float* left = layer.parallelScratch.getWritePointer (lane * 2);
    float* right = layer.parallelScratch.getWritePointer (lane * 2 + 1);
    item.voice->render (left, right, item.startSample, item.numSamples,
                        layer.snap, layer.ctx, *layer.blockModMatrix);
}

void SynthEngine::renderSegmentsCombined (int hostStart, int hostNum) noexcept
{
    if (hostNum <= 0)
        return;

    CombinedVoiceTask task;
    std::array<int, 2> layerVoiceCounts {};

    for (auto* layerPtr : { &upper, &lower })
    {
        const int idx = toInt (layerPtr->getId());
        if (layerPtr->isSilenced())
            continue;
        for (auto* voice : layerPtr->voices)
            if (voice->isActive())
            {
                task.items[(size_t) task.itemCount++] = { layerPtr, voice, hostStart, hostNum };
                ++layerVoiceCounts[(size_t) idx];
            }
    }

    if (task.itemCount == 0)
        return;

    // Waking the workers costs a few microseconds: short segments render on the audio thread.
    const int availableWorkers = hostNum >= kMinParallelSamples ? renderPool.getNumWorkers() : 0;
    const int laneCount = juce::jmin (availableWorkers + 1, task.itemCount);
    if (laneCount < 2 || task.itemCount < 2)
    {
        for (int i = 0; i < task.itemCount; ++i)
        {
            auto& item = task.items[(size_t) i];
            auto& layer = *item.layer;
            float* left = layer.hostScratch.getWritePointer (0);
            float* right = layer.hostScratch.getWritePointer (1);
            item.voice->render (left, right, item.startSample, item.numSamples,
                                layer.snap, layer.ctx, *layer.blockModMatrix);
        }
        return;
    }

    for (auto* layerPtr : { &upper, &lower })
    {
        const int idx = toInt (layerPtr->getId());
        if (layerVoiceCounts[(size_t) idx] == 0)
            continue;
        for (int lane = 0; lane < laneCount; ++lane)
        {
            layerPtr->parallelScratch.clear (lane * 2, hostStart, hostNum);
            layerPtr->parallelScratch.clear (lane * 2 + 1, hostStart, hostNum);
        }
    }

    renderPool.renderItems (task.itemCount, laneCount - 1, &task, &SynthEngine::renderCombinedVoiceItem);

    for (auto* layerPtr : { &upper, &lower })
    {
        const int idx = toInt (layerPtr->getId());
        if (layerVoiceCounts[(size_t) idx] == 0)
            continue;
        float* destL = layerPtr->hostScratch.getWritePointer (0) + hostStart;
        float* destR = layerPtr->hostScratch.getWritePointer (1) + hostStart;
        for (int lane = 0; lane < laneCount; ++lane)
        {
            juce::FloatVectorOperations::add (destL, layerPtr->parallelScratch.getReadPointer (lane * 2) + hostStart, hostNum);
            juce::FloatVectorOperations::add (destR, layerPtr->parallelScratch.getReadPointer (lane * 2 + 1) + hostStart, hostNum);
        }
    }
}

void SynthEngine::appendWaveBankState (juce::ValueTree& state) const
{
    for (int i = state.getNumChildren() - 1; i >= 0; --i)
        if (state.getChild (i).hasType ("USER_WAVES"))
            state.removeChild (i, nullptr);
    for (auto l : { Layer::upper, Layer::lower })
    {
        const auto bank = getWaveBank (l);
        if (bank->hasFactoryContent())
            continue;
        auto waves = bank->toValueTree();
        waves.setProperty ("layer", toInt (l), nullptr);
        state.addChild (waves, -1, nullptr);
    }
}

void SynthEngine::appendLayerWaveBankState (juce::ValueTree& patch, Layer l) const
{
    for (int i = patch.getNumChildren() - 1; i >= 0; --i)
        if (patch.getChild (i).hasType ("USER_WAVES"))
            patch.removeChild (i, nullptr);
    const auto bank = getWaveBank (l);
    if (! bank->hasFactoryContent())
        patch.addChild (bank->toValueTree(), -1, nullptr);
}

WaveBankPtr SynthEngine::buildWaveBank (const juce::ValueTree& userWaves, juce::String* error)
{
    if (! userWaves.isValid())
        return WaveBank::factory();
    if (! WaveBank::isValidValueTree (userWaves, error))
        return nullptr;

    // Building the mip tables takes tens of milliseconds: reuse the banks loaded recently
    // (auditioning presets back and forth, compare, both layers of a performance).
    auto payloadHash = [] (const juce::ValueTree& waves)
    {
        const auto* payload = waves.getProperty ("cycles").getBinaryData();
        const auto* bytes = static_cast<const uint8_t*> (payload->getData());
        uint64_t hash = 14695981039346656037ull; // FNV-1a
        for (size_t i = 0; i < payload->getSize(); ++i)
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        return hash;
    };
    static const uint64_t factoryKey = payloadHash (WaveBank::factory()->toValueTree());
    const auto key = payloadHash (userWaves);
    if (key == factoryKey)
        return WaveBank::factory();

    {
        const std::lock_guard<std::mutex> guard (waveCacheMutex);
        for (auto it = waveCache.begin(); it != waveCache.end(); ++it)
            if (it->first == key)
            {
                auto entry = *it;
                waveCache.erase (it);
                waveCache.insert (waveCache.begin(), entry);
                return entry.second;
            }
    }

    auto bank = std::make_shared<WaveBank> (*WaveBank::factory());
    if (! bank->restoreFromValueTree (userWaves, error))
        return nullptr;
    WaveBankPtr result = bank->hasFactoryContent() ? WaveBank::factory() : WaveBankPtr (std::move (bank));
    const std::lock_guard<std::mutex> guard (waveCacheMutex);
    waveCache.insert (waveCache.begin(), { key, result });
    if (waveCache.size() > 4)
        waveCache.pop_back();
    return result;
}

void SynthEngine::setWaveBank (Layer l, WaveBankPtr bank)
{
    if (bank == nullptr)
        bank = WaveBank::factory();
    auto& handoff = waveBanks[(size_t) toInt (l)];
    if (handoff.latest() != bank)
        handoff.publish (std::move (bank));
}

bool SynthEngine::stageWaveBankState (const juce::ValueTree& state, std::array<WaveBankPtr, 2>& staged, juce::String* error)
{
    // Sessions before per-layer banks carry one USER_WAVES child without a layer: it applies to both.
    std::array<juce::ValueTree, 2> trees;
    juce::ValueTree shared;
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        if (! child.hasType ("USER_WAVES"))
            continue;
        const int layerIndex = (int) child.getProperty ("layer", -1);
        auto& slot = layerIndex < 0 ? shared : trees[(size_t) juce::jlimit (0, 1, layerIndex)];
        if (layerIndex > 1 || slot.isValid() || (layerIndex < 0 && (trees[0].isValid() || trees[1].isValid()))
            || (layerIndex >= 0 && shared.isValid()))
        {
            if (error) *error = "State contains invalid or duplicate user wavetable banks.";
            return false;
        }
        slot = child;
    }
    if (shared.isValid())
        trees = { shared, shared };

    for (size_t l = 0; l < 2; ++l)
    {
        staged[l] = l == 1 && trees[1] == trees[0] ? staged[0] : buildWaveBank (trees[l], error);
        if (staged[l] == nullptr)
            return false;
    }
    return true;
}

bool SynthEngine::restoreWaveBankState (const juce::ValueTree& state, juce::String* error)
{
    std::array<WaveBankPtr, 2> staged;
    if (! stageWaveBankState (state, staged, error))
        return false;
    setWaveBank (Layer::upper, staged[0]);
    setWaveBank (Layer::lower, staged[1]);
    return true;
}

void SynthEngine::collectGarbage()
{
    for (auto& bank : waveBanks)
        bank.collect();
    for (auto l : { Layer::upper, Layer::lower })
        modMatrix (l).collectGarbage();
}

void SynthEngine::appendMidiInputState (juce::ValueTree& state) const
{
    for (int i = state.getNumChildren() - 1; i >= 0; --i)
        if (state.getChild (i).hasType ("MIDI_INPUT"))
            state.removeChild (i, nullptr);

    juce::ValueTree input ("MIDI_INPUT");
    input.setProperty ("schemaVersion", 1, nullptr);
    input.setProperty ("lowerMpeMembers", lowerMpeMembers.load(), nullptr);
    input.setProperty ("upperMpeMembers", upperMpeMembers.load(), nullptr);
    for (int channel = 0; channel < 16; ++channel)
    {
        juce::ValueTree tuning ("CHANNEL_TUNING");
        tuning.setProperty ("channel", channel + 1, nullptr);
        tuning.setProperty ("bendSensitivity", bendSensitivity[(size_t) channel].load(), nullptr);
        tuning.setProperty ("fine", channelFineTune[(size_t) channel].load(), nullptr);
        tuning.setProperty ("coarse", channelCoarseTune[(size_t) channel].load(), nullptr);
        input.addChild (tuning, -1, nullptr);
    }
    for (int cc = 0; cc < 128; ++cc)
    {
        const int binding = learnedCCBinding[(size_t) cc].load (std::memory_order_acquire);
        if (binding >= 0 && binding < (int) parameterBindings.size())
        {
            juce::ValueTree assignment ("LEARNED_CC");
            assignment.setProperty ("parameter", parameterBindings[(size_t) binding].id, nullptr);
            assignment.setProperty ("cc", cc, nullptr);
            input.addChild (assignment, -1, nullptr);
        }
    }
    state.addChild (input, -1, nullptr);
}

bool SynthEngine::restoreMidiInputState (const juce::ValueTree& state, juce::String* error, bool apply)
{
    juce::ValueTree input;
    for (int i = 0; i < state.getNumChildren(); ++i)
        if (state.getChild (i).hasType ("MIDI_INPUT"))
        {
            if (input.isValid())
            {
                if (error) *error = "State contains duplicate MIDI input configuration.";
                return false;
            }
            input = state.getChild (i);
        }
    if (! input.isValid())
    {
        if (! apply)
            return true;
        lowerMpeMembers.store (0);
        upperMpeMembers.store (0);
        for (int channel = 0; channel < 16; ++channel)
        {
            bendSensitivity[(size_t) channel].store (2.0f);
            channelFineTune[(size_t) channel].store (0.0f);
            channelCoarseTune[(size_t) channel].store (0.0f);
        }
        pendingMidiLearnBinding.store (-1);
        for (auto& mapping : learnedCCBinding)
            mapping.store (-1);
        midiLearnGeneration.fetch_add (1, std::memory_order_acq_rel);
        midiStateRefresh.store (true, std::memory_order_release);
        return true;
    }
    if ((int) input.getProperty ("schemaVersion", -1) != 1)
    {
        if (error) *error = "Unsupported MIDI input configuration version.";
        return false;
    }

    const int lowerMembers = (int) input.getProperty ("lowerMpeMembers", -1);
    const int upperMembers = (int) input.getProperty ("upperMpeMembers", -1);
    if (lowerMembers < 0 || lowerMembers > 15 || upperMembers < 0 || upperMembers > 15
        || lowerMembers + upperMembers > 15)
    {
        if (error) *error = "MIDI input state contains an invalid MPE zone size.";
        return false;
    }
    struct Tuning { float bend { 2.0f }, fine { 0.0f }, coarse { 0.0f }; };
    std::array<Tuning, 16> tunings {};
    std::array<bool, 16> tuningFound {};
    std::vector<int> learnedBindings (parameterBindings.size(), -1);
    std::array<int, 128> learnedCCs {};
    learnedCCs.fill (-1);
    for (int i = 0; i < input.getNumChildren(); ++i)
    {
        const auto child = input.getChild (i);
        if (child.hasType ("CHANNEL_TUNING"))
        {
            const int channel = (int) child.getProperty ("channel", 0) - 1;
            const float bend = (float) child.getProperty ("bendSensitivity", -1.0f);
            const float fine = (float) child.getProperty ("fine", 1000.0f);
            const float coarse = (float) child.getProperty ("coarse", 1000.0f);
            if (! juce::isPositiveAndBelow (channel, 16) || tuningFound[(size_t) channel]
                || ! std::isfinite (bend) || bend < 0.0f || bend > 12.99f
                || ! std::isfinite (fine) || fine < -100.0f || fine > 100.0f
                || ! std::isfinite (coarse) || coarse < -64.0f || coarse > 63.0f)
            {
                if (error) *error = "MIDI input state contains invalid or duplicate channel tuning.";
                return false;
            }
            tunings[(size_t) channel] = { bend, fine, coarse };
            tuningFound[(size_t) channel] = true;
        }
        else if (child.hasType ("LEARNED_CC"))
        {
            const int binding = findBinding (child.getProperty ("parameter").toString());
            const int cc = (int) child.getProperty ("cc", -1);
            if (binding < 0 || ! juce::isPositiveAndBelow (cc, 128)
                || learnedBindings[(size_t) binding] >= 0 || learnedCCs[(size_t) cc] >= 0)
            {
                if (error) *error = "MIDI input state contains an invalid or duplicate learned CC assignment.";
                return false;
            }
            learnedBindings[(size_t) binding] = cc;
            learnedCCs[(size_t) cc] = binding;
        }
        else
        {
            if (error) *error = "MIDI input state contains an unknown child node.";
            return false;
        }
    }

    if (! apply)
        return true;
    lowerMpeMembers.store (lowerMembers);
    upperMpeMembers.store (upperMembers);
    for (int channel = 0; channel < 16; ++channel)
    {
        bendSensitivity[(size_t) channel].store (tunings[(size_t) channel].bend);
        channelFineTune[(size_t) channel].store (tunings[(size_t) channel].fine);
        channelCoarseTune[(size_t) channel].store (tunings[(size_t) channel].coarse);
    }
    for (int cc = 0; cc < 128; ++cc)
        learnedCCBinding[(size_t) cc].store (learnedCCs[(size_t) cc]);
    midiLearnGeneration.fetch_add (1, std::memory_order_acq_rel);
    midiStateRefresh.store (true, std::memory_order_release);
    return true;
}

bool SynthEngine::isLearnableController (int cc) noexcept
{
    // Bank select, (N)RPN / data entry, sustain and the channel mode messages keep their meaning:
    // touching the pedal while a control waits for MIDI learn must not take it over.
    switch (cc)
    {
        case 0: case 6: case 32: case 38: case 64:
        case 96: case 97: case 98: case 99: case 100: case 101:
            return false;
        default:
            return cc < 120;
    }
}

int SynthEngine::findBinding (const juce::String& parameterId) const noexcept
{
    for (size_t i = 0; i < parameterBindings.size(); ++i)
        if (parameterBindings[i].id == parameterId)
            return (int) i;
    return -1;
}

bool SynthEngine::requestMidiLearn (const juce::String& parameterId)
{
    const int binding = findBinding (parameterId);
    if (binding < 0)
        return false;
    pendingMidiLearnBinding.store (binding);
    return true;
}

void SynthEngine::clearMidiLearn (const juce::String& parameterId)
{
    const int binding = findBinding (parameterId);
    if (binding < 0)
        return;
    for (int cc = 0; cc < 128; ++cc)
    {
        int expected = binding;
        learnedCCBinding[(size_t) cc].compare_exchange_strong (expected, -1, std::memory_order_acq_rel);
    }
    if (pendingMidiLearnBinding.load() == binding)
        pendingMidiLearnBinding.store (-1);
    midiLearnGeneration.fetch_add (1, std::memory_order_acq_rel);
}

int SynthEngine::getMidiLearnCC (const juce::String& parameterId) const
{
    const int binding = findBinding (parameterId);
    if (binding < 0)
        return -1;
    for (int cc = 0; cc < 128; ++cc)
        if (learnedCCBinding[(size_t) cc].load (std::memory_order_acquire) == binding)
            return cc;
    return -1;
}

bool SynthEngine::popMidiParameterChange (MidiParameterChange& change) noexcept
{
    const auto read = midiParameterRead.load (std::memory_order_relaxed);
    if (read == midiParameterWrite.load (std::memory_order_acquire))
        return false;
    change = midiParameterQueue[(size_t) (read % kMidiParameterQueueSize)];
    midiParameterRead.store (read + 1, std::memory_order_release);
    return true;
}

bool SynthEngine::hasPendingMidiParameterChanges() const noexcept
{
    return midiParameterRead.load (std::memory_order_acquire)
        != midiParameterWrite.load (std::memory_order_acquire);
}

void SynthEngine::queueParameterValue (int binding, int value, int maximum) noexcept
{
    if (! juce::isPositiveAndBelow (binding, (int) parameterBindings.size()) || maximum <= 0)
        return;
    const auto& target = parameterBindings[(size_t) binding];
    if (target.parameter == nullptr || target.spec == nullptr)
        return;

    const int sevenBit = juce::roundToInt ((float) value * 127.0f / (float) maximum);
    float plain = 0.0f;
    if (target.spec->kind == Kind::boolParam)
        plain = sevenBit >= 64 ? 1.0f : 0.0f;
    else if (target.spec->kind == Kind::choiceParam)
    {
        const int count = target.spec->choices.size();
        static constexpr int choice3[] = { 0, 43, 85 };
        static constexpr int choice4[] = { 0, 32, 64, 96 };
        static constexpr int choice5[] = { 0, 26, 51, 77, 102 };
        static constexpr int choice6[] = { 0, 21, 43, 64, 85, 107 };
        const int* values = nullptr;
        switch (count)
        {
            case 2: plain = sevenBit >= 64 ? 1.0f : 0.0f; break;
            case 3: values = choice3; break;
            case 4: values = choice4; break;
            case 5: values = choice5; break;
            case 6: values = choice6; break;
            default: plain = (float) juce::roundToInt ((float) sevenBit / 127.0f * (float) (count - 1)); break;
        }
        if (values != nullptr)
        {
            int choice = 0;
            for (int i = 1; i < count; ++i)
                if (std::abs (sevenBit - values[i]) < std::abs (sevenBit - values[choice]))
                    choice = i;
            plain = (float) choice;
        }
        if (target.spec->midiCC == 79)
            plain = (float) juce::jlimit (1, 4, value);
    }
    else
    {
        const auto range = target.parameter->getNormalisableRange();
        plain = range.convertFrom0to1 ((float) value / (float) maximum);
        if (target.spec->kind == Kind::intParam)
            plain = (float) juce::roundToInt (plain);
    }

    const float normalized = target.parameter->convertTo0to1 (plain);
    const auto write = midiParameterWrite.load (std::memory_order_relaxed);
    const auto read = midiParameterRead.load (std::memory_order_acquire);
    if (write - read >= kMidiParameterQueueSize)
    {
        droppedMidiParameterChanges.fetch_add (1, std::memory_order_relaxed);
        return;
    }
    midiParameterQueue[(size_t) (write % kMidiParameterQueueSize)] = { target.parameter, normalized };
    midiParameterWrite.store (write + 1, std::memory_order_release);
}

void SynthEngine::applyHardwareController (int channel, int cc, int value, int maximum)
{
    const bool channelRouting = perf.midiRouting == MidiRouting::channelPerLayer;
    for (int binding = 0; binding < (int) parameterBindings.size(); ++binding)
    {
        const auto& target = parameterBindings[(size_t) binding];
        if (target.spec == nullptr || target.spec->midiCC != cc)
            continue;
        if (target.layer >= 0)
        {
            if (channelRouting)
            {
                if ((channel != 1 && channel != 2)
                    || target.layer != (channel == 1 ? toInt (Layer::upper) : toInt (Layer::lower)))
                    continue;
            }
            else if (currentMode == KeyboardMode::single && target.layer != toInt (currentSingleLayer))
                continue;
        }
        queueParameterValue (binding, value, maximum);
    }
}

bool SynthEngine::isMpeMemberChannel (int channel) const noexcept
{
    const int lowerCount = lowerMpeMembers.load (std::memory_order_relaxed);
    const int upperCount = upperMpeMembers.load (std::memory_order_relaxed);
    return (lowerCount > 0 && channel >= 2 && channel <= lowerCount + 1)
        || (upperCount > 0 && channel >= 16 - upperCount && channel <= 15);
}

void SynthEngine::updateChannelExpression (int channel) noexcept
{
    if (! juce::isPositiveAndBelow (channel - 1, 16))
        return;
    const auto index = (size_t) (channel - 1);
    const auto& midi = midiChannels[index];
    const float bend = midi.pitchBend;
    const float sensitivity = bendSensitivity[index].load (std::memory_order_relaxed);
    const float tuning = channelFineTune[index].load (std::memory_order_relaxed) / 100.0f
                       + channelCoarseTune[index].load (std::memory_order_relaxed);
    const bool mpe = isMpeMemberChannel (channel);
    float zoneBend = 0.0f, zoneSensitivity = 2.0f;
    if (mpe)
    {
        const int master = channel <= lowerMpeMembers.load (std::memory_order_relaxed) + 1 ? 1 : 16;
        zoneBend = midiChannels[(size_t) (master - 1)].pitchBend;
        zoneSensitivity = bendSensitivity[(size_t) (master - 1)].load (std::memory_order_relaxed);
    }
    for (auto* synthLayer : { &upper, &lower })
        synthLayer->setMidiExpression (channel, bend, sensitivity, tuning,
                                       midi.pressure, midi.timbre, mpe, zoneBend, zoneSensitivity);
}

bool SynthEngine::isMpeMasterChannel (int channel) const noexcept
{
    return (channel == 1 && lowerMpeMembers.load (std::memory_order_relaxed) > 0)
        || (channel == 16 && upperMpeMembers.load (std::memory_order_relaxed) > 0);
}

void SynthEngine::handleRpnDataEntry (int channel, bool lsbUpdated) noexcept
{
    auto& midi = midiChannels[(size_t) (channel - 1)];
    if (midi.rpnMsb == 0)
    {
        const int data = midi.dataEntryMsb * 128 + midi.dataEntryLsb;
        const auto index = (size_t) (channel - 1);
        if (midi.rpnLsb == 0)
            bendSensitivity[index].store ((float) juce::jmin (12, midi.dataEntryMsb)
                                           + (float) juce::jmin (99, midi.dataEntryLsb) / 100.0f);
        else if (midi.rpnLsb == 1)
            channelFineTune[index].store (((float) data - 8192.0f) * (100.0f / 8192.0f));
        else if (midi.rpnLsb == 2 && ! lsbUpdated)
            channelCoarseTune[index].store ((float) (midi.dataEntryMsb - 64));
        else if (midi.rpnLsb == 6 && ! lsbUpdated)
        {
            // MPE Configuration Message. Per the MPE spec the zone's member channels default to a
            // 48 semitone bend range and its master channel to 2 (both can be changed by RPN 0 later).
            const int members = juce::jlimit (0, 15, midi.dataEntryMsb);
            int first = 0, last = -1;
            if (channel == 1)
            {
                const int count = juce::jmin (members, 15 - upperMpeMembers.load (std::memory_order_relaxed));
                lowerMpeMembers.store (count);
                first = 2;
                last = count + 1;
            }
            else if (channel == 16)
            {
                const int count = juce::jmin (members, 15 - lowerMpeMembers.load (std::memory_order_relaxed));
                upperMpeMembers.store (count);
                first = 16 - count;
                last = 15;
            }
            if (channel == 1 || channel == 16)
            {
                bendSensitivity[(size_t) (channel - 1)].store (2.0f);
                for (int member = first; member <= last; ++member)
                    bendSensitivity[(size_t) (member - 1)].store (48.0f);
            }
            for (int ch = 1; ch <= 16; ++ch)
                updateChannelExpression (ch);
            return;
        }
    }
    else if (midi.nrpnMsb == 8 && perf.hardwareCCMap)
    {
        static constexpr int mappedNrpnLsb[] = {
            1, 2, 3, 17, 18, 19, 21, 22, 25, 26, 28, 32, 33, 35, 37, 42,
            45, 46, 47, 52, 53, 54, 55, 56, 57, 58, 62, 63, 70, 71, 72, 73,
            74, 75, 76, 77, 91, 92, 94, 95, 108, 109, 110, 111
        };
        if (std::find (std::begin (mappedNrpnLsb), std::end (mappedNrpnLsb), midi.nrpnLsb) != std::end (mappedNrpnLsb))
            applyHardwareController (channel, midi.nrpnLsb, midi.dataEntryMsb * 128 + midi.dataEntryLsb, 16383);
    }
    updateChannelExpression (channel);
}

bool SynthEngine::requestWaveImport (Layer l, int slot, const juce::File& file, juce::String* error)
{
    if (! juce::isPositiveAndBelow (slot, WaveBank::kNumAltWaves))
    {
        if (error) *error = "Alternative wave slot must be in the range 1..32.";
        return false;
    }
    if (! file.existsAsFile())
    {
        if (error) *error = "Waveform file does not exist: " + file.getFullPathName();
        return false;
    }

    auto state = waveImportState.load();
    while (state != WaveImportState::building)
        if (waveImportState.compare_exchange_weak (state, WaveImportState::building))
            break;
    if (state == WaveImportState::building)
    {
        if (error) *error = "A wavetable import is already in progress.";
        return false;
    }

    const auto extension = file.getFileExtension().toLowerCase();
    {
        const std::lock_guard<std::mutex> lock (waveImportMutex);
        waveImportError.clear();
    }
    waveImportPool.addJob (new WaveImportJob ([this, l, slot, file, extension]
    {
        std::vector<float> cycle;
        juce::String failure;
        bool loaded = false;
        if (extension == ".ws6")
            loaded = WaveBank::readWs6 (file, cycle, &failure);
        else if (extension == ".wav" || extension == ".aif" || extension == ".aiff")
            loaded = WaveBank::readAudioCycle (file, cycle, &failure);
        else
            failure = "Supported waveform formats are .ws6, .wav and .aiff.";

        if (loaded)
        {
            // Based on the newest bank of the layer, so a preset loaded meanwhile is not undone.
            auto rebuilt = std::make_shared<WaveBank> (*getWaveBank (l));
            rebuilt->setAltCycle (slot, cycle.data());
            setWaveBank (l, std::move (rebuilt));
            waveImportState.store (WaveImportState::ready);
            return;
        }

        {
            const std::lock_guard<std::mutex> lock (waveImportMutex);
            waveImportError = failure.isNotEmpty() ? failure : "Wavetable import failed.";
        }
        waveImportState.store (WaveImportState::failed);
    }), true);
    return true;
}

void SynthEngine::acknowledgeWaveImport() noexcept
{
    auto state = waveImportState.load();
    while (state == WaveImportState::ready || state == WaveImportState::failed)
        if (waveImportState.compare_exchange_weak (state, WaveImportState::idle))
            break;
}

juce::String SynthEngine::getWaveImportError() const
{
    const std::lock_guard<std::mutex> lock (waveImportMutex);
    return waveImportError;
}

bool SynthEngine::exportWave (Layer l, int slot, const juce::File& file, juce::String* error) const
{
    if (! juce::isPositiveAndBelow (slot, WaveBank::kNumAltWaves))
    {
        if (error) *error = "Alternative wave slot must be in the range 1..32.";
        return false;
    }
    return WaveBank::writeWs6 (file.withFileExtension (".ws6"), getWaveBank (l)->altCycle (slot), error);
}

void SynthEngine::routeNote (const juce::MidiMessage& m, bool on)
{
    const int note = m.getNoteNumber();
    const float velocity = m.getFloatVelocity();

    auto send = [&] (SynthLayer& l)
    {
        if (on)
        {
            l.noteOn (note, velocity, m.getChannel());
            updateChannelExpression (m.getChannel());
        }
        else
            l.noteOff (note, m.getChannel());
    };

    // Optional per-layer channel routing: base channel = upper, +1 = lower.
    if (perf.midiRouting == MidiRouting::channelPerLayer && ! isMpeMemberChannel (m.getChannel())
        && (m.getChannel() == 1 || m.getChannel() == 2))
    {
        send (layer (m.getChannel() == 1 ? Layer::upper : Layer::lower));
        return;
    }

    switch (currentMode)
    {
        case KeyboardMode::single: send (layer (currentSingleLayer)); break;
        case KeyboardMode::dual:   send (upper); send (lower); break;
        case KeyboardMode::split:
            if (on)
                send (note >= perf.splitPoint ? upper : lower); // split point = first upper note
            else
            {
                upper.noteOff (note, m.getChannel()); // the split point may have moved since note-on
                lower.noteOff (note, m.getChannel());
            }
            break;
    }
}

void SynthEngine::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        routeNote (m, true);
    else if (m.isNoteOff())
        routeNote (m, false);
    else if (m.isPitchWheel())
    {
        const int channel = m.getChannel();
        midiChannels[(size_t) (channel - 1)].pitchBend =
            juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8192.0f);
        updateChannelExpression (channel);
        if (isMpeMasterChannel (channel))   // the zone's master bend moves every note of the zone
            for (int member = 2; member <= 15; ++member)
                if (isMpeMemberChannel (member))
                    updateChannelExpression (member);
    }
    else if (m.isAftertouch())
    {
        upper.polyAftertouch (m.getNoteNumber(), (float) m.getAfterTouchValue() / 127.0f, m.getChannel());
        lower.polyAftertouch (m.getNoteNumber(), (float) m.getAfterTouchValue() / 127.0f, m.getChannel());
    }
    else if (m.isChannelPressure())
    {
        const int channel = m.getChannel();
        midiChannels[(size_t) (channel - 1)].pressure = (float) m.getChannelPressureValue() / 127.0f;
        updateChannelExpression (channel);
    }
    else if (m.isController())
    {
        const int channel = m.getChannel();
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        auto& midi = midiChannels[(size_t) (channel - 1)];
        if (cc == 99)
        {
            midi.nrpnMsb = v;
            midi.rpnMsb = midi.rpnLsb = 127;
            return;
        }
        if (cc == 98)
        {
            midi.nrpnLsb = v;
            midi.rpnMsb = midi.rpnLsb = 127;
            return;
        }
        if (cc == 101)
        {
            midi.rpnMsb = v;
            midi.nrpnMsb = midi.nrpnLsb = 127;
            return;
        }
        if (cc == 100)
        {
            midi.rpnLsb = v;
            midi.nrpnMsb = midi.nrpnLsb = 127;
            return;
        }
        if (cc == 6 || cc == 38)
        {
            if (cc == 6)
                midi.dataEntryMsb = v;
            else
                midi.dataEntryLsb = v;
            handleRpnDataEntry (channel, cc == 38);
            return;
        }

        switch (cc)
        {
            case 1:  midiModLever = (float) v / 127.0f; break;
            case 2:  ribbonMsb = v; midiRibbon = juce::jlimit (-1.0f, 1.0f, (float) (v - 64) / 64.0f); break;
            case 34: midiRibbon = juce::jlimit (-1.0f, 1.0f, ((float) (ribbonMsb * 128 + v) - 8192.0f) / 8192.0f); break;
            case 4:
            case 11: midiExpression = (float) v / 127.0f; break;
            case 74:
                midi.timbre = (float) v / 127.0f;
                updateChannelExpression (channel);
                break;
            case 64:
                if (perf.dualPedal && (channel == 1 || channel == 2))
                    layer (channel == 1 ? Layer::upper : Layer::lower).setSustain (v >= 64);
                else
                {
                    upper.setSustain (v >= 64);
                    lower.setSustain (v >= 64);
                }
                break;
            case 120:
                upper.allNotesOff (true);
                lower.allNotesOff (true);
                break;
            case 123:
                upper.allNotesOff (false);
                lower.allNotesOff (false);
                break;
            default: break;
        }

        if (cc == 121)
        {
            midi.rpnMsb = midi.rpnLsb = midi.nrpnMsb = midi.nrpnLsb = 127;
            midi.dataEntryMsb = midi.dataEntryLsb = 0;
            midi.pressure = midi.timbre = 0.0f;
            midiModLever = midiExpression = midiRibbon = 0.0f;
            ribbonMsb = 64;
            if (perf.dualPedal && (channel == 1 || channel == 2))
                layer (channel == 1 ? Layer::upper : Layer::lower).setSustain (false);
            else
            {
                upper.setSustain (false);
                lower.setSustain (false);
            }
            updateChannelExpression (channel);
        }

        int learned = learnedCCBinding[(size_t) cc].load (std::memory_order_acquire);
        if (learned < 0 && isLearnableController (cc))
        {
            const int pending = pendingMidiLearnBinding.exchange (-1, std::memory_order_acq_rel);
            if (pending >= 0)
            {
                for (auto& previous : learnedCCBinding)
                {
                    int previousBinding = pending;
                    previous.compare_exchange_strong (previousBinding, -1, std::memory_order_acq_rel);
                }
                learned = pending;
                learnedCCBinding[(size_t) cc].store (learned, std::memory_order_release);
                midiLearnGeneration.fetch_add (1, std::memory_order_acq_rel);
            }
        }
        if (learned >= 0)
            queueParameterValue (learned, v, 127);
        else if (perf.hardwareCCMap)
            applyHardwareController (channel, cc, v, 127);
    }
    else if (m.isProgramChange())
    {
        pendingProgramChange.store (m.getProgramChangeNumber(), std::memory_order_release);
    }
}
} // namespace nsynth
