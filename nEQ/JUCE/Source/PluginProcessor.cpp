#include "PluginProcessor.h"
#include "PluginEditor.h"

NEQAudioProcessor::NEQAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, &undoManager, "neq_params", NEQ::params::createLayout())
{
    setLatencySamples(NEQ::kLinearPhaseLatencySamples);
}

void NEQAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(juce::jmax(1, getTotalNumOutputChannels()));
    dsp.prepare(spec);
}

void NEQAudioProcessor::releaseResources()
{
    dsp.reset();
}

bool NEQAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainInput = layouts.getMainInputChannelSet();
    const auto mainOutput = layouts.getMainOutputChannelSet();

    if (mainInput != mainOutput)
        return false;

    return mainInput == juce::AudioChannelSet::mono() || mainInput == juce::AudioChannelSet::stereo();
}

void NEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    dsp.process(buffer, apvts);
}

juce::AudioProcessorEditor* NEQAudioProcessor::createEditor()
{
    return new NEQAudioProcessorEditor(*this);
}

void NEQAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void NEQAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NEQAudioProcessor();
}