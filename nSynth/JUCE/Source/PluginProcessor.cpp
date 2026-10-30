#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace nsynth;

namespace
{
juce::File userRootDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("nBundleAudio").getChildFile ("nSynth");
}

bool sameValues (const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs (a[i] - b[i]) > 1.0e-6f)
            return false;
    return true;
}

bool sameWaves (const WaveBankPtr& a, const WaveBankPtr& b)
{
    if (a == b)
        return true;
    return a != nullptr && b != nullptr && a->getContentHash() == b->getContentHash();
}
} // namespace

bool NSynthAudioProcessor::LayerState::sameAs (const LayerState& other) const
{
    return sameValues (values, other.values) && matrix.sameContent (other.matrix) && sameWaves (waves, other.waves);
}

bool NSynthAudioProcessor::FullState::sameAs (const FullState& other) const
{
    return sameValues (values, other.values)
        && matrices[0].sameContent (other.matrices[0]) && matrices[1].sameContent (other.matrices[1])
        && sameWaves (waves[0], other.waves[0]) && sameWaves (waves[1], other.waves[1]);
}

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
        for (const auto& spec : getLayerParamSpecs())
        {
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (layerParamId (layer, spec.suffix)));
            jassert (parameter != nullptr);
            layerParameters[(size_t) toInt (layer)].push_back (parameter);
        }
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            allParameters.push_back (ranged);

    for (auto layer : { Layer::upper, Layer::lower })
        patchBaseline[(size_t) toInt (layer)] = captureLayerState (layer);
    performanceBaseline = captureFullState();
    lastObservedState = performanceBaseline;
    undoHistory.push_back (performanceBaseline);

    // Factory presets are compiled into the binary (PresetLibrary); only user presets live on disk.
    patchDirectory = presetLibrary.getUserDirectory (nSynth::PresetKind::patch);
    patchDirectory.createDirectory();
    const auto performanceDirectory = presetLibrary.getUserDirectory (nSynth::PresetKind::performance);
    presetManager = std::make_unique<nSynth::PresetManager> (performanceDirectory, performanceDirectory, ".nsynthperf");

    for (const auto& entry : nSynth::PresetLibrary::getFactoryEntries())
        if (entry.kind == nSynth::PresetKind::performance)
            factoryPerformances.push_back (entry);
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
    constexpr int workers = 0;
#else
    constexpr int workers = -1;
#endif
    engine.prepare (sampleRate, samplesPerBlock, workers, chooseOversamplingFactor());
    setLatencySamples ((int) std::lround (engine.getLatencySamples()));
}

int NSynthAudioProcessor::chooseOversamplingFactor() const noexcept
{
    return isNonRealtime() && offlineHighQuality.load() ? 4 : oversamplingQuality.load();
}

void NSynthAudioProcessor::setOversamplingQuality (int factor)
{
    oversamplingQuality = factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    applyOversamplingChange();
}

void NSynthAudioProcessor::setOfflineHighQuality (bool enabled)
{
    offlineHighQuality = enabled;
    applyOversamplingChange();
}

void NSynthAudioProcessor::applyOversamplingChange()
{
    // Re-prepares the engine with the audio callback held off (suspendProcessing takes the
    // callback lock), so processBlock never sees half-resized buffers.
    if (getSampleRate() <= 0.0 || chooseOversamplingFactor() == engine.getOversamplingFactor())
        return;
    suspendProcessing (true);
    prepareToPlay (getSampleRate(), getBlockSize());
    suspendProcessing (false);
}

void NSynthAudioProcessor::audioWorkgroupContextChanged (const juce::AudioWorkgroup& workgroup)
{
    engine.setAudioWorkgroup (workgroup);
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

    for (auto l : { Layer::upper, Layer::lower })
        activeVoices[(size_t) toInt (l)] = engine.getActiveVoices (l);

    bool needsMessageThread = engine.hasPendingMidiParameterChanges() || engine.hasPendingProgramChange();
    needsMessageThread = engine.takeGarbageNotice() || needsMessageThread;
    const int latency = (int) std::lround (engine.getLatencySamples());
    if (latency != getLatencySamples() && pendingLatency.exchange (latency) != latency)
        needsMessageThread = true;
    if (needsMessageThread)
        triggerAsyncUpdate();
}

void NSynthAudioProcessor::handleAsyncUpdate()
{
    nsynth::SynthEngine::MidiParameterChange change;
    while (engine.popMidiParameterChange (change))
        if (change.parameter != nullptr)
            change.parameter->setValueNotifyingHost (change.normalizedValue);
    if (pendingLatency.load() != getLatencySamples())
        setLatencySamples (pendingLatency.load());

    const int midiProgram = engine.takeProgramChange();
    if (midiProgram >= 0)
        loadProgram (midiProgram + 1);
    const int hostProgram = pendingProgram.exchange (-1);
    if (hostProgram >= 0)
        loadProgram (hostProgram);

    engine.collectGarbage();
}

//==============================================================================
void NSynthAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        loadProgram (index);
        return;
    }
    pendingProgram = index;
    triggerAsyncUpdate();
}

const juce::String NSynthAudioProcessor::getProgramName (int index)
{
    if (index == 0)
        return "Current";
    return juce::isPositiveAndBelow (index - 1, (int) factoryPerformances.size())
               ? factoryPerformances[(size_t) (index - 1)].name : juce::String();
}

void NSynthAudioProcessor::loadProgram (int index)
{
    // Program 0 is whatever is loaded: selecting it (hosts do on start-up) changes nothing.
    if (index <= 0 || index > (int) factoryPerformances.size())
        return;
    if (loadPerformance (factoryPerformances[(size_t) (index - 1)]))
        currentProgram = index;
}

//==============================================================================
juce::AudioProcessorEditor* NSynthAudioProcessor::createEditor()
{
    return new NSynthAudioProcessorEditor (*this);
}

void NSynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    auto state = apvts.copyState();
    engine.appendModMatrixState (state);
    engine.appendWaveBankState (state);
    engine.appendMidiInputState (state);
    state.setProperty ("editorScale", editorScale.load(), nullptr);
    state.setProperty ("oversampling", oversamplingQuality.load(), nullptr);
    state.setProperty ("offlineHQ", offlineHighQuality.load(), nullptr);
    state.setProperty ("presetName", currentPresetName, nullptr);
    state.setProperty ("presetKey", performanceKey, nullptr);
    state.setProperty ("upperPatch", patchNames[0], nullptr);
    state.setProperty ("lowerPatch", patchNames[1], nullptr);
    state.setProperty ("stateVersion", kStateVersion, nullptr);
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

    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    const auto metadata = tree.createCopy();   // restoreFullState() strips the display properties
    if (! restoreFullState (tree, false, nullptr))
        return;

    editorScale = (float) metadata.getProperty ("editorScale", 1.0f);
    const int factor = (int) metadata.getProperty ("oversampling", SynthLayer::kDefaultOversamplingFactor);
    oversamplingQuality = factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    offlineHighQuality = (bool) metadata.getProperty ("offlineHQ", true);
    applyOversamplingChange();
    currentPresetName = metadata.getProperty ("presetName", "Init").toString();
    performanceKey = metadata.getProperty ("presetKey", {}).toString();
    resetBaselines (metadata, "Session");
}

//==============================================================================
void NSynthAudioProcessor::setParameterWithGesture (juce::RangedAudioParameter& parameter, float normalisedValue)
{
    if (juce::exactlyEqual (parameter.getValue(), normalisedValue))
        return;
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (normalisedValue);
    parameter.endChangeGesture();
}

std::vector<float> NSynthAudioProcessor::captureLayerValues (Layer layer) const
{
    const auto& parameters = layerParameters[(size_t) toInt (layer)];
    std::vector<float> values;
    values.reserve (parameters.size());
    for (auto* parameter : parameters)
        values.push_back (parameter->getValue());
    return values;
}

void NSynthAudioProcessor::applyLayerValues (Layer layer, const std::vector<float>& values)
{
    const auto& parameters = layerParameters[(size_t) toInt (layer)];
    if (values.size() != parameters.size())
        return;
    for (size_t i = 0; i < values.size(); ++i)
        setParameterWithGesture (*parameters[i], values[i]);
}

NSynthAudioProcessor::LayerState NSynthAudioProcessor::captureLayerState (Layer layer) const
{
    return { captureLayerValues (layer), *engine.modMatrix (layer).snapshot(), engine.getWaveBank (layer) };
}

void NSynthAudioProcessor::applyLayerState (Layer layer, const LayerState& state)
{
    applyLayerValues (layer, state.values);
    engine.modMatrix (layer).restoreSnapshot (state.matrix);
    engine.setWaveBank (layer, state.waves);
}

NSynthAudioProcessor::FullState NSynthAudioProcessor::captureFullState() const
{
    FullState state;
    state.values.reserve (allParameters.size());
    for (auto* parameter : allParameters)
        state.values.push_back (parameter->getValue());
    for (auto layer : { Layer::upper, Layer::lower })
    {
        state.matrices[(size_t) toInt (layer)] = *engine.modMatrix (layer).snapshot();
        state.waves[(size_t) toInt (layer)] = engine.getWaveBank (layer);
    }
    return state;
}

void NSynthAudioProcessor::applyFullState (const FullState& state)
{
    if (state.values.size() == allParameters.size())
        for (size_t i = 0; i < allParameters.size(); ++i)
            setParameterWithGesture (*allParameters[i], state.values[i]);
    for (auto layer : { Layer::upper, Layer::lower })
    {
        engine.modMatrix (layer).restoreSnapshot (state.matrices[(size_t) toInt (layer)]);
        engine.setWaveBank (layer, state.waves[(size_t) toInt (layer)]);
    }
}

bool NSynthAudioProcessor::validateParameterValues (const juce::ValueTree& tree, juce::String* error) const
{
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild (i);
        if (! child.hasType ("PARAM"))
            continue;
        // Parameters that no longer exist are ignored by the APVTS; known ones need a finite number.
        if (apvts.getParameter (child.getProperty ("id").toString()) == nullptr)
            continue;
        if (! std::isfinite ((double) child.getProperty ("value", std::numeric_limits<double>::quiet_NaN())))
        {
            if (error) *error = "State contains an invalid value for parameter " + child.getProperty ("id").toString() + ".";
            return false;
        }
    }
    return true;
}

bool NSynthAudioProcessor::restoreFullState (juce::ValueTree tree, bool keepMasterVolume, juce::String* error)
{
    if ((int) tree.getProperty ("stateVersion", 0) > kStateVersion)
    {
        if (error) *error = "This state was saved by a newer nSynth version.";
        return false;
    }

    // Validate and build everything first: a broken file leaves the current sound untouched.
    std::array<ModMatrixData, 2> matrices;
    std::array<WaveBankPtr, 2> banks;
    if (! engine.restoreMidiInputState (tree, error, false)
        || ! engine.stageModMatrixState (tree, matrices, error)
        || ! engine.stageWaveBankState (tree, banks, error)
        || ! validateParameterValues (tree, error))
        return false;

    removeLegacyChildren (tree);
    // Display metadata of the file is not plugin state.
    for (auto property : { "name", "category", "presetKey", "presetName", "upperPatch", "lowerPatch", "editorScale",
                           "oversampling", "offlineHQ" })
        tree.removeProperty (property, nullptr);

    auto* masterParameter = apvts.getParameter (perfParamId (pid::masterVolume));
    const float masterVolume = masterParameter != nullptr ? masterParameter->getValue() : 0.0f;

    const SynthEngine::ScopedStateChange change (engine, SynthEngine::allScopes);
    engine.restoreMidiInputState (tree);
    apvts.replaceState (tree);
    if (keepMasterVolume && masterParameter != nullptr)
        setParameterWithGesture (*masterParameter, masterVolume);
    engine.applyModMatrixState (matrices);
    engine.setWaveBank (Layer::upper, banks[0]);
    engine.setWaveBank (Layer::lower, banks[1]);
    return true;
}

void NSynthAudioProcessor::resetBaselines (const juce::ValueTree& tree, const juce::String& fallbackPatchName)
{
    for (auto layer : { Layer::upper, Layer::lower })
    {
        const auto index = (size_t) toInt (layer);
        patchBaseline[index] = captureLayerState (layer);
        patchNames[index] = tree.getProperty (layer == Layer::upper ? "upperPatch" : "lowerPatch", fallbackPatchName).toString();
        patchCategories[index] = {};
        patchKeys[index] = {};
        patchCompareActive[index] = false;
        patchCompareSnapshot[index] = {};
    }
    performanceBaseline = captureFullState();
}

void NSynthAudioProcessor::removeLegacyChildren (juce::ValueTree& root)
{
    // Sequencer banks from sessions saved before the sequencer was removed.
    for (int i = root.getNumChildren() - 1; i >= 0; --i)
        if (root.getChild (i).hasType ("SEQUENCER"))
            root.removeChild (i, nullptr);
}

//==============================================================================
void NSynthAudioProcessor::initLayer (Layer layer)
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    {
        const SynthEngine::ScopedStateChange change (engine, SynthEngine::scopeOf (layer));
        for (auto* parameter : layerParameters[(size_t) toInt (layer)])
            setParameterWithGesture (*parameter, parameter->getDefaultValue());
        engine.modMatrix (layer).clearAll();
        engine.setWaveBank (layer, WaveBank::factory());
    }
    const auto index = (size_t) toInt (layer);
    patchNames[index] = "Init";
    patchCategories[index] = {};
    patchKeys[index] = {};
    patchBaseline[index] = captureLayerState (layer);
    patchCompareActive[index] = false;
}

void NSynthAudioProcessor::copyLayer (Layer from, Layer to)
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    const auto source = captureLayerState (from);
    {
        const SynthEngine::ScopedStateChange change (engine, SynthEngine::scopeOf (to));
        applyLayerState (to, source);
    }
    const auto target = (size_t) toInt (to), origin = (size_t) toInt (from);
    patchNames[target] = patchNames[origin];
    patchCategories[target] = patchCategories[origin];
    patchKeys[target] = patchKeys[origin];
    // The copy is as clean or dirty as the source: it shares the source's saved baseline.
    patchBaseline[target] = patchBaseline[origin];
    patchCompareActive[target] = false;
}

bool NSynthAudioProcessor::loadPatch (Layer layer, const juce::File& file, juce::String* error)
{
    juce::ValueTree tree;
    if (! presetManager->loadFromFile (tree, file, error))
        return false;
    const auto name = tree.getProperty ("name", nSynth::PresetLibrary::displayNameFromFileName (file.getFileNameWithoutExtension())).toString();
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
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
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
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
    juce::ValueTree waveTree;
    const auto& specs = getLayerParamSpecs();
    std::vector<float> values (specs.size(), 0.0f);
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
            if (waveTree.isValid() || ! WaveBank::isValidValueTree (child, error))
            {
                if (error && error->isEmpty())
                    *error = "Patch contains an invalid or duplicate user wavetable bank.";
                return false;
            }
            waveTree = child;
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
        for (size_t i = 0; i < specs.size(); ++i)
        {
            if (suffix != specs[i].suffix)
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
    const auto& parameters = layerParameters[(size_t) toInt (layer)];
    for (size_t i = 0; i < found.size(); ++i)
        if (! found[i])
            values[i] = parameters[i]->getDefaultValue();

    // A patch without USER_WAVES plays the factory waveforms. The bank is built before the
    // layer is suspended, so a slow build never stretches the silence.
    const auto bank = engine.buildWaveBank (waveTree, error);
    if (bank == nullptr)
        return false;

    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    {
        const SynthEngine::ScopedStateChange change (engine, SynthEngine::scopeOf (layer));
        applyLayerValues (layer, values);
        if (hasMatrix)
            engine.modMatrix (layer).restoreSnapshot (*stagedMatrix.snapshot());
        else
            engine.modMatrix (layer).clearAll();
        engine.setWaveBank (layer, bank);
    }
    const auto index = (size_t) toInt (layer);
    patchBaseline[index] = captureLayerState (layer);
    patchNames[index] = name;
    patchCategories[index] = tree.getProperty ("category", {}).toString();
    patchKeys[index] = {};
    patchCompareActive[index] = false;
    patchCompareSnapshot[index] = {};
    return true;
}

bool NSynthAudioProcessor::savePatch (Layer layer, const juce::File& file, juce::String* error)
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    const auto target = file.withFileExtension ("nsynthpatch");
    const auto name = nSynth::PresetLibrary::displayNameFromFileName (target.getFileNameWithoutExtension());
    const auto index = (size_t) toInt (layer);
    juce::ValueTree tree ("NSYNTH_PATCH");
    tree.setProperty ("layer", toInt (layer), nullptr);
    tree.setProperty ("stateVersion", kPatchStateVersion, nullptr);
    tree.setProperty ("name", name, nullptr);
    if (patchCategories[index].isNotEmpty())
        tree.setProperty ("category", patchCategories[index], nullptr);
    tree.addChild (engine.modMatrix (layer).toValueTree (toInt (layer)), -1, nullptr);
    engine.appendLayerWaveBankState (tree, layer);
    const auto& specs = getLayerParamSpecs();
    const auto& parameters = layerParameters[index];
    for (size_t i = 0; i < specs.size(); ++i)
    {
        juce::ValueTree child ("PARAM");
        child.setProperty ("id", layerParamId (layer, specs[i].suffix), nullptr);
        child.setProperty ("value", parameters[i]->getValue(), nullptr);
        tree.addChild (child, -1, nullptr);
    }
    if (! presetManager->saveToFile (tree, target, error))
        return false;

    patchBaseline[index] = captureLayerState (layer);
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
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    const auto index = (size_t) toInt (layer);
    // Cheap: cached parameter pointers, one matrix comparison and the wave banks' content hashes.
    if (! sameValues (captureLayerValues (layer), patchBaseline[index].values))
        return true;
    if (! engine.modMatrix (layer).snapshot()->sameContent (patchBaseline[index].matrix))
        return true;
    return ! sameWaves (engine.getWaveBank (layer), patchBaseline[index].waves);
}

bool NSynthAudioProcessor::isPerformanceDirty() const
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    return ! captureFullState().sameAs (performanceBaseline);
}

bool NSynthAudioProcessor::setPatchCompare (Layer layer, bool enabled)
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    const auto index = (size_t) toInt (layer);
    if (enabled == patchCompareActive[index])
        return true;

    if (enabled)
    {
        if (patchBaseline[index].values.size() != layerParameters[index].size())
            return false;
        patchCompareSnapshot[index] = captureLayerState (layer);
        applyLayerState (layer, patchBaseline[index]);
        patchCompareActive[index] = true;
        return true;
    }

    if (patchCompareSnapshot[index].values.size() != layerParameters[index].size())
        return false;
    applyLayerState (layer, patchCompareSnapshot[index]);
    patchCompareSnapshot[index] = {};
    patchCompareActive[index] = false;
    return true;
}

bool NSynthAudioProcessor::isPatchCompareActive (Layer layer) const noexcept
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    return patchCompareActive[(size_t) toInt (layer)];
}

juce::String NSynthAudioProcessor::getPatchName (Layer layer) const
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    return patchNames[(size_t) toInt (layer)];
}

juce::String NSynthAudioProcessor::getPatchCategory (Layer layer) const
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    return patchCategories[(size_t) toInt (layer)];
}

juce::String NSynthAudioProcessor::getPatchKey (Layer layer) const
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    return patchKeys[(size_t) toInt (layer)];
}

juce::String NSynthAudioProcessor::getPerformanceKey() const
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    return performanceKey;
}

juce::String NSynthAudioProcessor::getCurrentPresetName() const
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    return currentPresetName;
}

//==============================================================================
bool NSynthAudioProcessor::loadPerformance (const juce::File& file, juce::String* error)
{
    juce::ValueTree tree (apvts.state.getType());
    if (! presetManager->loadFromFile (tree, file, error))
        return false;
    const auto name = tree.getProperty ("name", nSynth::PresetLibrary::displayNameFromFileName (file.getFileNameWithoutExtension())).toString();
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
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
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
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
    const auto patchNameTree = tree.createCopy();
    if (! restoreFullState (tree, true, error))
        return false;
    currentPresetName = name;
    performanceKey = {};
    resetBaselines (patchNameTree, "Performance");
    return true;
}

bool NSynthAudioProcessor::savePerformance (const juce::File& file, juce::String* error)
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    auto state = apvts.copyState();
    engine.appendModMatrixState (state);
    engine.appendWaveBankState (state);
    engine.appendMidiInputState (state);
    state.setProperty ("stateVersion", kStateVersion, nullptr);
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
    performanceBaseline = captureFullState();
    presetLibrary.rescan();
    return true;
}

//==============================================================================
void NSynthAudioProcessor::captureUndoPoint()
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    auto current = captureFullState();
    const bool settled = current.sameAs (lastObservedState);
    lastObservedState = current;
    if (! settled || current.sameAs (undoHistory[undoPosition]))
        return;

    undoHistory.resize (undoPosition + 1);   // a new edit drops the redo steps
    undoHistory.push_back (std::move (current));
    if (undoHistory.size() > kMaxUndoSteps + 1)
        undoHistory.erase (undoHistory.begin());
    undoPosition = undoHistory.size() - 1;
}

bool NSynthAudioProcessor::undo()
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    // An edit that has not been recorded yet becomes a step first, so undo returns to the state before it.
    auto current = captureFullState();
    if (! current.sameAs (undoHistory[undoPosition]))
    {
        undoHistory.resize (undoPosition + 1);
        undoHistory.push_back (std::move (current));
        undoPosition = undoHistory.size() - 1;
    }
    if (undoPosition == 0)
        return false;
    --undoPosition;
    applyFullState (undoHistory[undoPosition]);
    lastObservedState = undoHistory[undoPosition];
    return true;
}

bool NSynthAudioProcessor::redo()
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    if (undoPosition + 1 >= undoHistory.size())
        return false;
    ++undoPosition;
    applyFullState (undoHistory[undoPosition]);
    lastObservedState = undoHistory[undoPosition];
    return true;
}

bool NSynthAudioProcessor::canUndo() const
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    return undoPosition > 0 || ! captureFullState().sameAs (undoHistory[undoPosition]);
}

bool NSynthAudioProcessor::canRedo() const
{
    const std::lock_guard<std::recursive_mutex> guard (stateLock);
    return undoPosition + 1 < undoHistory.size();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NSynthAudioProcessor();
}
