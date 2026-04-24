#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace
{
static constexpr std::array<const char*, 23> kObservedParamIds = {
    NComp::params::link,
    NComp::params::input1,
    NComp::params::input2,
    NComp::params::output1,
    NComp::params::output2,
    NComp::params::threshold1,
    NComp::params::threshold2,
    NComp::params::ratio1,
    NComp::params::ratio2,
    NComp::params::attack1,
    NComp::params::attack2,
    NComp::params::release1,
    NComp::params::release2,
    NComp::params::mix,
    NComp::params::character,
    NComp::params::mslr,
    NComp::params::power,
    NComp::params::autoGain,
    NComp::params::sidechain,
    NComp::params::sidechainListen,
    NComp::params::linkAmount,
    NComp::params::sidechainHpf,
    NComp::params::sidechainTilt,
};

static constexpr std::array<std::pair<const char*, const char*>, 6> kLinkedPairs = {
    std::pair { NComp::params::input1, NComp::params::input2 },
    std::pair { NComp::params::output1, NComp::params::output2 },
    std::pair { NComp::params::threshold1, NComp::params::threshold2 },
    std::pair { NComp::params::ratio1, NComp::params::ratio2 },
    std::pair { NComp::params::attack1, NComp::params::attack2 },
    std::pair { NComp::params::release1, NComp::params::release2 }
};

static constexpr const char* kStateRootType = "NCOMP_STATE";
static constexpr const char* kStateRoleProperty = "__NComp_role";
static constexpr const char* kStateRoleCurrent = "current";
static constexpr const char* kStateProgramProperty = "__NComp_programIndex";
static constexpr const char* kStatePresetKindProperty = "__NComp_presetKind";
static constexpr const char* kStatePresetPathProperty = "__NComp_presetPath";
static constexpr const char* kStatePresetDirtyProperty = "__NComp_presetDirty";
static constexpr const char* kPresetFileExtension = ".ncompreset";
static constexpr int kNumFactoryPresets = 5;
static constexpr const char* kApvtsValueNodeType = "PARAM";
static constexpr const char* kApvtsValueProperty = "value";
static constexpr const char* kApvtsIdProperty = "id";

static constexpr std::array<const char*, 26> kPresetParamIds = {
    NComp::params::input1,
    NComp::params::input2,
    NComp::params::output1,
    NComp::params::output2,
    NComp::params::threshold1,
    NComp::params::threshold2,
    NComp::params::ratio1,
    NComp::params::ratio2,
    NComp::params::attack1,
    NComp::params::attack2,
    NComp::params::release1,
    NComp::params::release2,
    NComp::params::mix,
    NComp::params::character,
    NComp::params::linkAmount,
    NComp::params::sidechainHpf,
    NComp::params::sidechainTilt,
    NComp::params::link,
    NComp::params::mslr,
    NComp::params::power,
    NComp::params::autoGain,
    NComp::params::ab,
    NComp::params::sidechain,
    NComp::params::sidechainListen,
    NComp::params::meterL,
    NComp::params::meterR,
};

juce::File normalisePresetFile(const juce::File& file)
{
    if (file.hasFileExtension(kPresetFileExtension))
        return file;

    return file.withFileExtension(kPresetFileExtension);
}

juce::File defaultPresetDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("Audio Music Apps")
        .getChildFile("nComp")
        .getChildFile("Presets");
}

juce::String presetDisplayName(const juce::File& presetFile)
{
    const auto presetDirectory = defaultPresetDirectory();
    auto relativePath = presetFile.getRelativePathFrom(presetDirectory);

    if (relativePath.isEmpty())
        relativePath = presetFile.getFileName();

    if (relativePath.endsWithIgnoreCase(kPresetFileExtension))
        relativePath = relativePath.dropLastCharacters(juce::String(kPresetFileExtension).length());

    return relativePath.replaceCharacter('\\', '/');
}

juce::String getPresetNameForIndex(int index)
{
    switch (index)
    {
        case 0: return "Manual";
        case 1: return "Mix Glue";
        case 2: return "Drum Bus";
        case 3: return "Vocal Lift";
        case 4: return "Parallel Punch";
        case 5: return "Rhythm Duck";
        default: return {};
    }
}

template <typename SetFloatFn, typename SetChoiceFn>
void applyPresetValues(int index, SetFloatFn&& setFloat, SetChoiceFn&& setChoice)
{
    setChoice(NComp::params::power, 0);
    setChoice(NComp::params::sidechainListen, 0);
    setChoice(NComp::params::autoGain, 0);
    setFloat(NComp::params::character, 0.0f);

    switch (index)
    {
        case 1:
            setChoice(NComp::params::link, 0);
            setChoice(NComp::params::mslr, 0);
            setChoice(NComp::params::sidechain, 0);
            setFloat(NComp::params::input1, 0.3f);
            setFloat(NComp::params::input2, 0.3f);
            setFloat(NComp::params::output1, 0.6f);
            setFloat(NComp::params::output2, 0.6f);
            setFloat(NComp::params::threshold1, -16.4f);
            setFloat(NComp::params::threshold2, -16.4f);
            setFloat(NComp::params::ratio1, 2.0f);
            setFloat(NComp::params::ratio2, 2.0f);
            setFloat(NComp::params::attack1, 42.0f);
            setFloat(NComp::params::attack2, 42.0f);
            setFloat(NComp::params::release1, 440.0f);
            setFloat(NComp::params::release2, 440.0f);
            setFloat(NComp::params::mix, 100.0f);
            setFloat(NComp::params::character, 4.0f);
            setFloat(NComp::params::linkAmount, 100.0f);
            setFloat(NComp::params::sidechainHpf, 92.0f);
            setFloat(NComp::params::sidechainTilt, -0.9f);
            break;

        case 2:
            setChoice(NComp::params::link, 0);
            setChoice(NComp::params::mslr, 0);
            setChoice(NComp::params::sidechain, 0);
            setFloat(NComp::params::input1, 3.9f);
            setFloat(NComp::params::input2, 3.9f);
            setFloat(NComp::params::output1, -0.4f);
            setFloat(NComp::params::output2, -0.4f);
            setFloat(NComp::params::threshold1, -20.0f);
            setFloat(NComp::params::threshold2, -20.0f);
            setFloat(NComp::params::ratio1, 7.5f);
            setFloat(NComp::params::ratio2, 7.5f);
            setFloat(NComp::params::attack1, 1.0f);
            setFloat(NComp::params::attack2, 1.0f);
            setFloat(NComp::params::release1, 5.0f);
            setFloat(NComp::params::release2, 5.0f);
            setFloat(NComp::params::mix, 70.0f);
            setFloat(NComp::params::character, 2.0f);
            setFloat(NComp::params::linkAmount, 72.0f);
            setFloat(NComp::params::sidechainHpf, 160.0f);
            setFloat(NComp::params::sidechainTilt, 2.5f);
            break;

        case 3:
            setChoice(NComp::params::link, 1);
            setChoice(NComp::params::mslr, 0);
            setChoice(NComp::params::sidechain, 0);
            setFloat(NComp::params::input1, 0.0f);
            setFloat(NComp::params::input2, 0.0f);
            setFloat(NComp::params::output1, 2.2f);
            setFloat(NComp::params::output2, 2.2f);
            setFloat(NComp::params::threshold1, -21.5f);
            setFloat(NComp::params::threshold2, -21.5f);
            setFloat(NComp::params::ratio1, 2.5f);
            setFloat(NComp::params::ratio2, 2.5f);
            setFloat(NComp::params::attack1, 53.0f);
            setFloat(NComp::params::attack2, 53.0f);
            setFloat(NComp::params::release1, 460.0f);
            setFloat(NComp::params::release2, 460.0f);
            setFloat(NComp::params::mix, 100.0f);
            setFloat(NComp::params::character, 3.0f);
            setFloat(NComp::params::linkAmount, 100.0f);
            setFloat(NComp::params::sidechainHpf, 68.0f);
            setFloat(NComp::params::sidechainTilt, 0.2f);
            break;

        case 4:
            setChoice(NComp::params::link, 0);
            setChoice(NComp::params::mslr, 0);
            setChoice(NComp::params::sidechain, 0);
            setFloat(NComp::params::input1, 4.2f);
            setFloat(NComp::params::input2, 4.2f);
            setFloat(NComp::params::output1, 0.2f);
            setFloat(NComp::params::output2, 0.2f);
            setFloat(NComp::params::threshold1, -22.8f);
            setFloat(NComp::params::threshold2, -22.8f);
            setFloat(NComp::params::ratio1, 5.3f);
            setFloat(NComp::params::ratio2, 5.3f);
            setFloat(NComp::params::attack1, 6.0f);
            setFloat(NComp::params::attack2, 6.0f);
            setFloat(NComp::params::release1, 110.0f);
            setFloat(NComp::params::release2, 110.0f);
            setFloat(NComp::params::mix, 48.0f);
            setFloat(NComp::params::character, 5.0f);
            setFloat(NComp::params::linkAmount, 86.0f);
            setFloat(NComp::params::sidechainHpf, 72.0f);
            setFloat(NComp::params::sidechainTilt, 0.65f);
            break;

        case 5:
            setChoice(NComp::params::link, 0);
            setChoice(NComp::params::mslr, 0);
            setChoice(NComp::params::sidechain, 1);
            setFloat(NComp::params::input1, 0.0f);
            setFloat(NComp::params::input2, 0.0f);
            setFloat(NComp::params::output1, 0.0f);
            setFloat(NComp::params::output2, 0.0f);
            setFloat(NComp::params::threshold1, -20.0f);
            setFloat(NComp::params::threshold2, -20.0f);
            setFloat(NComp::params::ratio1, 4.6f);
            setFloat(NComp::params::ratio2, 4.6f);
            setFloat(NComp::params::attack1, 2.0f);
            setFloat(NComp::params::attack2, 2.0f);
            setFloat(NComp::params::release1, 80.0f);
            setFloat(NComp::params::release2, 80.0f);
            setFloat(NComp::params::mix, 100.0f);
            setFloat(NComp::params::character, 1.0f);
            setFloat(NComp::params::linkAmount, 100.0f);
            setFloat(NComp::params::sidechainHpf, 150.0f);
            setFloat(NComp::params::sidechainTilt, 1.5f);
            break;

        default:
            break;
    }
}

bool isFlatPresetState(const juce::ValueTree& tree)
{
    if (!tree.isValid() || tree.getNumChildren() != 0)
        return false;

    return std::any_of(kPresetParamIds.begin(), kPresetParamIds.end(), [&tree] (const char* paramId)
    {
        return tree.hasProperty(paramId);
    });
}

juce::ValueTree convertFlatPresetState(const juce::ValueTree& flatState)
{
    juce::ValueTree converted(flatState.getType());

    for (int propertyIndex = 0; propertyIndex < flatState.getNumProperties(); ++propertyIndex)
    {
        const auto propertyName = flatState.getPropertyName(propertyIndex);
        const auto propertyNameString = propertyName.toString();
        const auto isParameterProperty = std::find_if(kPresetParamIds.begin(), kPresetParamIds.end(), [&propertyNameString] (const char* paramId)
        {
            return propertyNameString == paramId;
        }) != kPresetParamIds.end();

        if (isParameterProperty)
            continue;

        converted.setProperty(propertyName, flatState.getProperty(propertyName), nullptr);
    }

    for (const auto* paramId : kPresetParamIds)
    {
        if (!flatState.hasProperty(paramId))
            continue;

        juce::ValueTree paramNode(kApvtsValueNodeType);
        paramNode.setProperty(kApvtsIdProperty, paramId, nullptr);
        paramNode.setProperty(kApvtsValueProperty, flatState.getProperty(paramId), nullptr);
        converted.appendChild(paramNode, nullptr);
    }

    return converted;
}

}

NCompAudioProcessor::NCompAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Main Input", juce::AudioChannelSet::stereo(), true)
                               .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, &undoManager, "ncomp_params", NComp::params::createLayout())
{
    for (const auto* id : kObservedParamIds)
        apvts.addParameterListener(id, this);

    initialiseABSlotsFromCurrentState();
}

NCompAudioProcessor::~NCompAudioProcessor()
{
    cancelPendingUpdate();

    for (const auto* id : kObservedParamIds)
        apvts.removeParameterListener(id, this);
}

void NCompAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec{};
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());
    dsp.prepare(spec);
    meterL.store(0.0f);
    meterR.store(0.0f);
    transferInputDb.store(-60.0f);
    transferOutputDb.store(-60.0f);
    transferReductionDb.store(0.0f);
    setLatencySamples(dsp.getLatencySamples());
}

void NCompAudioProcessor::releaseResources()
{
}

int NCompAudioProcessor::getNumPrograms()
{
    return kNumFactoryPresets + 1;
}

int NCompAudioProcessor::getCurrentProgram()
{
    return currentProgramIndex.load();
}

void NCompAudioProcessor::setCurrentProgram(int index)
{
    const int clampedIndex = juce::jlimit(0, getNumPrograms() - 1, index);

    if (clampedIndex == 0)
    {
        setCurrentPresetInfo(PresetKind::manual, 0, {}, false);
        return;
    }

    auto setFloat = [this](const char* id, float value)
    {
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id)))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };

    auto setChoice = [this](const char* id, int value)
    {
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id)))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(value)));
    };

    suppressCallbacks.store(true);
    applyingFactoryPreset.store(true);
    applyPresetValues(clampedIndex, setFloat, setChoice);
    applyingFactoryPreset.store(false);
    suppressCallbacks.store(false);
    initialiseABSlotsFromCurrentState();
    setCurrentPresetInfo(PresetKind::factory, clampedIndex, {}, false);
}

const juce::String NCompAudioProcessor::getProgramName(int index)
{
    return getPresetNameForIndex(juce::jlimit(0, getNumPrograms() - 1, index));
}

NCompAudioProcessor::PresetKind NCompAudioProcessor::getCurrentPresetKind() const noexcept
{
    return presetKindFromInt(currentPresetKind.load());
}

juce::String NCompAudioProcessor::getCurrentPresetName() const
{
    switch (getCurrentPresetKind())
    {
        case PresetKind::factory:
            return getPresetNameForIndex(currentProgramIndex.load());

        case PresetKind::user:
        {
            const juce::ScopedLock lock(stateLock);
            auto presetFile = juce::File(currentUserPresetPath);
            return presetFile.existsAsFile() || presetFile.getFullPathName().isNotEmpty()
                ? presetDisplayName(presetFile)
                : juce::String("User Preset");
        }

        case PresetKind::manual:
        default:
            return "Manual";
    }
}

juce::File NCompAudioProcessor::getCurrentPresetFile() const
{
    const juce::ScopedLock lock(stateLock);
    return juce::File(currentUserPresetPath);
}

juce::File NCompAudioProcessor::getUserPresetDirectory() const
{
    return defaultPresetDirectory();
}

std::vector<juce::File> NCompAudioProcessor::getUserPresetFiles() const
{
    auto presetDirectory = getUserPresetDirectory();
    std::vector<juce::File> presetFiles;

    if (!presetDirectory.exists())
        return presetFiles;

    auto files = presetDirectory.findChildFiles(juce::File::findFiles, true, juce::String("*") + kPresetFileExtension);
    presetFiles.assign(files.begin(), files.end());

    std::sort(presetFiles.begin(), presetFiles.end(), [] (const juce::File& lhs, const juce::File& rhs)
    {
        return presetDisplayName(lhs).compareIgnoreCase(presetDisplayName(rhs)) < 0;
    });

    return presetFiles;
}

bool NCompAudioProcessor::loadPresetFromFile(const juce::File& file, juce::String* errorMessage)
{
    juce::ValueTree presetState;
    juce::String message;
    auto presetFile = normalisePresetFile(file);

    if (!readPresetStateFromFile(presetFile, presetState, message))
    {
        if (errorMessage != nullptr)
            *errorMessage = message;
        return false;
    }

    applyPresetMetadata(presetState, PresetKind::user, 0, presetFile, false);

    suppressCallbacks.store(true);
    apvts.replaceState(presetState);
    suppressCallbacks.store(false);

    initialiseABSlotsFromCurrentState();
    setCurrentPresetInfo(PresetKind::user, 0, presetFile, false);
    return true;
}

bool NCompAudioProcessor::savePresetToFile(const juce::File& file, juce::String* errorMessage)
{
    auto presetFile = normalisePresetFile(file);
    auto parentDirectory = presetFile.getParentDirectory();

    if (!parentDirectory.exists() && !parentDirectory.createDirectory())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Unable to create preset directory.";
        return false;
    }

    auto presetState = apvts.copyState();
    applyPresetMetadata(presetState, PresetKind::user, 0, presetFile, false);

    auto xml = presetState.createXml();
    if (xml == nullptr || !presetFile.replaceWithText(xml->toString()))
    {
        if (errorMessage != nullptr)
            *errorMessage = "Unable to write preset file.";
        return false;
    }

    setCurrentPresetInfo(PresetKind::user, 0, presetFile, false);
    return true;
}

bool NCompAudioProcessor::deletePresetFile(const juce::File& file, juce::String* errorMessage)
{
    auto presetFile = normalisePresetFile(file);
    if (!presetFile.existsAsFile())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Preset file does not exist.";
        return false;
    }

    if (!presetFile.deleteFile())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Unable to delete preset file.";
        return false;
    }

    if (getCurrentPresetKind() == PresetKind::user && getCurrentPresetFile() == presetFile)
        markCurrentStateAsManual();

    return true;
}

void NCompAudioProcessor::markCurrentStateAsManual()
{
    setCurrentPresetInfo(PresetKind::manual, 0, {}, false);
}

void NCompAudioProcessor::copyActiveABSlotToInactive()
{
    const juce::ScopedLock lock(stateLock);
    const int sourceSlot = juce::jlimit(0, 1, currentABSlot);
    const int targetSlot = 1 - sourceSlot;

    captureABSlot(sourceSlot);

    auto copiedState = abSlots[static_cast<size_t>(sourceSlot)].isValid()
        ? abSlots[static_cast<size_t>(sourceSlot)].createCopy()
        : apvts.copyState();

    abSlots[static_cast<size_t>(targetSlot)] = copiedState;
    updateHostDisplay();
}

void NCompAudioProcessor::selectABSlot(int slot)
{
    const int targetSlot = juce::jlimit(0, 1, slot);

    if (targetSlot == currentABSlot)
        return;

    const juce::ScopedLock lock(stateLock);
    suppressCallbacks.store(true);
    captureABSlot(currentABSlot);
    restoreABSlot(targetSlot);
    currentABSlot = targetSlot;
    suppressCallbacks.store(false);
    updateHostDisplay();
}

bool NCompAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();

    if (mainOut != juce::AudioChannelSet::stereo() || mainIn != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != mainOut)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto side = layouts.inputBuses[1];
        if (!side.isDisabled() && side != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void NCompAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    auto mainInput = getBusBuffer(buffer, true, 0);
    auto mainOutput = getBusBuffer(buffer, false, 0);

    if (mainInput.getNumChannels() == mainOutput.getNumChannels())
    {
        for (int ch = 0; ch < mainOutput.getNumChannels(); ++ch)
            mainOutput.copyFrom(ch, 0, mainInput.getReadPointer(ch), mainInput.getNumSamples());
    }
    else
    {
        mainOutput.clear();
    }

    juce::AudioBuffer<float>* scBuffer = nullptr;
    juce::AudioBuffer<float> scView;

    const auto layout = getBusesLayout();
    if (layout.inputBuses.size() > 1 && !layout.inputBuses[1].isDisabled())
    {
        scView = getBusBuffer(buffer, true, 1);
        scBuffer = &scView;
    }

    dsp.process(mainOutput, scBuffer, apvts, meterL, meterR, transferInputDb, transferOutputDb, transferReductionDb);
}

void NCompAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree root(kStateRootType);
    root.setProperty(kStateProgramProperty, currentProgramIndex.load(), nullptr);
    root.setProperty(kStatePresetKindProperty, currentPresetKind.load(), nullptr);
    root.setProperty(kStatePresetDirtyProperty, currentPresetDirty.load(), nullptr);
    root.setProperty(kStatePresetPathProperty, getCurrentPresetFile().getFullPathName(), nullptr);

    {
        const juce::ScopedLock lock(stateLock);
        auto current = apvts.copyState();
        applyPresetMetadata(current,
                            getCurrentPresetKind(),
                            currentProgramIndex.load(),
                            juce::File(currentUserPresetPath),
                            currentPresetDirty.load());
        current.setProperty(kStateRoleProperty, kStateRoleCurrent, nullptr);
        root.addChild(current, -1, nullptr);
    }

    juce::MemoryOutputStream mos(destData, false);
    root.writeToStream(mos);
}

void NCompAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto tree = juce::ValueTree::readFromData(data, sizeInBytes); tree.isValid())
    {
        if (tree.hasType(kStateRootType))
        {
            juce::ValueTree current;

            for (int i = 0; i < tree.getNumChildren(); ++i)
            {
                auto child = tree.getChild(i);
                if (child.getProperty(kStateRoleProperty).toString() == kStateRoleCurrent)
                    current = child;
            }

            if (!current.isValid() && tree.getNumChildren() > 0)
                current = tree.getChild(0);

            if (current.isValid())
            {
                apvts.replaceState(current);
                restorePresetMetadata(current);
                initialiseABSlotsFromCurrentState();
            }
            return;
        }

        // Legacy format compatibility: APVTS state was stored directly.
        apvts.replaceState(tree);
        restorePresetMetadata(tree);
        initialiseABSlotsFromCurrentState();
    }
}

void NCompAudioProcessor::parameterChanged(const juce::String& parameterID, float)
{
    if (suppressCallbacks.load())
        return;

    const auto markPresetDirty = [this, &parameterID]()
    {
        if (!applyingFactoryPreset.load())
            currentPresetDirty.store(true);
    };

    if (parameterID == NComp::params::link)
    {
        markPresetDirty();

        if (!isLinkOn())
            return;

        pendingLinkSyncAll.store(true);
        triggerAsyncUpdate();
        return;
    }

    if (!isLinkOn())
    {
        markPresetDirty();
        return;
    }

    for (size_t index = 0; index < kLinkedPairs.size(); ++index)
    {
        const auto& pair = kLinkedPairs[index];

        if (parameterID == pair.first)
        {
            markPresetDirty();
            pendingMirrorPairIndex.store(static_cast<int>(index));
            pendingMirrorDirection.store(1);
            triggerAsyncUpdate();
            return;
        }
        else if (parameterID == pair.second)
        {
            markPresetDirty();
            pendingMirrorPairIndex.store(static_cast<int>(index));
            pendingMirrorDirection.store(2);
            triggerAsyncUpdate();
            return;
        }
    }

    markPresetDirty();
}

void NCompAudioProcessor::handleAsyncUpdate()
{
    if (pendingLinkSyncAll.exchange(false) && isLinkOn())
    {
        suppressCallbacks.store(true);
        for (const auto& pair : kLinkedPairs)
            mirrorParameterValue(pair.first, pair.second);
        suppressCallbacks.store(false);
    }

    const int pendingPair = pendingMirrorPairIndex.exchange(-1);
    const int pendingDirection = pendingMirrorDirection.exchange(0);

    if (!isLinkOn() || pendingPair < 0 || pendingPair >= static_cast<int>(kLinkedPairs.size()) || pendingDirection == 0)
        return;

    const auto& pair = kLinkedPairs[static_cast<size_t>(pendingPair)];
    suppressCallbacks.store(true);

    if (pendingDirection == 1)
        mirrorParameterValue(pair.first, pair.second);
    else if (pendingDirection == 2)
        mirrorParameterValue(pair.second, pair.first);

    suppressCallbacks.store(false);
}

void NCompAudioProcessor::mirrorParameterValue(const char* sourceId, const char* targetId)
{
    auto* source = apvts.getParameter(sourceId);
    auto* target = apvts.getParameter(targetId);

    if (source == nullptr || target == nullptr)
        return;

    if (std::abs(source->getValue() - target->getValue()) < 1.0e-6f)
        return;

    target->setValueNotifyingHost(source->getValue());
}

bool NCompAudioProcessor::isLinkOn() const
{
    return getChoiceIndex(NComp::params::link) == 0;
}

int NCompAudioProcessor::getChoiceIndex(const char* id) const
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id)))
        return choice->getIndex();
    return 0;
}

NCompAudioProcessor::PresetKind NCompAudioProcessor::presetKindFromInt(int value) noexcept
{
    switch (value)
    {
        case static_cast<int>(PresetKind::factory): return PresetKind::factory;
        case static_cast<int>(PresetKind::user): return PresetKind::user;
        case static_cast<int>(PresetKind::manual):
        default: return PresetKind::manual;
    }
}

void NCompAudioProcessor::setCurrentPresetInfo(PresetKind kind, int factoryIndex, const juce::File& userPresetFile, bool dirty)
{
    currentProgramIndex.store(kind == PresetKind::factory ? juce::jlimit(0, getNumPrograms() - 1, factoryIndex) : 0);
    currentPresetKind.store(static_cast<int>(kind));
    currentPresetDirty.store(dirty);

    const juce::ScopedLock lock(stateLock);
    currentUserPresetPath = kind == PresetKind::user ? userPresetFile.getFullPathName() : juce::String();
}

void NCompAudioProcessor::applyPresetMetadata(juce::ValueTree& state,
                                              PresetKind kind,
                                              int factoryIndex,
                                              const juce::File& userPresetFile,
                                              bool dirty) const
{
    state.setProperty(kStateProgramProperty, kind == PresetKind::factory ? factoryIndex : 0, nullptr);
    state.setProperty(kStatePresetKindProperty, static_cast<int>(kind), nullptr);
    state.setProperty(kStatePresetPathProperty, kind == PresetKind::user ? userPresetFile.getFullPathName() : juce::String(), nullptr);
    state.setProperty(kStatePresetDirtyProperty, dirty, nullptr);
}

void NCompAudioProcessor::restorePresetMetadata(const juce::ValueTree& state)
{
    const auto kind = presetKindFromInt(static_cast<int>(state.getProperty(kStatePresetKindProperty, static_cast<int>(PresetKind::manual))));
    const int factoryIndex = static_cast<int>(state.getProperty(kStateProgramProperty, 0));
    const auto presetPath = state.getProperty(kStatePresetPathProperty).toString();
    const bool dirty = static_cast<bool>(state.getProperty(kStatePresetDirtyProperty, false));

    setCurrentPresetInfo(kind, factoryIndex, juce::File(presetPath), dirty);
}

void NCompAudioProcessor::captureABSlot(int slot)
{
    if (slot < 0 || slot > 1)
        return;

    abSlots[static_cast<size_t>(slot)] = apvts.copyState();
}

void NCompAudioProcessor::restoreABSlot(int slot)
{
    if (slot < 0 || slot > 1)
        return;

    auto toRestore = abSlots[static_cast<size_t>(slot)].isValid() ? abSlots[static_cast<size_t>(slot)].createCopy()
                                                                   : apvts.copyState();
    applyPresetMetadata(toRestore,
                        getCurrentPresetKind(),
                        currentProgramIndex.load(),
                        juce::File(currentUserPresetPath),
                        currentPresetDirty.load());
    apvts.replaceState(toRestore);
}

void NCompAudioProcessor::initialiseABSlotsFromCurrentState()
{
    const juce::ScopedLock lock(stateLock);
    abSlots[0] = apvts.copyState();
    abSlots[1] = apvts.copyState();
    currentABSlot = 0;
}

bool NCompAudioProcessor::readPresetStateFromFile(const juce::File& file, juce::ValueTree& outState, juce::String& errorMessage)
{
    if (!file.existsAsFile())
    {
        errorMessage = "Preset file does not exist.";
        return false;
    }

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
    {
        errorMessage = "Preset file could not be parsed.";
        return false;
    }

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid())
    {
        errorMessage = "Preset file does not contain valid state data.";
        return false;
    }

    if (tree.hasType(kStateRootType))
    {
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            auto child = tree.getChild(i);
            if (child.getProperty(kStateRoleProperty).toString() == kStateRoleCurrent)
            {
                outState = child;
                return true;
            }
        }

        if (tree.getNumChildren() > 0)
        {
            outState = tree.getChild(0);
            return true;
        }

        errorMessage = "Preset state is empty.";
        return false;
    }

    outState = isFlatPresetState(tree) ? convertFlatPresetState(tree) : tree;
    return true;
}

juce::AudioProcessorEditor* NCompAudioProcessor::createEditor()
{
    return new NCompAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NCompAudioProcessor();
}
