#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <set>
#include <string>
#include <vector>

#include "NCompProcessor.h"
#include "Parameters.h"

namespace
{
constexpr int kBlockSize = 256;
constexpr int kFftOrder = 16;
constexpr int kFftSize = 1 << kFftOrder;
constexpr float kFloorDb = -160.0f;
constexpr double kTwoPi = juce::MathConstants<double>::twoPi;

double gainToDb(double gain) noexcept
{
    return juce::Decibels::gainToDecibels(static_cast<float>(gain), kFloorDb);
}

double dbToGain(double db) noexcept
{
    return juce::Decibels::decibelsToGain(static_cast<float>(db));
}

double coherentFrequency(double sampleRate, int bin) noexcept
{
    return (sampleRate * static_cast<double>(bin)) / static_cast<double>(kFftSize);
}

double magnitudeAt(const std::vector<float>& spectrum, int bin, int spread = 1)
{
    const int start = juce::jlimit(0, static_cast<int>(spectrum.size()) - 1, bin - spread);
    const int end = juce::jlimit(0, static_cast<int>(spectrum.size()) - 1, bin + spread);
    double total = 0.0;

    for (int index = start; index <= end; ++index)
        total += static_cast<double>(spectrum[static_cast<size_t>(index)]) * static_cast<double>(spectrum[static_cast<size_t>(index)]);

    return total;
}

int binForFrequency(double sampleRate, double frequency)
{
    return juce::jlimit(0, kFftSize / 2 - 1, static_cast<int>(std::lround((frequency / sampleRate) * kFftSize)));
}

double sumSpectrumEnergy(const std::vector<float>& spectrum, int startBin = 1)
{
    double total = 0.0;

    for (int bin = juce::jmax(0, startBin); bin < static_cast<int>(spectrum.size()); ++bin)
    {
        const double magnitude = static_cast<double>(spectrum[static_cast<size_t>(bin)]);
        total += magnitude * magnitude;
    }

    return total;
}

double foldToBaseband(double sampleRate, double frequency) noexcept
{
    const double wrapped = std::fmod(std::fabs(frequency), sampleRate);
    return wrapped > sampleRate * 0.5 ? sampleRate - wrapped : wrapped;
}

std::string verdict(bool pass)
{
    return pass ? "PASS" : "WARN";
}

std::vector<float> makeMagnitudeSpectrum(const juce::AudioBuffer<float>& buffer, int channel)
{
    juce::dsp::FFT fft(kFftOrder);
    juce::dsp::WindowingFunction<float> window(kFftSize, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> fftData(static_cast<size_t>(kFftSize * 2), 0.0f);

    const int start = juce::jmax(0, buffer.getNumSamples() - kFftSize);
    const auto* read = buffer.getReadPointer(channel);

    for (int i = 0; i < kFftSize; ++i)
        fftData[static_cast<size_t>(i)] = read[start + i];

    window.multiplyWithWindowingTable(fftData.data(), kFftSize);
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    return { fftData.begin(), fftData.begin() + (kFftSize / 2) };
}

double channelRmsInRange(const juce::AudioBuffer<float>& buffer, int channel, int startSample, int endSample)
{
    const int clampedStart = juce::jlimit(0, buffer.getNumSamples(), startSample);
    const int clampedEnd = juce::jlimit(clampedStart, buffer.getNumSamples(), endSample);
    const auto* read = buffer.getReadPointer(channel);
    double sumSquares = 0.0;

    for (int sample = clampedStart; sample < clampedEnd; ++sample)
    {
        const double value = static_cast<double>(read[sample]);
        sumSquares += value * value;
    }

    const int numSamples = juce::jmax(1, clampedEnd - clampedStart);
    return std::sqrt(sumSquares / static_cast<double>(numSamples));
}

double stereoRmsInRange(const juce::AudioBuffer<float>& buffer, int startSample, int endSample)
{
    const int clampedStart = juce::jlimit(0, buffer.getNumSamples(), startSample);
    const int clampedEnd = juce::jlimit(clampedStart, buffer.getNumSamples(), endSample);
    double sumSquares = 0.0;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* read = buffer.getReadPointer(channel);
        for (int sample = clampedStart; sample < clampedEnd; ++sample)
        {
            const double value = static_cast<double>(read[sample]);
            sumSquares += value * value;
        }
    }

    const int sampleCount = juce::jmax(1, clampedEnd - clampedStart);
    const int channelCount = juce::jmax(1, buffer.getNumChannels());
    return std::sqrt(sumSquares / static_cast<double>(sampleCount * channelCount));
}

bool bufferIsFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* read = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (!std::isfinite(read[sample]))
                return false;
    }

    return true;
}

std::vector<double> estimateEnvelope(const juce::AudioBuffer<float>& buffer, int channel, double sampleRate)
{
    const auto* read = buffer.getReadPointer(channel);
    std::vector<double> envelope(static_cast<size_t>(buffer.getNumSamples()), 0.0);
    const double coef = std::exp(-1.0 / std::max(1.0, sampleRate * 0.0025));
    double state = 0.0;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const double rectified = std::fabs(static_cast<double>(read[sample]));
        state = rectified + coef * (state - rectified);
        envelope[static_cast<size_t>(sample)] = state;
    }

    return envelope;
}

double averageInRange(const std::vector<double>& values, int startSample, int endSample)
{
    const int clampedStart = juce::jlimit(0, static_cast<int>(values.size()), startSample);
    const int clampedEnd = juce::jlimit(clampedStart, static_cast<int>(values.size()), endSample);
    if (clampedStart == clampedEnd)
        return 0.0;

    const double sum = std::accumulate(values.begin() + clampedStart, values.begin() + clampedEnd, 0.0);
    return sum / static_cast<double>(clampedEnd - clampedStart);
}

double secondsFromSample(int sampleIndex, double sampleRate)
{
    return static_cast<double>(sampleIndex) / sampleRate;
}

class AnalysisAudioProcessor : public juce::AudioProcessor
{
public:
    AnalysisAudioProcessor()
        : juce::AudioProcessor(BusesProperties()
                                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Output", juce::AudioChannelSet::stereo(), true))
        , state(*this, nullptr, "ncomp_analysis", NComp::params::createLayout())
    {
    }

    const juce::String getName() const override { return "nCompAnalysis"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState state;
};

struct AnalysisRig
{
    void prepare(double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        juce::dsp::ProcessSpec spec{};
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(maxBlockSize);
        spec.numChannels = 2;
        processor.prepare(spec);
        meterL.store(0.0f);
        meterR.store(0.0f);
        transferInputDb.store(-60.0f);
        transferOutputDb.store(-60.0f);
        transferReductionDb.store(0.0f);
    }

    void process(juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain = nullptr)
    {
        processor.process(buffer, sidechain, owner.state, meterL, meterR, transferInputDb, transferOutputDb, transferReductionDb);
    }

    void setFloat(const char* id, float value)
    {
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(owner.state.getParameter(id)))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }

    void setChoice(const char* id, int index)
    {
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(owner.state.getParameter(id)))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(index)));
    }

    void configureReferenceBusCompressor()
    {
        setChoice(NComp::params::power, 0);
        setChoice(NComp::params::link, 0);
        setChoice(NComp::params::mslr, 0);
        setChoice(NComp::params::sidechain, 0);
        setChoice(NComp::params::sidechainListen, 0);
        setChoice(NComp::params::autoGain, 0);
        setFloat(NComp::params::input1, 2.0f);
        setFloat(NComp::params::input2, 2.0f);
        setFloat(NComp::params::output1, 0.0f);
        setFloat(NComp::params::output2, 0.0f);
        setFloat(NComp::params::threshold1, -18.0f);
        setFloat(NComp::params::threshold2, -18.0f);
        setFloat(NComp::params::ratio1, 4.0f);
        setFloat(NComp::params::ratio2, 4.0f);
        setFloat(NComp::params::attack1, 8.0f);
        setFloat(NComp::params::attack2, 8.0f);
        setFloat(NComp::params::release1, 130.0f);
        setFloat(NComp::params::release2, 130.0f);
        setFloat(NComp::params::mix, 100.0f);
        setFloat(NComp::params::linkAmount, 85.0f);
        setFloat(NComp::params::sidechainHpf, 110.0f);
        setFloat(NComp::params::sidechainTilt, 1.5f);
    }

    AnalysisAudioProcessor owner;
    NComp::NCompProcessor processor;
    double sampleRate { 44100.0 };
    std::atomic<float> meterL { 0.0f };
    std::atomic<float> meterR { 0.0f };
    std::atomic<float> transferInputDb { -60.0f };
    std::atomic<float> transferOutputDb { -60.0f };
    std::atomic<float> transferReductionDb { 0.0f };
};

template <typename BeforeBlockFn>
void processInBlocks(AnalysisRig& rig,
                     juce::AudioBuffer<float>& buffer,
                     int blockSize,
                     const juce::AudioBuffer<float>* sidechain,
                     BeforeBlockFn&& beforeBlock)
{
    juce::AudioBuffer<float> block(buffer.getNumChannels(), blockSize);
    juce::AudioBuffer<float> scBlock;

    if (sidechain != nullptr)
        scBlock.setSize(sidechain->getNumChannels(), blockSize, false, true, true);

    int blockIndex = 0;
    for (int start = 0; start < buffer.getNumSamples(); start += blockSize, ++blockIndex)
    {
        const int numThisBlock = juce::jmin(blockSize, buffer.getNumSamples() - start);
        block.clear();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            block.copyFrom(channel, 0, buffer, channel, start, numThisBlock);

        juce::AudioBuffer<float>* sidechainBlock = nullptr;
        if (sidechain != nullptr)
        {
            scBlock.clear();
            for (int channel = 0; channel < sidechain->getNumChannels(); ++channel)
                scBlock.copyFrom(channel, 0, *sidechain, channel, start, numThisBlock);
            sidechainBlock = &scBlock;
        }

        beforeBlock(blockIndex, start, numThisBlock);
        rig.process(block, sidechainBlock);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.copyFrom(channel, start, block, channel, 0, numThisBlock);
    }
}

void processInBlocks(AnalysisRig& rig, juce::AudioBuffer<float>& buffer, int blockSize, const juce::AudioBuffer<float>* sidechain = nullptr)
{
    processInBlocks(rig, buffer, blockSize, sidechain, [](int, int, int) {});
}

juce::AudioBuffer<float> makeStereoBuffer(int numSamples)
{
    juce::AudioBuffer<float> buffer(2, numSamples);
    buffer.clear();
    return buffer;
}

juce::AudioBuffer<float> makeStereoSine(double sampleRate, double frequency, float amplitude, int numSamples)
{
    auto buffer = makeStereoBuffer(numSamples);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float value = amplitude * std::sin(static_cast<float>(kTwoPi * frequency * secondsFromSample(sample, sampleRate)));
        left[sample] = value;
        right[sample] = value;
    }

    return buffer;
}

juce::AudioBuffer<float> makeStereoTwoTone(double sampleRate,
                                           double frequencyA,
                                           double amplitudeA,
                                           double frequencyB,
                                           double amplitudeB,
                                           int numSamples)
{
    auto buffer = makeStereoBuffer(numSamples);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const double time = secondsFromSample(sample, sampleRate);
        const float value = static_cast<float>(amplitudeA * std::sin(kTwoPi * frequencyA * time)
                                             + amplitudeB * std::sin(kTwoPi * frequencyB * time));
        left[sample] = value;
        right[sample] = value;
    }

    return buffer;
}

juce::AudioBuffer<float> makeReleaseStepSignal(double sampleRate, int numSamples, int stepSample)
{
    auto buffer = makeStereoBuffer(numSamples);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const double time = secondsFromSample(sample, sampleRate);
        const float amplitude = sample < stepSample ? 0.9f : 0.18f;
        const float value = amplitude * std::sin(static_cast<float>(kTwoPi * 1000.0 * time));
        left[sample] = value;
        right[sample] = value;
    }

    return buffer;
}

juce::AudioBuffer<float> makeStereoImageStressSignal(double sampleRate, int numSamples)
{
    auto buffer = makeStereoBuffer(numSamples);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    const int pulseInterval = static_cast<int>(sampleRate * 0.25);
    const int pulseLength = static_cast<int>(sampleRate * 0.008);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const double time = secondsFromSample(sample, sampleRate);
        const float bed = 0.2f * std::sin(static_cast<float>(kTwoPi * 900.0 * time));

        float transient = 0.0f;
        const int positionInPulse = sample % pulseInterval;
        if (positionInPulse < pulseLength)
        {
            const float phase = static_cast<float>(positionInPulse) / static_cast<float>(juce::jmax(1, pulseLength - 1));
            const float window = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * phase);
            transient = 0.8f * window * std::sin(static_cast<float>(kTwoPi * 2200.0 * time));
        }

        left[sample] = bed + transient;
        right[sample] = bed;
    }

    return buffer;
}

juce::AudioBuffer<float> makeAutomationStressSignal(double sampleRate, int numSamples)
{
    auto buffer = makeStereoBuffer(numSamples);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const double time = secondsFromSample(sample, sampleRate);
        const float carrier = 0.32f * std::sin(static_cast<float>(kTwoPi * 1200.0 * time));
        const float sub = 0.18f * std::sin(static_cast<float>(kTwoPi * 62.0 * time));
        left[sample] = carrier + sub;
        right[sample] = carrier - sub;
    }

    return buffer;
}

std::string analyseAliasing()
{
    AnalysisRig rig;
    rig.prepare(44100.0, kBlockSize);
    rig.configureReferenceBusCompressor();

    const int toneBin = 23831;
    const double toneHz = coherentFrequency(rig.sampleRate, toneBin);
    auto signal = makeStereoSine(rig.sampleRate, toneHz, 0.82f, kFftSize + 32768);
    processInBlocks(rig, signal, kBlockSize);

    const auto spectrum = makeMagnitudeSpectrum(signal, 0);
    const double fundamentalEnergy = magnitudeAt(spectrum, toneBin, 2);

    std::set<int> aliasBins;
    for (int harmonic = 2; harmonic <= 11; ++harmonic)
    {
        const double harmonicHz = toneHz * static_cast<double>(harmonic);
        if (harmonicHz < rig.sampleRate * 0.5)
            continue;

        const double foldedHz = foldToBaseband(rig.sampleRate, harmonicHz);
        if (foldedHz < 30.0)
            continue;

        aliasBins.insert(binForFrequency(rig.sampleRate, foldedHz));
    }

    double aliasEnergy = 0.0;
    double strongestSpur = 0.0;
    int strongestSpurBin = 0;

    for (int bin : aliasBins)
    {
        const double energy = magnitudeAt(spectrum, bin, 1);
        aliasEnergy += energy;

        if (energy > strongestSpur)
        {
            strongestSpur = energy;
            strongestSpurBin = bin;
        }
    }

    const double aliasRatioDb = gainToDb(std::sqrt(aliasEnergy / std::max(fundamentalEnergy, 1.0e-12)));
    const double strongestSpurDb = gainToDb(std::sqrt(strongestSpur / std::max(fundamentalEnergy, 1.0e-12)));
    const double strongestSpurHz = (static_cast<double>(strongestSpurBin) * rig.sampleRate) / static_cast<double>(kFftSize);
    const bool passes = aliasRatioDb <= -55.0;

    std::ostringstream report;
    report << "Alias-fold stress [" << verdict(passes) << "]: " << std::fixed << std::setprecision(1) << aliasRatioDb
           << " dBr from folded harmonics of a " << toneHz << " Hz coherent tone; strongest fold at " << strongestSpurHz
           << " Hz (" << strongestSpurDb << " dBr).";
    return report.str();
}

std::string analyseThdn()
{
    AnalysisRig rig;
    rig.prepare(48000.0, kBlockSize);
    rig.configureReferenceBusCompressor();

    const int toneBin = 1365;
    const double toneHz = coherentFrequency(rig.sampleRate, toneBin);
    auto signal = makeStereoSine(rig.sampleRate, toneHz, 0.42f, kFftSize + 32768);
    processInBlocks(rig, signal, kBlockSize);

    const auto spectrum = makeMagnitudeSpectrum(signal, 0);
    const double fundamentalEnergy = magnitudeAt(spectrum, toneBin, 2);
    double harmonicEnergy = 0.0;

    for (int harmonic = 2; harmonic <= 7; ++harmonic)
    {
        const double harmonicHz = toneHz * static_cast<double>(harmonic);
        if (harmonicHz >= rig.sampleRate * 0.5)
            break;

        harmonicEnergy += magnitudeAt(spectrum, binForFrequency(rig.sampleRate, harmonicHz), 1);
    }

    const double totalEnergy = sumSpectrumEnergy(spectrum, 1);
    const double thdPercent = 100.0 * std::sqrt(harmonicEnergy / std::max(fundamentalEnergy, 1.0e-12));
    const double thdnPercent = 100.0 * std::sqrt(std::max(0.0, totalEnergy - fundamentalEnergy) / std::max(fundamentalEnergy, 1.0e-12));
    const bool passes = thdnPercent <= 1.2;

    std::ostringstream report;
    report << "THD/THD+N [" << verdict(passes) << "]: " << std::fixed << std::setprecision(2) << thdPercent
           << "% / " << thdnPercent << "% at " << toneHz << " Hz under reference bus-comp settings.";
    return report.str();
}

std::string analyseImd()
{
    AnalysisRig rig;
    rig.prepare(48000.0, kBlockSize);
    rig.configureReferenceBusCompressor();

    auto signal = makeStereoTwoTone(rig.sampleRate, 60.0, 0.42, 7000.0, 0.42, kFftSize + 32768);
    processInBlocks(rig, signal, kBlockSize);

    const auto spectrum = makeMagnitudeSpectrum(signal, 0);
    const int carrierBin = binForFrequency(rig.sampleRate, 7000.0);
    const double carrierEnergy = magnitudeAt(spectrum, carrierBin, 2);

    double sidebandEnergy = 0.0;
    for (int sideband = 1; sideband <= 5; ++sideband)
    {
        sidebandEnergy += magnitudeAt(spectrum, binForFrequency(rig.sampleRate, 7000.0 + 60.0 * sideband), 1);
        sidebandEnergy += magnitudeAt(spectrum, binForFrequency(rig.sampleRate, 7000.0 - 60.0 * sideband), 1);
    }

    const double imdPercent = 100.0 * std::sqrt(sidebandEnergy / std::max(carrierEnergy, 1.0e-12));
    std::ostringstream report;
    report << "SMPTE-style IMD: " << std::fixed << std::setprecision(2) << imdPercent
           << "% from 60 Hz + 7 kHz sidebands under reference bus-comp settings.";
    return report.str();
}

std::string analyseRelease()
{
    AnalysisRig rig;
    rig.prepare(48000.0, kBlockSize);
    rig.configureReferenceBusCompressor();
    rig.setFloat(NComp::params::attack1, 4.0f);
    rig.setFloat(NComp::params::attack2, 4.0f);
    rig.setFloat(NComp::params::release1, 180.0f);
    rig.setFloat(NComp::params::release2, 180.0f);
    rig.setFloat(NComp::params::threshold1, -20.0f);
    rig.setFloat(NComp::params::threshold2, -20.0f);

    const int stepSample = static_cast<int>(rig.sampleRate * 0.3);
    auto input = makeReleaseStepSignal(rig.sampleRate, static_cast<int>(rig.sampleRate * 1.0), stepSample);
    auto output = input;
    processInBlocks(rig, output, kBlockSize);

    const auto inputEnvelope = estimateEnvelope(input, 0, rig.sampleRate);
    const auto outputEnvelope = estimateEnvelope(output, 0, rig.sampleRate);
    std::vector<double> gainEnvelope(inputEnvelope.size(), 1.0);

    for (size_t index = 0; index < gainEnvelope.size(); ++index)
        gainEnvelope[index] = outputEnvelope[index] / std::max(inputEnvelope[index], 1.0e-4);

    const double startGain = averageInRange(gainEnvelope,
                                            stepSample - static_cast<int>(rig.sampleRate * 0.02),
                                            stepSample - static_cast<int>(rig.sampleRate * 0.005));
    const double finalGain = averageInRange(gainEnvelope,
                                            static_cast<int>(rig.sampleRate * 0.85),
                                            static_cast<int>(rig.sampleRate * 0.98));

    const auto findRecoveryTime = [&](double fraction)
    {
        const double target = startGain + fraction * (finalGain - startGain);
        for (int sample = stepSample; sample < static_cast<int>(gainEnvelope.size()); ++sample)
            if (gainEnvelope[static_cast<size_t>(sample)] >= target)
                return (secondsFromSample(sample - stepSample, rig.sampleRate) * 1000.0);
        return -1.0;
    };

    const double recovery50 = findRecoveryTime(0.5);
    const double recovery90 = findRecoveryTime(0.9);
    const double gainStartDb = gainToDb(startGain);
    const double gainFinalDb = gainToDb(finalGain);

    std::ostringstream report;
    report << "Program release: starts around " << std::fixed << std::setprecision(1) << gainStartDb
           << " dB gain, settles to " << gainFinalDb << " dB, with 50% recovery in "
           << recovery50 << " ms and 90% recovery in " << recovery90 << " ms.";
    return report.str();
}

std::string analyseStereoImageStability()
{
    AnalysisRig rig;
    rig.prepare(48000.0, kBlockSize);
    rig.configureReferenceBusCompressor();
    rig.setFloat(NComp::params::linkAmount, 92.0f);

    auto output = makeStereoImageStressSignal(rig.sampleRate, static_cast<int>(rig.sampleRate * 2.0));
    processInBlocks(rig, output, kBlockSize);

    const int pulseInterval = static_cast<int>(rig.sampleRate * 0.25);
    const int pulseEnd = static_cast<int>(rig.sampleRate * 0.02);
    const int evalLength = static_cast<int>(rig.sampleRate * 0.08);
    double worstDeltaDb = 0.0;

    for (int start = pulseInterval + pulseEnd; start + evalLength < output.getNumSamples(); start += pulseInterval)
    {
        const double leftRms = channelRmsInRange(output, 0, start, start + evalLength);
        const double rightRms = channelRmsInRange(output, 1, start, start + evalLength);
        const double deltaDb = std::abs(gainToDb(leftRms) - gainToDb(rightRms));
        worstDeltaDb = std::max(worstDeltaDb, deltaDb);
    }

    std::ostringstream report;
    report << "Stereo image drift after off-centre transients: worst post-pulse L/R bed delta "
           << std::fixed << std::setprecision(2) << worstDeltaDb << " dB.";
    return report.str();
}

std::string analyseAutomationStress()
{
    AnalysisRig rig;
    rig.prepare(48000.0, kBlockSize);
    rig.configureReferenceBusCompressor();

    auto output = makeAutomationStressSignal(rig.sampleRate, static_cast<int>(rig.sampleRate * 6.0));

    processInBlocks(rig,
                    output,
                    kBlockSize,
                    nullptr,
                    [&rig](int blockIndex, int, int)
                    {
                        const double phase = static_cast<double>(blockIndex);
                        rig.setFloat(NComp::params::threshold1, static_cast<float>(-23.0 + 8.5 * std::sin(phase * 0.73)));
                        rig.setFloat(NComp::params::threshold2, static_cast<float>(-23.0 + 8.5 * std::sin(phase * 0.73 + 0.35)));
                        rig.setFloat(NComp::params::mix, static_cast<float>(67.0 + 33.0 * std::sin(phase * 0.41)));
                        rig.setFloat(NComp::params::linkAmount, static_cast<float>(65.0 + 35.0 * std::sin(phase * 0.19)));
                    });

    double maxJump = 0.0;
    for (int sample = 1; sample < output.getNumSamples(); ++sample)
    {
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
        {
            const double previous = output.getSample(channel, sample - 1);
            const double current = output.getSample(channel, sample);
            maxJump = std::max(maxJump, std::fabs(current - previous));
        }
    }

    std::ostringstream report;
    report << "Automation stress: finite=" << (bufferIsFinite(output) ? "yes" : "no")
           << ", max sample-to-sample jump " << std::fixed << std::setprecision(2)
           << gainToDb(std::max(maxJump, 1.0e-6)) << " dBFS.";
    return report.str();
}

std::string analyseAutoGainMatch()
{
    struct Scenario
    {
        const char* name;
        float inputDb;
        float thresholdDb;
        float ratio;
        float mix;
    };

    constexpr std::array<Scenario, 4> scenarios {{
        { "glue", 2.0f, -18.0f, 2.0f, 100.0f },
        { "bus", 5.0f, -22.0f, 4.0f, 100.0f },
        { "heavy", 8.0f, -26.0f, 8.0f, 100.0f },
        { "parallel", 6.0f, -24.0f, 6.0f, 55.0f },
    }};

    const double sampleRate = 48000.0;
    const int numSamples = static_cast<int>(sampleRate * 6.0);
    const int measureStart = static_cast<int>(sampleRate * 3.0);
    const int measureEnd = static_cast<int>(sampleRate * 5.5);
    double worstErrorDb = 0.0;
    double totalErrorDb = 0.0;
    std::ostringstream perScenario;

    for (const auto& scenario : scenarios)
    {
        AnalysisRig rig;
        rig.prepare(sampleRate, kBlockSize);
        rig.configureReferenceBusCompressor();
        rig.setChoice(NComp::params::autoGain, 1);
        rig.setFloat(NComp::params::input1, scenario.inputDb);
        rig.setFloat(NComp::params::input2, scenario.inputDb);
        rig.setFloat(NComp::params::output1, 0.0f);
        rig.setFloat(NComp::params::output2, 0.0f);
        rig.setFloat(NComp::params::threshold1, scenario.thresholdDb);
        rig.setFloat(NComp::params::threshold2, scenario.thresholdDb);
        rig.setFloat(NComp::params::ratio1, scenario.ratio);
        rig.setFloat(NComp::params::ratio2, scenario.ratio);
        rig.setFloat(NComp::params::mix, scenario.mix);

        const auto input = makeAutomationStressSignal(sampleRate, numSamples);
        auto output = input;
        processInBlocks(rig, output, kBlockSize);

        const double inputRms = stereoRmsInRange(input, measureStart, measureEnd);
        const double referenceRms = inputRms * dbToGain(scenario.inputDb);
        const double outputRms = stereoRmsInRange(output, measureStart, measureEnd);
        const double errorDb = std::abs(gainToDb(std::max(outputRms, 1.0e-9)) - gainToDb(std::max(referenceRms, 1.0e-9)));

        worstErrorDb = std::max(worstErrorDb, errorDb);
        totalErrorDb += errorDb;
        perScenario << ' ' << scenario.name << '=' << std::fixed << std::setprecision(2) << errorDb << " dB;";
    }

    const double meanErrorDb = totalErrorDb / static_cast<double>(scenarios.size());
    const bool passes = worstErrorDb <= 0.35;

    std::ostringstream report;
    report << "Auto gain match [" << verdict(passes) << "]: worst " << std::fixed << std::setprecision(2) << worstErrorDb
           << " dB, mean " << meanErrorDb << " dB across compression scenes;"
           << perScenario.str();
    return report.str();
}

std::string analyseAutoGainDynamics()
{
    AnalysisRig rig;
    rig.prepare(48000.0, kBlockSize);
    rig.configureReferenceBusCompressor();
    rig.setChoice(NComp::params::autoGain, 1);
    rig.setFloat(NComp::params::input1, 6.0f);
    rig.setFloat(NComp::params::input2, 6.0f);
    rig.setFloat(NComp::params::output1, 0.0f);
    rig.setFloat(NComp::params::output2, 0.0f);
    rig.setFloat(NComp::params::threshold1, -22.0f);
    rig.setFloat(NComp::params::threshold2, -22.0f);
    rig.setFloat(NComp::params::ratio1, 6.0f);
    rig.setFloat(NComp::params::ratio2, 6.0f);
    rig.setFloat(NComp::params::attack1, 12.0f);
    rig.setFloat(NComp::params::attack2, 12.0f);
    rig.setFloat(NComp::params::release1, 240.0f);
    rig.setFloat(NComp::params::release2, 240.0f);
    rig.setFloat(NComp::params::mix, 100.0f);

    const int numSamples = static_cast<int>(rig.sampleRate * 1.2);
    const int stepSample = static_cast<int>(rig.sampleRate * 0.35);
    auto input = makeReleaseStepSignal(rig.sampleRate, numSamples, stepSample);
    auto reference = input;
    reference.applyGain(static_cast<float>(dbToGain(6.0)));
    auto output = input;
    processInBlocks(rig, output, kBlockSize);

    const auto referenceEnvelope = estimateEnvelope(reference, 0, rig.sampleRate);
    const auto outputEnvelope = estimateEnvelope(output, 0, rig.sampleRate);
    const int transitionStart = juce::jmax(0, stepSample - static_cast<int>(rig.sampleRate * 0.04));
    const int transitionEnd = juce::jmin(numSamples, stepSample + static_cast<int>(rig.sampleRate * 0.32));
    const int settleStart = juce::jmin(numSamples, stepSample + static_cast<int>(rig.sampleRate * 0.55));
    const int settleEnd = juce::jmin(numSamples, stepSample + static_cast<int>(rig.sampleRate * 0.78));
    double worstTransitionErrorDb = 0.0;

    for (int sample = transitionStart; sample < transitionEnd; ++sample)
    {
        const double referenceDb = gainToDb(std::max(referenceEnvelope[static_cast<size_t>(sample)], 1.0e-6));
        const double outputDb = gainToDb(std::max(outputEnvelope[static_cast<size_t>(sample)], 1.0e-6));
        worstTransitionErrorDb = std::max(worstTransitionErrorDb, std::abs(outputDb - referenceDb));
    }

    const double settledReference = averageInRange(referenceEnvelope, settleStart, settleEnd);
    const double settledOutput = averageInRange(outputEnvelope, settleStart, settleEnd);
    const double settledErrorDb = std::abs(gainToDb(std::max(settledOutput, 1.0e-6))
                                         - gainToDb(std::max(settledReference, 1.0e-6)));
    const bool passes = worstTransitionErrorDb <= 1.5 && settledErrorDb <= 0.35;

    std::ostringstream report;
    report << "Auto gain dynamics [" << verdict(passes) << "]: worst transition envelope delta "
           << std::fixed << std::setprecision(2) << worstTransitionErrorDb
           << " dB, settled delta " << settledErrorDb
           << " dB on a release step with auto gain enabled.";
    return report.str();
}

std::string analyseSampleRateSwitching()
{
    std::ostringstream report;
    report << "Sample-rate switching:";

    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        AnalysisRig rig;
        rig.prepare(sampleRate, 512);
        rig.configureReferenceBusCompressor();
        auto signal = makeStereoSine(sampleRate, 1000.0, 0.45f, static_cast<int>(sampleRate * 0.35));
        processInBlocks(rig, signal, 512);
        report << ' ' << static_cast<int>(sampleRate) << " Hz latency=" << rig.processor.getLatencySamples()
               << " samples finite=" << (bufferIsFinite(signal) ? "yes" : "no") << ';';
    }

    return report.str();
}

std::string analyseCpuHeadroom()
{
    AnalysisRig rig;
    rig.prepare(48000.0, 512);
    rig.configureReferenceBusCompressor();
    auto block = makeAutomationStressSignal(rig.sampleRate, 512);

    const int numBlocks = 4000;
    const double audioDurationSeconds = (static_cast<double>(numBlocks) * 512.0) / rig.sampleRate;
    const auto start = std::chrono::high_resolution_clock::now();

    for (int blockIndex = 0; blockIndex < numBlocks; ++blockIndex)
    {
        rig.setFloat(NComp::params::threshold1, static_cast<float>(-24.0 + 7.0 * std::sin(blockIndex * 0.11)));
        rig.setFloat(NComp::params::threshold2, static_cast<float>(-24.0 + 7.0 * std::sin(blockIndex * 0.11 + 0.2)));
        rig.setFloat(NComp::params::mix, static_cast<float>(85.0 + 15.0 * std::sin(blockIndex * 0.08)));
        rig.process(block);
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const double elapsedSeconds = std::chrono::duration<double>(end - start).count();
    const double realtimeFactor = audioDurationSeconds / std::max(elapsedSeconds, 1.0e-9);
    const double millisecondsPerBlock = (elapsedSeconds * 1000.0) / static_cast<double>(numBlocks);

    std::ostringstream report;
    report << "CPU headroom: " << std::fixed << std::setprecision(1) << realtimeFactor
           << "x realtime, " << std::setprecision(3) << millisecondsPerBlock << " ms per 512-sample block.";
    return report.str();
}

std::string buildReport()
{
    std::ostringstream report;
    report << "# nComp Analysis Report\n\n";
    report << "- " << analyseAliasing() << "\n";
    report << "- " << analyseThdn() << "\n";
    report << "- " << analyseImd() << "\n";
    report << "- " << analyseRelease() << "\n";
    report << "- " << analyseStereoImageStability() << "\n";
    report << "- " << analyseAutomationStress() << "\n";
    report << "- " << analyseAutoGainMatch() << "\n";
    report << "- " << analyseAutoGainDynamics() << "\n";
    report << "- " << analyseSampleRateSwitching() << "\n";
    report << "- " << analyseCpuHeadroom() << "\n";
    return report.str();
}
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::ScopedNoDenormals noDenormals;

    const std::string report = buildReport();
    std::cout << report;

    if (argc == 3 && std::string(argv[1]) == "--output")
    {
        std::ofstream output(argv[2], std::ios::out | std::ios::trunc);
        output << report;
    }

    return 0;
}