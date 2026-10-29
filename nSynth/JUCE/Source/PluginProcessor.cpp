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

juce::File userRootDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("nBundleAudio").getChildFile ("nSynth");
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
      engine (apvts),
      presetLibrary (userRootDirectory())
{
    for (auto layer : { Layer::upper, Layer::lower })
    {
        patchBaseline[(size_t) toInt (layer)] = captureLayerValues (layer);
        patchMatrixBaseline[(size_t) toInt (layer)] = *engine.modMatrix (layer).snapshot();
        patchWaveBaseline[(size_t) toInt (layer)] = engine.getWaveBank().toValueTree();
    }

    // Factory presets are compiled into the binary (PresetLibrary); only user presets live on disk.
    patchDirectory = presetLibrary.getUserDirectory (nSynth::PresetKind::patch);
    patchDirectory.createDirectory();
    const auto performanceDirectory = presetLibrary.getUserDirectory (nSynth::PresetKind::performance);
    presetManager = std::make_unique<nSynth::PresetManager> (performanceDirectory, performanceDirectory, ".nsynthperf");
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
    state.setProperty ("presetKey", performanceKey, nullptr);
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
        patchCategories[index] = {};
        patchKeys[index] = {};
        patchCompareActive[index] = false;
    }
    performanceKey = tree.getProperty ("presetKey", {}).toString();
}

void NSynthAudioProcessor::initLayer (Layer layer)
{
    for (auto& spec : getLayerParamSpecs())
        if (auto* p = apvts.getParameter (layerParamId (layer, spec.suffix)))
            p->setValueNotifyingHost (p->getDefaultValue());
    patchNames[(size_t) toInt (layer)] = "Init";
    patchCategories[(size_t) toInt (layer)] = {};
    patchKeys[(size_t) toInt (layer)] = {};
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
    patchNames[(size_t) toInt (to)] = patchNames[(size_t) toInt (from)];
    patchCategories[(size_t) toInt (to)] = patchCategories[(size_t) toInt (from)];
    patchKeys[(size_t) toInt (to)] = patchKeys[(size_t) toInt (from)];
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
    const auto name = tree.getProperty ("name", nSynth::PresetLibrary::displayNameFromFileName (file.getFileNameWithoutExtension())).toString();
    if (! loadPatchState (layer, tree, name, error))
        return false;
    nSynth::PresetEntry entry;
    entry.kind = nSynth::PresetKind::patch;
    entry.factory = false;
    entry.file = file;
    patchKeys[(size_t) toInt (layer)] = entry.key();
    return true;
}

bool NSynthAudioProcessor::loadPatch (Layer layer, const nSynth::PresetEntry& entry, juce::String* error)
{
    if (entry.kind != nSynth::PresetKind::patch)
    {
        if (error) *error = "\"" + entry.name + "\" is not a layer patch.";
        return false;
    }
    const auto xml = entry.readXml (error);
    if (xml == nullptr)
        return false;
    if (! loadPatchState (layer, juce::ValueTree::fromXml (*xml), entry.name, error))
        return false;
    patchCategories[(size_t) toInt (layer)] = entry.category;
    patchKeys[(size_t) toInt (layer)] = entry.key();
    return true;
}

bool NSynthAudioProcessor::loadPatchState (Layer layer, const juce::ValueTree& tree, const juce::String& name,
                                           juce::String* error)
{
    if (! tree.hasType ("NSYNTH_PATCH"))
    {
        if (error) *error = "Not an nSynth layer patch.";
        return false;
    }

    ModMatrix stagedMatrix;
    bool hasMatrix = false;
    bool hasWaveBank = false;
    juce::ValueTree stagedWaveBank;
    std::vector<float> values (getLayerParamSpecs().size(), 0.0f);
    std::vector<bool> found (values.size(), false);
    // All parameters of a patch belong to the layer it was saved from ("up_" or "lo_");
    // they are mapped onto the target layer by suffix.
    juce::String sourcePrefix;
    for (int childIndex = 0; childIndex < tree.getNumChildren(); ++childIndex)
    {
        const auto child = tree.getChild (childIndex);
        if (child.hasType ("MOD_MATRIX"))
        {
            if (hasMatrix || ! stagedMatrix.restoreFromValueTree (child, error))
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
        juce::String prefix;
        for (auto candidate : { Layer::upper, Layer::lower })
            if (id.startsWith (layerPrefix (candidate)))
                prefix = layerPrefix (candidate);
        if (prefix.isEmpty() || (sourcePrefix.isNotEmpty() && prefix != sourcePrefix))
        {
            if (error) *error = "Patch mixes parameters of both layers or contains an unknown parameter.";
            return false;
        }
        sourcePrefix = prefix;

        const auto suffix = id.substring (prefix.length());
        for (size_t i = 0; i < getLayerParamSpecs().size(); ++i)
        {
            if (suffix != getLayerParamSpecs()[i].suffix)
                continue;
            const float value = (float) child.getProperty ("value", -1.0f);
            if (found[i] || ! std::isfinite (value) || value < 0.0f || value > 1.0f)
            {
                if (error) *error = "Patch contains a duplicate or invalid parameter value.";
                return false;
            }
            values[i] = value;
            found[i] = true;
            break;
        }
        // Parameters that no longer exist (arp/sequencer, old FX) are skipped.
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
    patchNames[index] = name;
    patchCategories[index] = tree.getProperty ("category", {}).toString();
    patchKeys[index] = {};
    patchCompareActive[index] = false;
    return true;
}

bool NSynthAudioProcessor::savePatch (Layer layer, const juce::File& file, juce::String* error)
{
    const auto target = file.withFileExtension ("nsynthpatch");
    const auto name = nSynth::PresetLibrary::displayNameFromFileName (target.getFileNameWithoutExtension());
    juce::ValueTree tree ("NSYNTH_PATCH");
    tree.setProperty ("layer", toInt (layer), nullptr);
    tree.setProperty ("stateVersion", 4, nullptr);
    tree.setProperty ("name", name, nullptr);
    if (patchCategories[(size_t) toInt (layer)].isNotEmpty())
        tree.setProperty ("category", patchCategories[(size_t) toInt (layer)], nullptr);
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
    if (! presetManager->saveToFile (tree, target, error))
        return false;

    const auto index = (size_t) toInt (layer);
    patchBaseline[index] = captureLayerValues (layer);
    patchMatrixBaseline[index] = *engine.modMatrix (layer).snapshot();
    patchWaveBaseline[index] = engine.getWaveBank().toValueTree();
    patchNames[index] = name;
    nSynth::PresetEntry entry;
    entry.kind = nSynth::PresetKind::patch;
    entry.factory = false;
    entry.file = target;
    patchKeys[index] = entry.key();
    patchCompareActive[index] = false;
    presetLibrary.rescan();
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
    if (! currentMatrix.sameContent (baselineMatrix))
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
    const auto name = tree.getProperty ("name", nSynth::PresetLibrary::displayNameFromFileName (file.getFileNameWithoutExtension())).toString();
    if (! loadPerformanceState (tree, name, error))
        return false;
    nSynth::PresetEntry entry;
    entry.kind = nSynth::PresetKind::performance;
    entry.factory = false;
    entry.file = file;
    performanceKey = entry.key();
    return true;
}

bool NSynthAudioProcessor::loadPerformance (const nSynth::PresetEntry& entry, juce::String* error)
{
    if (entry.kind != nSynth::PresetKind::performance)
    {
        if (error) *error = "\"" + entry.name + "\" is not a performance.";
        return false;
    }
    const auto xml = entry.readXml (error);
    if (xml == nullptr)
        return false;
    if (! loadPerformanceState (juce::ValueTree::fromXml (*xml), entry.name, error))
        return false;
    performanceKey = entry.key();
    return true;
}

bool NSynthAudioProcessor::loadPerformanceState (juce::ValueTree tree, const juce::String& name, juce::String* error)
{
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
    // Display metadata of the file is not plugin state.
    for (auto property : { "name", "category", "presetKey" })
        tree.removeProperty (property, nullptr);
    const float masterVolume = apvts.getParameter (perfParamId (pid::masterVolume))->getValue();
    apvts.replaceState (tree);
    if (auto* masterParameter = apvts.getParameter (perfParamId (pid::masterVolume)))
        masterParameter->setValueNotifyingHost (masterVolume);
    currentPresetName = name;
    performanceKey = {};
    for (auto layer : { Layer::upper, Layer::lower })
    {
        const auto index = (size_t) toInt (layer);
        patchBaseline[index] = captureLayerValues (layer);
        patchMatrixBaseline[index] = *engine.modMatrix (layer).snapshot();
        patchWaveBaseline[index] = getWaveBankState (tree);
        if (! patchWaveBaseline[index].isValid())
            patchWaveBaseline[index] = engine.getWaveBank().toValueTree();
        patchNames[index] = tree.getProperty (layer == Layer::upper ? "upperPatch" : "lowerPatch", "Performance").toString();
        patchCategories[index] = {};
        patchKeys[index] = {};
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
    const auto name = nSynth::PresetLibrary::displayNameFromFileName (file.getFileNameWithoutExtension());
    state.setProperty ("name", name, nullptr);
    state.setProperty ("upperPatch", patchNames[0], nullptr);
    state.setProperty ("lowerPatch", patchNames[1], nullptr);
    if (! presetManager->saveToFile (state, file, error))
        return false;
    currentPresetName = name;
    nSynth::PresetEntry entry;
    entry.kind = nSynth::PresetKind::performance;
    entry.factory = false;
    entry.file = file;
    performanceKey = entry.key();
    presetLibrary.rescan();
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NSynthAudioProcessor();
}
