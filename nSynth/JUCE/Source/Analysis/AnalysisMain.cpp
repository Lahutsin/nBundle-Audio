#include "PluginProcessor.h"
#include "Core/DSP/Dds1.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <thread>

using namespace nsynth;

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

void setPlain (NSynthAudioProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.getState().getParameter (id);
    if (parameter == nullptr)
        throw std::runtime_error ("Missing analysis parameter: " + id.toStdString());
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

std::vector<float> renderNote (int note, const char* waveform, int samples)
{
    NSynthAudioProcessor processor;
    const auto layer = Layer::upper;
    setPlain (processor, layerParamId (layer, pid::binaural), 0.0f);
    setPlain (processor, layerParamId (layer, pid::dds1Wave), (float) (std::strcmp (waveform, "sine") == 0
                                                                          ? toInt (Dds1Wave::sine)
                                                                          : toInt (Dds1Wave::saw)));
    setPlain (processor, layerParamId (layer, pid::mix), 0.0f);
    setPlain (processor, layerParamId (layer, pid::superMode), toInt (SuperMode::off));
    setPlain (processor, layerParamId (layer, pid::crossMod), 0.0f);
    setPlain (processor, layerParamId (layer, pid::lpf), 25000.0f);
    setPlain (processor, layerParamId (layer, pid::res), 0.0f);
    setPlain (processor, layerParamId (layer, pid::vcfDrive), toInt (Drive::off));
    setPlain (processor, layerParamId (layer, pid::env2Attack), 0.001f);
    setPlain (processor, layerParamId (layer, pid::env2Sustain), 1.0f);
    setPlain (processor, layerParamId (layer, pid::env2Release), 0.1f);
    processor.getEngine().prepare (sampleRate, blockSize, 0);

    std::vector<float> result;
    result.reserve ((size_t) samples);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 120), 0);
    for (int position = 0; position < samples; position += blockSize)
    {
        const int count = juce::jmin (blockSize, samples - position);
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), 2, count);
        processor.getEngine().process (slice, nullptr, nullptr, midi, 120.0);
        midi.clear();
        for (int i = 0; i < count; ++i)
            result.push_back (slice.getSample (0, i));
    }
    return result;
}

double toneAmplitude (const std::vector<float>& signal, size_t start, int length, double frequency)
{
    double real = 0.0, imaginary = 0.0, windowSum = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (length - 1));
        const double phase = juce::MathConstants<double>::twoPi * frequency * i / sampleRate;
        const double sample = (double) signal[start + (size_t) i] * window;
        real += sample * std::cos (phase);
        imaginary -= sample * std::sin (phase);
        windowSum += window;
    }
    return 2.0 * std::sqrt (real * real + imaginary * imaginary) / windowSum;
}

struct ThdMetrics
{
    double fundamental { 0.0 };
    double thdPercent { 0.0 };
};

struct AliasingMetrics
{
    double largestSpurDb { -120.0 };
    double largestSpurHz { 0.0 };
};

AliasingMetrics measureDds1Aliasing()
{
    constexpr int fftOrder = 16;
    constexpr int fftSize = 1 << fftOrder;
    const double internalRate = sampleRate * SynthLayer::kOversamplingFactor;
    const double frequency = 2000.0 * internalRate / fftSize;
    WaveBank bank;
    Dds1 oscillator;
    oscillator.setWaveBank (&bank);
    oscillator.setSampleRate (internalRate);
    Dds1::Settings settings;
    settings.wave = Dds1Wave::saw;
    oscillator.resetPhases();

    std::vector<float> signal ((size_t) fftSize);
    juce::Random random (0x51a7);
    WrapEvent wrap;
    for (auto& sample : signal)
        sample = oscillator.process (frequency, settings, random, wrap);

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> data ((size_t) 2 * (size_t) fftSize, 0.0f);
    for (int i = 0; i < fftSize; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (fftSize - 1));
        data[(size_t) i] = signal[(size_t) i] * (float) window;
    }
    fft.performFrequencyOnlyForwardTransform (data.data());

    double harmonic = 0.0, inharmonic = 0.0;
    double spurFrequency = 0.0;
    const double binHz = internalRate / fftSize;
    for (size_t bin = 2; bin < (size_t) fftSize / 2; ++bin)
    {
        const double hz = (double) bin * binHz;
        if (hz > 20000.0)
            break;
        const double nearest = std::round (hz / frequency) * frequency;
        if (std::abs (hz - nearest) < 6.0 * binHz)
            harmonic = juce::jmax (harmonic, (double) data[bin]);
        else if (data[bin] > inharmonic)
        {
            inharmonic = data[bin];
            spurFrequency = hz;
        }
    }
    return { 20.0 * std::log10 (juce::jmax (1.0e-12, inharmonic) / juce::jmax (harmonic, 1.0e-12)),
             spurFrequency };
}

ThdMetrics measureThd (const std::vector<float>& signal, size_t start, int length, double frequency)
{
    ThdMetrics metrics;
    metrics.fundamental = toneAmplitude (signal, start, length, frequency);
    double harmonicPower = 0.0;
    for (int harmonic = 2; harmonic <= 10; ++harmonic)
    {
        const double harmonicFrequency = frequency * harmonic;
        if (harmonicFrequency >= sampleRate * 0.5)
            break;
        const double amplitude = toneAmplitude (signal, start, length, harmonicFrequency);
        harmonicPower += amplitude * amplitude;
    }
    metrics.thdPercent = 100.0 * std::sqrt (harmonicPower) / juce::jmax (metrics.fundamental, 1.0e-12);
    return metrics;
}

struct CpuMeasurement
{
    int workers { 0 };
    int activeVoices { 0 };
    double millisecondsPerBlock { 0.0 };
    double realtimeFactor { 0.0 };
    float peak { 0.0f };
};

CpuMeasurement benchmarkCpu (int requestedWorkers)
{
    NSynthAudioProcessor processor;
    setPlain (processor, layerParamId (Layer::upper, pid::binaural), 0.0f);
    setPlain (processor, layerParamId (Layer::upper, pid::voiceMode), toInt (VoiceMode::poly1));
    processor.getEngine().prepare (sampleRate, blockSize, requestedWorkers);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int note = 0; note < SynthEngine::kNumVoices; ++note)
        midi.addEvent (juce::MidiMessage::noteOn (1, 36 + note, (juce::uint8) 100), 0);

    for (int block = 0; block < 32; ++block)
    {
        processor.getEngine().process (buffer, nullptr, nullptr, midi, 120.0);
        midi.clear();
    }

    constexpr int measuredBlocks = 160;
    const auto start = std::chrono::steady_clock::now();
    float peak = 0.0f;
    for (int block = 0; block < measuredBlocks; ++block)
    {
        processor.getEngine().process (buffer, nullptr, nullptr, midi, 120.0);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            peak = juce::jmax (peak, juce::jmax (std::abs (buffer.getSample (0, sample)),
                                                 std::abs (buffer.getSample (1, sample))));
    }
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    const double audioDuration = (double) measuredBlocks * blockSize / sampleRate;

    return { processor.getEngine().getRenderWorkerCount(),
             processor.getEngine().getActiveVoices (Layer::upper),
             1000.0 * elapsed / measuredBlocks, audioDuration / juce::jmax (elapsed, 1.0e-9), peak };
}

bool writeReport (const juce::File& file)
{
    constexpr int thdWindowSize = 16384;
    const auto sine = renderNote (69, "sine", 24576);
    const auto sineMetrics = measureThd (sine, 4096, thdWindowSize, 440.0);
    const auto aliasing = measureDds1Aliasing();

    const std::array<CpuMeasurement, 2> cpu { benchmarkCpu (0), benchmarkCpu (-1) };

    if (! std::isfinite (sineMetrics.thdPercent) || sineMetrics.fundamental < 1.0e-5
        || ! std::isfinite (aliasing.largestSpurDb))
    {
        std::cerr << "DSP analysis produced invalid or silent measurements." << std::endl;
        return false;
    }

    juce::String report;
    report << "# nSynth offline DSP/CPU analysis\n\n"
           << "- Run: " << juce::Time::getCurrentTime().toISO8601 (true) << "\n"
           << "- Host: " << juce::SystemStats::getOperatingSystemName() << "\n"
           << "- Logical CPUs: " << (int) std::thread::hardware_concurrency() << "\n"
           << "- Engine sample rate: 48 kHz; THD windowed DFT: " << thdWindowSize
           << " samples; alias analysis: 65536-point FFT; CPU block: "
           << blockSize << " samples.\n"
           << "- This is an offline regression/benchmark report, not a hardware-reference certification.\n\n"
           << "## Spectrum\n\n"
           << "| Signal | Fundamental | THD (H2-H10) | Largest non-harmonic bin below 20 kHz |\n"
           << "|---|---:|---:|---:|\n"
           << "| DDS1 sine, MIDI 69, 2x | " << juce::String (sineMetrics.fundamental, 6) << " peak | "
           << juce::String (sineMetrics.thdPercent, 4) << "% | n/a |\n"
           << "| DDS1 saw, 2x internal rate | n/a | n/a | "
           << juce::String (aliasing.largestSpurDb, 2) << " dBc at "
           << juce::String (aliasing.largestSpurHz, 1) << " Hz |\n\n"
           << "DDS1 aliasing is measured before decimation at twice the 48 kHz host rate, using a "
              "bin-centred saw fundamental and the maximum inharmonic bin below 20 kHz. "
              "It uses the same audible-band spur metric as the oscillator regression test. "
              "It is a repeatable smoke metric, not a substitute for listening against the instrument.\n\n"
           << "## " << SynthEngine::kNumVoices << "-voice render CPU (2x oversampling)\n\n"
           << "| Mode | Workers | Active voices | ms/block | Real-time factor | Output peak |\n"
           << "|---|---:|---:|---:|---:|---:|\n";
    for (const auto& item : cpu)
        report << "| "
               << (item.workers == 0 ? "serial" : "automatic workers") << " | " << item.workers
               << " | " << item.activeVoices << " | " << juce::String (item.millisecondsPerBlock, 4)
               << " | " << juce::String (item.realtimeFactor, 2) << "x | " << juce::String (item.peak, 5) << " |\n";

    report << "\nThe real-time factor is audio duration divided by measured wall time; values above 1x "
              "indicate that this isolated offline run completed faster than real time. Results depend on "
              "the host, power policy, and competing audio instances. No universal CPU guarantee is implied.\n";

    if (! file.getParentDirectory().createDirectory() || ! file.replaceWithText (report))
    {
        std::cerr << "Could not write analysis report: " << file.getFullPathName() << std::endl;
        return false;
    }
    std::cout << "Wrote " << file.getFullPathName() << std::endl;
    std::cout << "Sine THD " << sineMetrics.thdPercent << "%; DDS1 saw alias spur "
              << aliasing.largestSpurDb << " dBc." << std::endl;
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc != 3 || juce::String (argv[1]) != "--output")
    {
        std::cerr << "Usage: nSynthAnalysis --output <report.md>" << std::endl;
        return 2;
    }

    const auto output = juce::File::isAbsolutePath (argv[2])
                      ? juce::File (argv[2])
                      : juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    return writeReport (output) ? 0 : 1;
}
