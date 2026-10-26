#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace nsynth;

namespace
{
bool sameUserWaveState (const juce::ValueTree& a, const juce::ValueTree& b)
{
    const auto aCycles = a.getProperty ("cycles");
    const auto bCycles = b.getProperty ("cycles");
    const auto* aData = aCycles.getBinaryData();
    const auto* bData = bCycles.getBinaryData();
    return aData != nullptr && bData != nullptr && *aData == *bData;
}
} // namespace

juce::AudioProcessor::BusesProperties NSynthAudioProcessor::makeBuses()
{
    // Main stereo mix output plus optional per-layer stereo outputs.
    return BusesProperties()
        .withOutput ("Mix", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Upper", juce::AudioChannelSet::stereo(), false)
        .withOutput ("Lower", juce::AudioChannelSet::stereo(), false);
}

NSynthAudioProcessor::NSynthAudioProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "NSYNTH", createParameterLayout()),
      engine (apvts)
{
    for (auto layer : { Layer::upper, Layer::lower })
    {
        patchBaseline[(size_t) toInt (layer)] = captureLayerValues (layer);
        patchMatrixBaseline[(size_t) toInt (layer)] = *engine.modMatrix (layer).snapshot();
        patchWaveBaseline[(size_t) toInt (layer)] = engine.getWaveBank().toValueTree();
    }

    auto root = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                    .getChildFile ("nBundleAudio").getChildFile ("nSynth");
    patchDirectory = root.getChildFile ("Patches");
    patchDirectory.createDirectory();
    const auto factoryRoot = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                                 .getSiblingFile ("Presets");
    factoryPatchDirectory = factoryRoot.getChildFile ("Patches");
    presetManager = std::make_unique<nSynth::PresetManager> (
        factoryRoot.getChildFile ("Performances"), root.getChildFile ("Performances"), ".nsynthperf");
}

NSynthAudioProcessor::~NSynthAudioProcessor()
{
    cancelPendingUpdate();
}

bool NSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return layouts.inputBuses.isEmpty();
}

void NSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
#if defined (NSYNTH_TESTS)
    engine.prepare (sampleRate, samplesPerBlock, 0);
#else
    engine.prepare (sampleRate, samplesPerBlock);
#endif
    setLatencySamples ((int) std::lround (engine.getLatencySamples()));
}

void NSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    double bpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;

    auto mix = getBusBuffer (buffer, false, 0);
    juce::AudioBuffer<float> upperBus, lowerBus;
    juce::AudioBuffer<float>* upperPtr = nullptr;
    juce::AudioBuffer<float>* lowerPtr = nullptr;
    if (getBusCount (false) > 1 && getBus (false, 1)->isEnabled())
    {
        upperBus = getBusBuffer (buffer, false, 1);
        upperPtr = &upperBus;
    }
    if (getBusCount (false) > 2 && getBus (false, 2)->isEnabled())
    {
        lowerBus = getBusBuffer (buffer, false, 2);
        lowerPtr = &lowerBus;
    }

    engine.process (mix, upperPtr, lowerPtr, midi, bpm);
    if (engine.hasPendingMidiParameterChanges())
        triggerAsyncUpdate();

    for (auto l : { Layer::upper, Layer::lower })
        activeVoices[(size_t) toInt (l)] = engine.getActiveVoices (l);

    const int latency = (int) std::lround (engine.getLatencySamples());
    if (latency != getLatencySamples() && pendingLatency.exchange (latency) != latency)
        triggerAsyncUpdate();
}

void NSynthAudioProcessor::handleAsyncUpdate()
{
    nsynth::SynthEngine::MidiParameterChange change;
    while (engine.popMidiParameterChange (change))
        if (change.parameter != nullptr)
            change.parameter->setValueNotifyingHost (change.normalizedValue);
    setLatencySamples (pendingLatency.load());
}

juce::AudioProcessorEditor* NSynthAudioProcessor::createEditor()
{
    return new NSynthAudioProcessorEditor (*this);
}

void NSynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    engine.appendModMatrixState (state);
    engine.appendWaveBankState (state);
    engine.appendMidiInputState (state);
    state.setProperty ("editorScale", editorScale.load(), nullptr);
    state.setProperty ("presetName", currentPresetName, nullptr);
    state.setProperty ("stateVersion", 6, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
        return;
    if (! engine.restoreMidiInputState (tree))
        return;
    if (! engine.restoreModMatrixState (tree))
        return;
    if (! engine.restoreWaveBankState (tree))
        return;

    removeLegacyChildren (tree);
    editorScale = (float) tree.getProperty ("editorScale", 1.0f);
    currentPresetName = tree.getProperty ("presetName", "Init").toString();
    apvts.replaceState (tree);
    for (auto layer : { Layer::upper, Layer::lower })
    {
        const auto index = (size_t) toInt (layer);
        patchBaseline[index] = captureLayerValues (layer);
        patchMatrixBaseline[index] = *engine.modMatrix (layer).snapshot();
        patchWaveBaseline[index] = getWaveBankState (tree);
        if (! patchWaveBaseline[index].isValid())
            patchWaveBaseline[index] = engine.getWaveBank().toValueTree();
        patchNames[index] = "Session";
        patchCompareActive[index] = false;
    }
}

void NSynthAudioProcessor::initLayer (Layer layer)
{
    for (auto& spec : getLayerParamSpecs())
        if (auto* p = apvts.getParameter (layerParamId (layer, spec.suffix)))
            p->setValueNotifyingHost (p->getDefaultValue());
    patchNames[(size_t) toInt (layer)] = "Init";
    engine.modMatrix (layer).clearAll();
    patchBaseline[(size_t) toInt (layer)] = captureLayerValues (layer);
    patchMatrixBaseline[(size_t) toInt (layer)] = *engine.modMatrix (layer).snapshot();
    patchWaveBaseline[(size_t) toInt (layer)] = engine.getWaveBank().toValueTree();
    patchCompareActive[(size_t) toInt (layer)] = false;
}

void NSynthAudioProcessor::copyLayer (Layer from, Layer to)
{
    for (auto& spec : getLayerParamSpecs())
    {
        auto* src = apvts.getParameter (layerParamId (from, spec.suffix));
        auto* dst = apvts.getParameter (layerParamId (to, spec.suffix));
        if (src != nullptr && dst != nullptr)
            dst->setValueNotifyingHost (src->getValue());
    }
    engine.modMatrix (to).restoreSnapshot (*engine.modMatrix (from).snapshot());
    patchNames[(size_t) toInt (to)] = "Copied";
    patchCompareActive[(size_t) toInt (to)] = false;
}

std::vector<float> NSynthAudioProcessor::captureLayerValues (Layer layer) const
{
    std::vector<float> values;
    values.reserve (getLayerParamSpecs().size());
    for (const auto& spec : getLayerParamSpecs())
        if (auto* parameter = apvts.getParameter (layerParamId (layer, spec.suffix)))
            values.push_back (parameter->getValue());
    return values;
}

void NSynthAudioProcessor::applyLayerValues (Layer layer, const std::vector<float>& values)
{
    if (values.size() != getLayerParamSpecs().size())
        return;
    for (size_t i = 0; i < values.size(); ++i)
        if (auto* parameter = apvts.getParameter (layerParamId (layer, getLayerParamSpecs()[i].suffix)))
            parameter->setValueNotifyingHost (values[i]);
}

juce::ValueTree NSynthAudioProcessor::getWaveBankState (const juce::ValueTree& root) const
{
    juce::ValueTree result;
    for (int i = 0; i < root.getNumChildren(); ++i)
        if (root.getChild (i).hasType ("USER_WAVES"))
        {
            if (result.isValid())
                return {};
            result = root.getChild (i);
        }
    return result;
}

void NSynthAudioProcessor::removeLegacyChildren (juce::ValueTree& root)
{
    // Sequencer banks from sessions saved before the sequencer was removed.
    for (int i = root.getNumChildren() - 1; i >= 0; --i)
        if (root.getChild (i).hasType ("SEQUENCER"))
            root.removeChild (i, nullptr);
}

bool NSynthAudioProcessor::loadPatch (Layer layer, const juce::File& file, juce::String* error)
{
    juce::ValueTree tree;
    if (! presetManager->loadFromFile (tree, file, error))
        return false;
    if (! tree.hasType ("NSYNTH_PATCH") || (int) tree.getProperty ("layer", -1) != toInt (layer))
    {
        if (error) *error = "Patch file type or target layer does not match.";
        return false;
    }

    ModMatrix stagedMatrix;
    bool hasMatrix = false;
    bool hasWaveBank = false;
    juce::ValueTree stagedWaveBank;
    std::vector<float> values (getLayerParamSpecs().size(), 0.0f);
    std::vector<bool> found (values.size(), false);
    for (int childIndex = 0; childIndex < tree.getNumChildren(); ++childIndex)
    {
        const auto child = tree.getChild (childIndex);
        if (child.hasType ("MOD_MATRIX"))
        {
            if (hasMatrix || (int) child.getProperty ("layer", -1) != toInt (layer)
                || ! stagedMatrix.restoreFromValueTree (child, error))
            {
                if (error && error->isEmpty())
                    *error = "Patch contains an invalid or duplicate modulation matrix.";
                return false;
            }
            hasMatrix = true;
            continue;
        }
        if (child.hasType ("USER_WAVES"))
        {
            if (hasWaveBank || ! WaveBank::isValidValueTree (child, error))
                return false;
            hasWaveBank = true;
            stagedWaveBank = child;
            continue;
        }
        if (! child.hasType ("PARAM"))
            continue;
        const auto id = child.getProperty ("id").toString();
        bool matched = false;
        for (size_t i = 0; i < getLayerParamSpecs().size(); ++i)
        {
            if (id != layerParamId (layer, getLayerParamSpecs()[i].suffix))
                continue;
            const float value = (float) child.getProperty ("value", -1.0f);
            if (found[i] || ! std::isfinite (value) || value < 0.0f || value > 1.0f)
            {
                if (error) *error = "Patch contains a duplicate or invalid parameter value.";
                return false;
            }
            values[i] = value;
            found[i] = true;
            matched = true;
            break;
        }
        // Parameters of this layer that no longer exist (arp/sequencer, old FX) are skipped.
        if (! matched && ! id.startsWith (layerPrefix (layer)))
        {
            if (error) *error = "Patch contains a parameter for the wrong layer or an unknown parameter.";
            return false;
        }
    }

    // Parameters added after the patch was saved start from their defaults.
    for (size_t i = 0; i < found.size(); ++i)
        if (! found[i])
            if (auto* parameter = apvts.getParameter (layerParamId (layer, getLayerParamSpecs()[i].suffix)))
                values[i] = parameter->getDefaultValue();

    if (hasWaveBank && ! engine.restoreWaveBankState (tree, error))
        return false;
    if (! hasWaveBank)
        stagedWaveBank = engine.getWaveBank().toValueTree();
    applyLayerValues (layer, values);
    const auto index = (size_t) toInt (layer);
    if (hasMatrix)
        engine.modMatrix (layer).restoreSnapshot (*stagedMatrix.snapshot());
    else
        engine.modMatrix (layer).clearAll();
    patchBaseline[index] = std::move (values);
    patchMatrixBaseline[index] = *engine.modMatrix (layer).snapshot();
    patchWaveBaseline[index] = stagedWaveBank;
    patchNames[index] = file.getFileNameWithoutExtension();
    patchCompareActive[index] = false;
    return true;
}

bool NSynthAudioProcessor::savePatch (Layer layer, const juce::File& file, juce::String* error)
{
    juce::ValueTree tree ("NSYNTH_PATCH");
    tree.setProperty ("layer", toInt (layer), nullptr);
    tree.setProperty ("stateVersion", 4, nullptr);
    tree.addChild (engine.modMatrix (layer).toValueTree (toInt (layer)), -1, nullptr);
    engine.appendWaveBankState (tree);
    for (const auto& spec : getLayerParamSpecs())
    {
        auto* parameter = apvts.getParameter (layerParamId (layer, spec.suffix));
        if (parameter == nullptr)
        {
            if (error) *error = "Could not find patch parameter: " + juce::String (spec.suffix);
            return false;
        }
        juce::ValueTree child ("PARAM");
        child.setProperty ("id", layerParamId (layer, spec.suffix), nullptr);
        child.setProperty ("value", parameter->getValue(), nullptr);
        tree.addChild (child, -1, nullptr);
    }
    if (! presetManager->saveToFile (tree, file.withFileExtension ("nsynthpatch"), error))
        return false;

    const auto index = (size_t) toInt (layer);
    patchBaseline[index] = captureLayerValues (layer);
    patchMatrixBaseline[index] = *engine.modMatrix (layer).snapshot();
    patchWaveBaseline[index] = engine.getWaveBank().toValueTree();
    patchNames[index] = file.getFileNameWithoutExtension();
    patchCompareActive[index] = false;
    return true;
}

bool NSynthAudioProcessor::isPatchDirty (Layer layer) const
{
    const auto current = captureLayerValues (layer);
    const auto& baseline = patchBaseline[(size_t) toInt (layer)];
    if (current.size() != baseline.size())
        return true;
    for (size_t i = 0; i < current.size(); ++i)
        if (std::abs (current[i] - baseline[i]) > 1.0e-6f)
            return true;
    const auto& currentMatrix = *engine.modMatrix (layer).snapshot();
    const auto& baselineMatrix = patchMatrixBaseline[(size_t) toInt (layer)];
    if (currentMatrix.fixed != baselineMatrix.fixed || currentMatrix.direct != baselineMatrix.direct)
        return true;
    return ! sameUserWaveState (engine.getWaveBank().toValueTree(), patchWaveBaseline[(size_t) toInt (layer)]);
}

bool NSynthAudioProcessor::setPatchCompare (Layer layer, bool enabled)
{
    const auto index = (size_t) toInt (layer);
    if (enabled == patchCompareActive[index])
        return true;

    if (enabled)
    {
        if (patchBaseline[index].size() != getLayerParamSpecs().size())
            return false;
        patchCompareSnapshot[index] = captureLayerValues (layer);
        patchMatrixCompareSnapshot[index] = *engine.modMatrix (layer).snapshot();
        patchWaveCompareSnapshot[index] = engine.getWaveBank().toValueTree();
        juce::ValueTree waveState ("NSYNTH_WAVE_STATE");
        waveState.addChild (patchWaveBaseline[index].createCopy(), -1, nullptr);
        if (! engine.restoreWaveBankState (waveState))
            return false;
        applyLayerValues (layer, patchBaseline[index]);
        engine.modMatrix (layer).restoreSnapshot (patchMatrixBaseline[index]);
        patchCompareActive[index] = true;
        return true;
    }

    if (patchCompareSnapshot[index].size() != getLayerParamSpecs().size())
        return false;
    juce::ValueTree waveState ("NSYNTH_WAVE_STATE");
    waveState.addChild (patchWaveCompareSnapshot[index].createCopy(), -1, nullptr);
    if (! engine.restoreWaveBankState (waveState))
        return false;
    applyLayerValues (layer, patchCompareSnapshot[index]);
    engine.modMatrix (layer).restoreSnapshot (patchMatrixCompareSnapshot[index]);
    patchMatrixCompareSnapshot[index] = {};
    patchWaveCompareSnapshot[index] = {};
    patchCompareSnapshot[index].clear();
    patchCompareActive[index] = false;
    return true;
}

bool NSynthAudioProcessor::isPatchCompareActive (Layer layer) const noexcept
{
    return patchCompareActive[(size_t) toInt (layer)];
}

juce::String NSynthAudioProcessor::getPatchName (Layer layer) const
{
    return patchNames[(size_t) toInt (layer)];
}

bool NSynthAudioProcessor::loadPerformance (const juce::File& file, juce::String* error)
{
    juce::ValueTree tree (apvts.state.getType());
    if (! presetManager->loadFromFile (tree, file, error))
        return false;

    if (! tree.hasType (apvts.state.getType()))
    {
        if (error) *error = "Not an nSynth performance file.";
        return false;
    }

    if (! engine.restoreMidiInputState (tree, error))
        return false;
    if (! engine.restoreModMatrixState (tree, error))
        return false;
    if (! engine.restoreWaveBankState (tree, error))
        return false;
    removeLegacyChildren (tree);
    const float masterVolume = apvts.getParameter (perfParamId (pid::masterVolume))->getValue();
    apvts.replaceState (tree);
    if (auto* masterParameter = apvts.getParameter (perfParamId (pid::masterVolume)))
        masterParameter->setValueNotifyingHost (masterVolume);
    currentPresetName = file.getFileNameWithoutExtension();
    for (auto layer : { Layer::upper, Layer::lower })
    {
        const auto index = (size_t) toInt (layer);
        patchBaseline[index] = captureLayerValues (layer);
        patchMatrixBaseline[index] = *engine.modMatrix (layer).snapshot();
        patchWaveBaseline[index] = getWaveBankState (tree);
        if (! patchWaveBaseline[index].isValid())
            patchWaveBaseline[index] = engine.getWaveBank().toValueTree();
        patchNames[index] = "Performance";
        patchCompareActive[index] = false;
    }
    return true;
}

bool NSynthAudioProcessor::savePerformance (const juce::File& file, juce::String* error)
{
    auto state = apvts.copyState();
    engine.appendModMatrixState (state);
    engine.appendWaveBankState (state);
    engine.appendMidiInputState (state);
    state.setProperty ("stateVersion", 6, nullptr);
    if (! presetManager->saveToFile (state, file, error))
        return false;
    currentPresetName = file.getFileNameWithoutExtension();
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NSynthAudioProcessor();
}
