#include "PluginProcessor.h"
#include "Core/DSP/Dds1.h"
#include "Core/DSP/Dds2.h"
#include <chrono>
#include <cmath>
#include <complex>
#include <functional>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
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
    const double frequency = 2000.0 * sampleRate / fftSize;
    WaveBank bank;
    Dds1 oscillator;
    oscillator.setWaveBank (&bank);
    oscillator.setSampleRate (sampleRate);
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
    const double binHz = sampleRate / fftSize;
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

//==============================================================================
// Oscillator band-limiting across the keyboard: how much of the audible band each note keeps,
// how much aliasing it lets through, whether the DDS1 brightness steps while the pitch glides,
// and how clean DDS2 hard sync is. The oscillators are measured alone (no filter/VCA).

using FrequencyAt = std::function<double (int)>;
using Renderer = std::function<std::vector<float> (double rate, const FrequencyAt& frequencyAt, int samples)>;
constexpr int kSettleSamples = 64; // dropped: the BLEP delay line starts from silence

Renderer dds1Renderer (Dds1Wave wave)
{
    return [wave] (double rate, const FrequencyAt& frequencyAt, int samples)
    {
        static const WaveBank bank;
        Dds1 oscillator;
        oscillator.setWaveBank (&bank);
        oscillator.setSampleRate (rate);
        oscillator.resetPhases();
        Dds1::Settings settings;
        settings.wave = wave;
        juce::Random random (0x51a7);
        WrapEvent wrap;
        std::vector<float> signal ((size_t) samples);
        for (int i = 0; i < samples; ++i)
            signal[(size_t) i] = oscillator.process (frequencyAt (i), settings, random, wrap);
        return signal;
    };
}

Renderer dds2Renderer (Dds2Wave wave)
{
    return [wave] (double rate, const FrequencyAt& frequencyAt, int samples)
    {
        Dds2 oscillator;
        oscillator.setSampleRate (rate);
        oscillator.noteOn();
        juce::Random random (0x51a7);
        std::vector<float> signal ((size_t) samples);
        for (int i = -kSettleSamples; i < samples; ++i)
        {
            const float sample = oscillator.process (frequencyAt (juce::jmax (0, i)), wave, 0.5f, nullptr, random);
            if (i >= 0)
                signal[(size_t) i] = sample;
        }
        return signal;
    };
}

// Harmonic amplitudes of an unfiltered classic cycle (index = harmonic number): 1 saw, 2 square.
const std::vector<double>& referenceHarmonics (int classicCycle)
{
    static const std::array<std::vector<double>, 4> amplitudes = []
    {
        std::array<std::vector<double>, 4> result;
        constexpr int order = 12, size = 1 << order;
        for (int index = 0; index < 4; ++index)
        {
            const auto cycle = WaveBank::makeClassicCycle (index);
            std::vector<float> data ((size_t) 2 * size, 0.0f);
            std::copy (cycle.begin(), cycle.end(), data.begin());
            juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (data.data());
            result[(size_t) index].assign ((size_t) size / 2, 0.0);
            for (size_t k = 1; k < (size_t) size / 2; ++k)
                result[(size_t) index][k] = 2.0 * data[k] / size;
        }
        return result;
    }();
    return amplitudes[(size_t) classicCycle];
}

// Hann-windowed DFT at one exact frequency (rotating phasor, no per-sample trigonometry).
double harmonicAmplitude (const std::vector<float>& signal, int length, double frequency, double rate)
{
    const double w = juce::MathConstants<double>::twoPi * frequency / rate;
    const std::complex<double> step (std::cos (w), -std::sin (w));
    std::complex<double> phasor (1.0, 0.0), sum;
    double windowSum = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (length - 1));
        sum += (double) signal[(size_t) i] * window * phasor;
        windowSum += window;
        phasor *= step;
        if ((i & 1023) == 1023)
            phasor /= std::abs (phasor);
    }
    return 2.0 * std::abs (sum) / windowSum;
}

struct AliasMetrics
{
    double spurDb { -200.0 };   // largest inharmonic bin below 20 kHz, dB re the strongest harmonic
    double energyDb { -200.0 }; // all inharmonic energy below 20 kHz, dB re all energy below 20 kHz
};

// 65536-point FFT with a 4-term Blackman-Harris window (-92 dB side lobes, so the floor is the
// oscillator, not the window); bins within 8 bins of a multiple of `fundamental` are harmonic.
AliasMetrics measureAliasing (const std::vector<float>& signal, double rate, double fundamental)
{
    constexpr int fftOrder = 16, fftSize = 1 << fftOrder;
    jassert (signal.size() >= (size_t) fftSize);
    std::vector<float> data ((size_t) 2 * fftSize, 0.0f);
    for (int i = 0; i < fftSize; ++i)
    {
        const double x = juce::MathConstants<double>::twoPi * i / (fftSize - 1);
        const double window = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x) - 0.01168 * std::cos (3.0 * x);
        data[(size_t) i] = signal[(size_t) i] * (float) window;
    }
    juce::dsp::FFT (fftOrder).performFrequencyOnlyForwardTransform (data.data());

    const double binHz = rate / fftSize, audibleTop = juce::jmin (20000.0, 0.5 * rate);
    double strongest = 0.0, spur = 0.0, harmonicPower = 0.0, aliasPower = 0.0;
    for (int bin = 1; bin < fftSize / 2 && bin * binHz < audibleTop; ++bin)
    {
        const double hz = bin * binHz, magnitude = data[(size_t) bin];
        const double nearest = juce::jmax (1.0, std::round (hz / fundamental)) * fundamental;
        if (std::abs (hz - nearest) < 8.0 * binHz)
        {
            strongest = juce::jmax (strongest, magnitude);
            harmonicPower += magnitude * magnitude;
        }
        else if (hz > 20.0)
        {
            spur = juce::jmax (spur, magnitude);
            aliasPower += magnitude * magnitude;
        }
    }
    return { 20.0 * std::log10 (juce::jmax (1.0e-12, spur) / juce::jmax (1.0e-12, strongest)),
             10.0 * std::log10 (juce::jmax (1.0e-24, aliasPower) / juce::jmax (1.0e-24, aliasPower + harmonicPower)) };
}

struct NoteBandMetrics
{
    int note { 0 };
    double frequency { 0.0 };
    double bandEdgeHz { 0.0 };     // every harmonic up to here is within 3 dB of the analog cycle
    double highBandLossDb { 0.0 }; // energy of the 10..20 kHz harmonics versus the analog cycle
    bool fullBand { false };       // no harmonic below 20 kHz / Nyquist is more than 3 dB down
    AliasMetrics alias;
};

NoteBandMetrics measureNoteBand (const Renderer& render, int referenceCycle, double rate, int note)
{
    constexpr int fftSize = 1 << 16, dftLength = 16384;
    const double frequency = 440.0 * std::exp2 ((note - 69) / 12.0);
    const auto signal = render (rate, [frequency] (int) { return frequency; }, fftSize);
    const auto& reference = referenceHarmonics (referenceCycle);
    const double audibleTop = juce::jmin (20000.0, 0.5 * rate);

    NoteBandMetrics metrics;
    metrics.note = note;
    metrics.frequency = frequency;
    bool edgeFound = false;
    double measuredHigh = 0.0, referenceHigh = 0.0;
    for (int k = 1; k < (int) reference.size() && k * frequency < audibleTop; ++k)
    {
        if (reference[(size_t) k] < 1.0e-6 * reference[1])
            continue; // a harmonic the analog cycle does not have (the square's even ones)
        const double hz = k * frequency;
        const double amplitude = harmonicAmplitude (signal, dftLength, hz, rate);
        if (! edgeFound && amplitude < reference[(size_t) k] * std::pow (10.0, -3.0 / 20.0))
            edgeFound = true;
        if (! edgeFound)
            metrics.bandEdgeHz = hz;
        if (hz >= 10000.0)
        {
            measuredHigh += amplitude * amplitude;
            referenceHigh += reference[(size_t) k] * reference[(size_t) k];
        }
    }
    metrics.fullBand = ! edgeFound;
    metrics.highBandLossDb = referenceHigh > 0.0 ? 10.0 * std::log10 (juce::jmax (1.0e-30, measuredHigh) / referenceHigh) : 0.0;
    metrics.alias = measureAliasing (signal, rate, frequency);
    return metrics;
}

struct GlideMetrics
{
    double largestStepDb { 0.0 }; // largest frame-to-frame jump of the 10..18 kHz share
    double largestStepNote { 0.0 };
    int stepsOver1Db { 0 };
};

// MIDI 36 -> 84 in 6 s: a band-energy share that jumps between neighbouring frames is a mip
// level switching, not the saw's own spectrum (which changes by well under 1 dB per hop).
GlideMetrics measureGlide (double rate)
{
    constexpr int frameOrder = 11, frameSize = 1 << frameOrder, hop = 512;
    constexpr double startNote = 36.0, endNote = 84.0;
    const int samples = (int) (6.0 * rate);
    auto noteAt = [&] (double i) { return startNote + (endNote - startNote) * i / samples; };
    const auto signal = dds1Renderer (Dds1Wave::saw) (rate, [&] (int i) { return 440.0 * std::exp2 ((noteAt (i) - 69.0) / 12.0); }, samples);

    // Raised-cosine edges, so harmonics sliding across the band edges do not step either.
    auto bandWeight = [] (double hz)
    {
        auto rise = [] (double x) { return x <= 0.0 ? 0.0 : x >= 1.0 ? 1.0 : 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * x); };
        return rise ((hz - 9000.0) / 2000.0) * (1.0 - rise ((hz - 17000.0) / 2000.0));
    };

    juce::dsp::FFT fft (frameOrder);
    std::vector<float> data ((size_t) 2 * frameSize);
    GlideMetrics metrics;
    double previousDb = 0.0;
    bool havePrevious = false;
    for (int start = 0; start + frameSize <= samples; start += hop)
    {
        std::fill (data.begin(), data.end(), 0.0f);
        for (int i = 0; i < frameSize; ++i)
            data[(size_t) i] = signal[(size_t) (start + i)] * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (frameSize - 1)));
        fft.performFrequencyOnlyForwardTransform (data.data());
        double band = 0.0, total = 0.0;
        for (int bin = 1; bin < frameSize / 2; ++bin)
        {
            const double power = (double) data[(size_t) bin] * data[(size_t) bin];
            total += power;
            band += power * bandWeight (bin * rate / frameSize);
        }
        const double db = 10.0 * std::log10 (juce::jmax (1.0e-30, band) / juce::jmax (1.0e-30, total));
        if (havePrevious)
        {
            const double step = std::abs (db - previousDb);
            metrics.stepsOver1Db += step > 1.0 ? 1 : 0;
            if (step > metrics.largestStepDb)
            {
                metrics.largestStepDb = step;
                metrics.largestStepNote = noteAt (start + frameSize / 2);
            }
        }
        previousDb = db;
        havePrevious = true;
    }
    return metrics;
}

struct KeyboardSweep
{
    juce::String name;
    double rate { 0.0 };
    std::vector<NoteBandMetrics> notes;
};

KeyboardSweep measureKeyboard (const juce::String& name, const Renderer& render, int referenceCycle, double rate)
{
    KeyboardSweep sweep { name, rate, {} };
    for (int note = 24; note <= 108; ++note)
        sweep.notes.push_back (measureNoteBand (render, referenceCycle, rate, note));
    return sweep;
}

struct SyncMetrics
{
    juce::String wave;
    double rate { 0.0 };
    AliasMetrics worstSpur, worstEnergy;
    int spurNote { 0 }, energyNote { 0 };
    double spurRatio { 0.0 }, energyRatio { 0.0 };
};

// DDS1 sine master, DDS2 slave synced to it at non-integer ratios: the output repeats with the
// master period, so everything between the master's harmonics is aliasing.
SyncMetrics measureSync (const juce::String& name, Dds2Wave wave, double rate)
{
    static const WaveBank bank;
    SyncMetrics metrics { name, rate, {}, {} };
    for (int note : { 36, 48, 60, 72 })
        for (double ratio : { 1.53, 2.37, 3.71, 5.53, 7.19 })
        {
            const double master = 440.0 * std::exp2 ((note - 69) / 12.0);
            Dds1 dds1;
            dds1.setWaveBank (&bank);
            dds1.setSampleRate (rate);
            dds1.resetPhases();
            Dds1::Settings settings;
            settings.wave = Dds1Wave::sine;
            Dds2 dds2;
            dds2.setSampleRate (rate);
            dds2.noteOn();
            juce::Random random (0x51a7);
            std::vector<float> signal (1 << 16);
            for (int i = -kSettleSamples; i < (int) signal.size(); ++i)
            {
                WrapEvent wrap;
                dds1.process (master, settings, random, wrap);
                const float sample = dds2.process (master * ratio, wave, 0.5f, &wrap, random);
                if (i >= 0)
                    signal[(size_t) i] = sample;
            }
            const auto alias = measureAliasing (signal, rate, master);
            if (alias.spurDb > metrics.worstSpur.spurDb)
            {
                metrics.worstSpur = alias;
                metrics.spurNote = note;
                metrics.spurRatio = ratio;
            }
            if (alias.energyDb > metrics.worstEnergy.energyDb)
            {
                metrics.worstEnergy = alias;
                metrics.energyNote = note;
                metrics.energyRatio = ratio;
            }
        }
    return metrics;
}

struct OscillatorAnalysis
{
    std::vector<KeyboardSweep> keyboard;
    std::array<GlideMetrics, 2> glide;
    std::vector<SyncMetrics> sync;
};

OscillatorAnalysis analyseOscillators()
{
    OscillatorAnalysis analysis;
    for (double rate : { 44100.0, 48000.0 })
    {
        analysis.keyboard.push_back (measureKeyboard ("DDS1 saw", dds1Renderer (Dds1Wave::saw), 1, rate));
        analysis.keyboard.push_back (measureKeyboard ("DDS2 saw", dds2Renderer (Dds2Wave::saw), 1, rate));
        analysis.keyboard.push_back (measureKeyboard ("DDS2 square", dds2Renderer (Dds2Wave::square), 2, rate));
        for (auto [name, wave] : { std::pair { "saw", Dds2Wave::saw }, std::pair { "square", Dds2Wave::square },
                                   std::pair { "triangle", Dds2Wave::triangle }, std::pair { "sine", Dds2Wave::sine } })
            analysis.sync.push_back (measureSync (name, wave, rate));
    }
    analysis.glide = { measureGlide (44100.0), measureGlide (48000.0) };
    return analysis;
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

juce::String noteName (int note);

//==============================================================================
// CROSS MOD: DDS2 sine -> DDS1 sine. With the modulator at a harmonic ratio the result repeats
// with the common period, so energy off that harmonic series is detuning (exponential FM shifts
// the carrier) or aliasing (sidebands beyond Nyquist).
struct CrossModMetrics
{
    CrossModMode mode { CrossModMode::exponential };
    juce::String ratio;
    int note { 0 };
    float depth { 0.0f };
    AliasMetrics offSeries;
};

CrossModMetrics measureCrossMod (CrossModMode mode, float depth, int note, Dds2Range modulatorRange, const juce::String& ratio,
                                 double seriesDivisor)
{
    constexpr int settle = 4800, length = 1 << 16;
    NSynthAudioProcessor processor;
    const auto layer = Layer::upper;
    setPlain (processor, layerParamId (layer, pid::binaural), 0.0f);
    setPlain (processor, layerParamId (layer, pid::drift), 0.0f);
    setPlain (processor, layerParamId (layer, pid::dds1Wave), (float) toInt (Dds1Wave::sine));
    setPlain (processor, layerParamId (layer, pid::dds2Wave), (float) toInt (Dds2Wave::sine));
    setPlain (processor, layerParamId (layer, pid::dds2Range), (float) toInt (modulatorRange));
    setPlain (processor, layerParamId (layer, pid::mix), 0.0f);
    setPlain (processor, layerParamId (layer, pid::crossMod), depth);
    setPlain (processor, layerParamId (layer, pid::crossModMode), (float) toInt (mode));
    processor.getEngine().prepare (sampleRate, blockSize, 0);

    std::vector<float> signal;
    signal.reserve ((size_t) (settle + length + blockSize));
    juce::AudioBuffer<float> buffer (2, blockSize);
    for (int position = 0; position < settle + length; position += blockSize)
    {
        juce::MidiBuffer midi;
        if (position == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        processor.getEngine().process (buffer, nullptr, nullptr, midi, 120.0);
        signal.insert (signal.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }
    signal.erase (signal.begin(), signal.begin() + settle);
    const double carrier = 440.0 * std::exp2 ((note - 69) / 12.0);
    return { mode, ratio, note, depth, measureAliasing (signal, sampleRate, carrier / seriesDivisor) };
}

std::vector<CrossModMetrics> analyseCrossMod()
{
    std::vector<CrossModMetrics> result;
    for (auto mode : { CrossModMode::exponential, CrossModMode::linear })
    {
        for (float depth : { 0.25f, 0.5f, 1.0f })
        {
            result.push_back (measureCrossMod (mode, depth, 57, Dds2Range::ft8, "1:1", 1.0));
            result.push_back (measureCrossMod (mode, depth, 57, Dds2Range::ft4, "2:1", 1.0));
            result.push_back (measureCrossMod (mode, depth, 57, Dds2Range::ft16, "1:2", 2.0));
        }
        for (float depth : { 0.5f, 1.0f })
            result.push_back (measureCrossMod (mode, depth, 81, Dds2Range::ft8, "1:1", 1.0));
    }
    return result;
}

juce::String crossModReport (const std::vector<CrossModMetrics>& results)
{
    juce::String r;
    r << "## CROSS MOD\n\n"
      << "DDS2 sine modulating DDS1 sine (modulator:carrier ratio via the DDS2 range), mix = DDS1. Energy off the "
         "harmonic series of the common period, re all energy (0 dB = nothing left on the series): detuning (exponential FM shifts the carrier) "
         "or aliasing.\n\n"
      << "| Note | Ratio | Depth | Exp: off-series energy | Linear: off-series energy |\n|---|---|---:|---:|---:|\n";
    const size_t half = results.size() / 2;
    for (size_t i = 0; i < half; ++i)
        r << "| " << noteName (results[i].note) << " | " << results[i].ratio << " | " << juce::String (results[i].depth, 2)
          << " | " << juce::String (results[i].offSeries.energyDb, 1) << " dB | " << juce::String (results[i + half].offSeries.energyDb, 1) << " dB |\n";
    return r << "\n";
}

//==============================================================================
// VCF saturation: DDS1 saw through each filter type and drive, open (25 kHz, no resonance) and
// half closed with resonance. The oscillator alone stays below -80 dB, so energy off the
// harmonic series here is the filter's nonlinearity folding back.
struct VcfMetrics
{
    FilterType type { FilterType::ladder24 };
    Drive drive { Drive::off };
    float cutoff { 0.0f }, resonance { 0.0f };
    std::array<AliasMetrics, 4> notes;
    double highBandLossDb { 0.0 }; // C4: energy of the 10..20 kHz harmonics versus the analog saw
};

constexpr std::array<int, 4> kVcfNotes { 48, 60, 72, 84 };

std::vector<float> renderHeldNote (const std::function<void (NSynthAudioProcessor&)>& configure, int note, int length)
{
    constexpr int settle = 4800;
    NSynthAudioProcessor processor;
    const auto layer = Layer::upper;
    setPlain (processor, layerParamId (layer, pid::binaural), 0.0f);
    setPlain (processor, layerParamId (layer, pid::drift), 0.0f);
    configure (processor);
    processor.getEngine().prepare (sampleRate, blockSize, 0);

    std::vector<float> signal;
    signal.reserve ((size_t) (settle + length + blockSize));
    juce::AudioBuffer<float> buffer (2, blockSize);
    for (int position = 0; position < settle + length; position += blockSize)
    {
        juce::MidiBuffer midi;
        if (position == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        processor.getEngine().process (buffer, nullptr, nullptr, midi, 120.0);
        signal.insert (signal.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }
    signal.erase (signal.begin(), signal.begin() + settle);
    return signal;
}

std::vector<VcfMetrics> analyseVcf()
{
    std::vector<VcfMetrics> result;
    for (auto [cutoff, resonance] : { std::pair { 25000.0f, 0.0f }, std::pair { 2000.0f, 0.6f } })
        for (auto type : { FilterType::ladder24, FilterType::ladder12, FilterType::cascade24, FilterType::svf12 })
            for (auto drive : { Drive::off, Drive::one, Drive::two })
            {
                VcfMetrics metrics { type, drive, cutoff, resonance, {} };
                for (size_t n = 0; n < kVcfNotes.size(); ++n)
                {
                    const auto signal = renderHeldNote ([&] (NSynthAudioProcessor& processor)
                    {
                        const auto layer = Layer::upper;
                        setPlain (processor, layerParamId (layer, pid::vcfType), (float) toInt (type));
                        setPlain (processor, layerParamId (layer, pid::vcfDrive), (float) toInt (drive));
                        setPlain (processor, layerParamId (layer, pid::lpf), cutoff);
                        setPlain (processor, layerParamId (layer, pid::res), resonance);
                    }, kVcfNotes[n], 1 << 16);
                    const double frequency = 440.0 * std::exp2 ((kVcfNotes[n] - 69) / 12.0);
                    metrics.notes[n] = measureAliasing (signal, sampleRate, frequency);
                    if (kVcfNotes[n] == 60)
                    {
                        // Relative to the C4 fundamental, so the filter's overall gain does not count.
                        const auto& reference = referenceHarmonics (1);
                        const double fundamental = harmonicAmplitude (signal, 16384, frequency, sampleRate);
                        double measured = 0.0, expected = 0.0;
                        for (int k = 1; k * frequency < 20000.0; ++k)
                            if (k * frequency >= 10000.0)
                            {
                                const double a = harmonicAmplitude (signal, 16384, k * frequency, sampleRate) / fundamental;
                                measured += a * a;
                                expected += reference[(size_t) k] * reference[(size_t) k] / (reference[1] * reference[1]);
                            }
                        metrics.highBandLossDb = 10.0 * std::log10 (measured / expected);
                    }
                }
                result.push_back (metrics);
            }
    return result;
}

juce::String vcfReport (const std::vector<VcfMetrics>& results)
{
    static const char* typeNames[] = { "Ladder 24", "Ladder 12", "Cascade 24", "SVF 12" };
    static const char* driveNames[] = { "OFF", "1", "2" };
    juce::String r;
    r << "## VCF saturation aliasing\n\n"
      << "DDS1 saw (SUPER OFF) through the VCF; alias energy below 20 kHz re all energy below 20 kHz "
         "(the oscillator alone: about -80 dB or lower). C4 10-20 kHz = energy of those harmonics relative to the "
         "fundamental, versus the analog saw: the filter's (and its saturation's) treble loss.\n\n"
      << "| Cutoff / res | Type | Drive";
    for (int note : kVcfNotes)
        r << " | " << noteName (note);
    r << " | C4 10-20 kHz |\n|---|---|---";
    for (size_t i = 0; i < kVcfNotes.size(); ++i)
        r << "|---:";
    r << "|---:|\n";
    for (const auto& m : results)
    {
        r << "| " << juce::String (m.cutoff / 1000.0f, 0) << " kHz / " << juce::String (m.resonance, 1) << " | "
          << typeNames[toInt (m.type)] << " | " << driveNames[toInt (m.drive)];
        for (const auto& n : m.notes)
            r << " | " << juce::String (n.energyDb, 1);
        r << " | " << juce::String (m.highBandLossDb, 1) << " dB |\n";
    }
    return r << "\n";
}

//==============================================================================
// ANALOG: the spread between voices (tuning, level) and the pitch noise of a held note, measured
// on a sine through the open filter, one note at a time so the voice allocation walks the slots.
struct AnalogSpread
{
    float amount { 0.0f };
    double tuningRmsCents { 0.0 }, tuningRangeCents { 0.0 }, levelRangeDb { 0.0 }, noiseRmsCents { 0.0 };
};

double zeroCrossingCents (const float* x, int length, double reference)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = 1; i < length; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = (i - 1) + (double) -x[i - 1] / (double) (x[i] - x[i - 1]);
            if (first < 0.0)
                first = t;
            last = t;
            ++count;
        }
    return count < 2 ? 0.0 : 1200.0 * std::log2 (sampleRate * (count - 1) / (last - first) / reference);
}

AnalogSpread measureAnalog (float amount)
{
    constexpr int notes = 32, noteLength = 24000, heldStart = 4800, heldEnd = 19200, window = 2400;
    NSynthAudioProcessor processor;
    const auto layer = Layer::upper;
    setPlain (processor, layerParamId (layer, pid::binaural), 0.0f);
    setPlain (processor, layerParamId (layer, pid::dds1Wave), (float) toInt (Dds1Wave::sine));
    setPlain (processor, layerParamId (layer, pid::drift), 0.0f);
    setPlain (processor, layerParamId (layer, pid::analog), amount);
    processor.getEngine().prepare (sampleRate, blockSize, 0);

    std::vector<double> tuning, level, noise;
    juce::AudioBuffer<float> buffer (2, blockSize);
    std::vector<float> note ((size_t) noteLength);
    for (int n = 0; n < notes; ++n)
    {
        for (int position = 0; position < noteLength; position += blockSize)
        {
            juce::MidiBuffer midi;
            if (position == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
            if (position <= heldEnd && heldEnd < position + blockSize)
                midi.addEvent (juce::MidiMessage::noteOff (1, 69), heldEnd - position);
            const int count = juce::jmin (blockSize, noteLength - position);
            juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), 2, count);
            processor.getEngine().process (slice, nullptr, nullptr, midi, 120.0);
            std::copy (slice.getReadPointer (0), slice.getReadPointer (0) + count, note.begin() + position);
        }
        const float* held = note.data() + heldStart;
        const double cents = zeroCrossingCents (held, heldEnd - heldStart, 440.0);
        tuning.push_back (cents);
        double power = 0.0;
        for (int i = 0; i < heldEnd - heldStart; ++i)
            power += (double) held[i] * held[i];
        level.push_back (10.0 * std::log10 (power / (heldEnd - heldStart)));
        for (int w = 0; w + window <= heldEnd - heldStart; w += window)
            noise.push_back (zeroCrossingCents (held + w, window, 440.0) - cents);
    }

    auto rmsOf = [] (const std::vector<double>& v, double centre)
    {
        double sum = 0.0;
        for (auto x : v)
            sum += (x - centre) * (x - centre);
        return std::sqrt (sum / (double) v.size());
    };
    auto rangeOf = [] (const std::vector<double>& v) { return *std::max_element (v.begin(), v.end()) - *std::min_element (v.begin(), v.end()); };
    const double meanTuning = std::accumulate (tuning.begin(), tuning.end(), 0.0) / (double) tuning.size();
    return { amount, rmsOf (tuning, meanTuning), rangeOf (tuning), rangeOf (level), rmsOf (noise, 0.0) };
}

juce::String analogReport (const std::array<AnalogSpread, 3>& spreads)
{
    juce::String r;
    r << "## ANALOG\n\n"
      << "Sine through the open filter, MIDI 69, 32 notes played one after the other (each on the next voice slot). "
         "Tuning = mean pitch of each held note (0.3 s); pitch noise = deviation of 50 ms windows from their note's mean.\n\n"
      << "| ANALOG | Tuning spread (RMS) | Tuning range | Level range | Pitch noise (RMS) |\n|---:|---:|---:|---:|---:|\n";
    for (const auto& a : spreads)
        r << "| " << juce::String (a.amount, 1) << " | " << juce::String (a.tuningRmsCents, 2) << " ct | "
          << juce::String (a.tuningRangeCents, 2) << " ct | " << juce::String (a.levelRangeDb, 2) << " dB | "
          << juce::String (a.noiseRmsCents, 2) << " ct |\n";
    return r << "\n";
}

struct CpuMeasurement
{
    float analog { 0.0f };
    int workers { 0 };
    int activeVoices { 0 };
    double millisecondsPerBlock { 0.0 };
    double realtimeFactor { 0.0 };
    float peak { 0.0f };
};

CpuMeasurement benchmarkCpu (int requestedWorkers, float analog = 0.0f)
{
    NSynthAudioProcessor processor;
    setPlain (processor, layerParamId (Layer::upper, pid::binaural), 0.0f);
    setPlain (processor, layerParamId (Layer::upper, pid::voiceMode), toInt (VoiceMode::poly1));
    setPlain (processor, layerParamId (Layer::upper, pid::analog), analog);
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

    return { analog, processor.getEngine().getRenderWorkerCount(),
             processor.getEngine().getActiveVoices (Layer::upper),
             1000.0 * elapsed / measuredBlocks, audioDuration / juce::jmax (elapsed, 1.0e-9), peak };
}

struct KeyboardSummary
{
    int fullBandNotes { 0 };
    NoteBandMetrics narrowest, dullest, noisiest, mostAliased;
};

KeyboardSummary summarise (const KeyboardSweep& sweep)
{
    KeyboardSummary summary;
    summary.narrowest.bandEdgeHz = 1.0e9;
    for (const auto& m : sweep.notes)
    {
        summary.fullBandNotes += m.fullBand ? 1 : 0;
        if (m.bandEdgeHz < summary.narrowest.bandEdgeHz)
            summary.narrowest = m;
        if (m.highBandLossDb < summary.dullest.highBandLossDb)
            summary.dullest = m;
        if (m.alias.spurDb > summary.noisiest.alias.spurDb)
            summary.noisiest = m;
        if (m.alias.energyDb > summary.mostAliased.alias.energyDb)
            summary.mostAliased = m;
    }
    return summary;
}

juce::String rateName (double rate) { return juce::String (rate / 1000.0, 1) + " kHz"; }
juce::String noteName (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, 4) + " (" + juce::String (note) + ")"; }

juce::String oscillatorReport (const OscillatorAnalysis& analysis)
{
    juce::String r;
    r << "## Oscillators across the keyboard\n\n"
      << "MIDI 24..108 in semitones, oscillator alone (no filter/VCA), SUPER OFF. Band edge = highest harmonic "
         "up to which every harmonic is within 3 dB of the analog cycle (capped at 20 kHz / Nyquist); 10-20 kHz loss = "
         "energy of the harmonics in that band versus the analog cycle; spur = largest inharmonic bin below 20 kHz "
         "re the strongest harmonic; alias energy = all inharmonic energy below 20 kHz re all energy below 20 kHz "
         "(Blackman-Harris, 65536-point FFT).\n\n"
      << "| Signal | Rate | Full-band notes | Narrowest band edge | Worst 10-20 kHz loss | Worst spur | Worst alias energy |\n"
      << "|---|---|---:|---:|---:|---:|---:|\n";
    for (const auto& sweep : analysis.keyboard)
    {
        const auto s = summarise (sweep);
        r << "| " << sweep.name << " | " << rateName (sweep.rate) << " | " << s.fullBandNotes << " / " << (int) sweep.notes.size()
          << " | " << juce::String (s.narrowest.bandEdgeHz, 0) << " Hz at " << noteName (s.narrowest.note)
          << " | " << juce::String (s.dullest.highBandLossDb, 1) << " dB at " << noteName (s.dullest.note)
          << " | " << juce::String (s.noisiest.alias.spurDb, 1) << " dB at " << noteName (s.noisiest.note)
          << " | " << juce::String (s.mostAliased.alias.energyDb, 1) << " dB at " << noteName (s.mostAliased.note) << " |\n";
    }

    r << "\nDDS1 saw glide, MIDI 36 -> 84 in 6 s: largest jump of the 9-19 kHz energy share between 2048-sample "
         "frames 512 samples apart (a mip level switching shows as a jump of several dB).\n\n"
      << "| Rate | Largest step | Steps > 1 dB |\n|---|---:|---:|\n";
    for (size_t i = 0; i < analysis.glide.size(); ++i)
        r << "| " << rateName (i == 0 ? 44100.0 : 48000.0) << " | " << juce::String (analysis.glide[i].largestStepDb, 2)
          << " dB near MIDI " << juce::String (analysis.glide[i].largestStepNote, 1) << " | " << analysis.glide[i].stepsOver1Db << " |\n";

    r << "\n## DDS2 hard sync\n\n"
      << "DDS1 sine master at MIDI 36/48/60/72, DDS2 slave at 1.53/2.37/3.71/5.53/7.19 x the master; "
         "worst case over the 20 combinations.\n\n"
      << "| Slave | Rate | Worst spur | Worst alias energy |\n|---|---|---:|---:|\n";
    for (const auto& s : analysis.sync)
        r << "| " << s.wave << " | " << rateName (s.rate)
          << " | " << juce::String (s.worstSpur.spurDb, 1) << " dB (MIDI " << s.spurNote << ", x" << juce::String (s.spurRatio, 2) << ")"
          << " | " << juce::String (s.worstEnergy.energyDb, 1) << " dB (MIDI " << s.energyNote << ", x" << juce::String (s.energyRatio, 2) << ") |\n";

    r << "\n### Per note\n\nBand edge / 10-20 kHz loss / spur / alias energy; `*` = the whole audible band is kept.\n\n";
    for (const auto& sweep : analysis.keyboard)
    {
        r << "<details><summary>" << sweep.name << ", " << rateName (sweep.rate) << "</summary>\n\n"
          << "| Note | Hz | Edge | 10-20 kHz | Spur | Alias |\n|---|---:|---:|---:|---:|---:|\n";
        for (const auto& m : sweep.notes)
            r << "| " << noteName (m.note) << " | " << juce::String (m.frequency, 1)
              << " | " << juce::String (m.bandEdgeHz / 1000.0, 1) << "k" << (m.fullBand ? "*" : "")
              << " | " << juce::String (m.highBandLossDb, 1) << " | " << juce::String (m.alias.spurDb, 1)
              << " | " << juce::String (m.alias.energyDb, 1) << " |\n";
        r << "\n</details>\n\n";
    }
    return r;
}

void printOscillatorSummary (const OscillatorAnalysis& analysis)
{
    for (const auto& sweep : analysis.keyboard)
    {
        const auto s = summarise (sweep);
        std::cout << sweep.name << " @ " << sweep.rate << " Hz: full band " << s.fullBandNotes << "/" << sweep.notes.size()
                  << ", narrowest " << s.narrowest.bandEdgeHz << " Hz (MIDI " << s.narrowest.note
                  << "), worst 10-20 kHz loss " << s.dullest.highBandLossDb << " dB, worst spur " << s.noisiest.alias.spurDb
                  << " dB (MIDI " << s.noisiest.note << "), worst alias energy " << s.mostAliased.alias.energyDb
                  << " dB (MIDI " << s.mostAliased.note << ")." << std::endl;
    }
    for (size_t i = 0; i < analysis.glide.size(); ++i)
        std::cout << "DDS1 saw glide @ " << (i == 0 ? 44100 : 48000) << " Hz: largest step " << analysis.glide[i].largestStepDb << " dB." << std::endl;
    for (const auto& s : analysis.sync)
        std::cout << "DDS2 sync " << s.wave << " @ " << s.rate << " Hz: worst spur " << s.worstSpur.spurDb
                  << " dB, worst alias energy " << s.worstEnergy.energyDb << " dB." << std::endl;
}

bool writeReport (const juce::File& file)
{
    constexpr int thdWindowSize = 16384;
    const auto sine = renderNote (69, "sine", 24576);
    const auto sineMetrics = measureThd (sine, 4096, thdWindowSize, 440.0);
    const auto aliasing = measureDds1Aliasing();
    const auto oscillators = analyseOscillators();

    const std::array<CpuMeasurement, 3> cpu { benchmarkCpu (0), benchmarkCpu (0, 1.0f), benchmarkCpu (-1) };
    const std::array<AnalogSpread, 3> analog { measureAnalog (0.0f), measureAnalog (0.5f), measureAnalog (1.0f) };
    const auto crossMod = analyseCrossMod();
    const auto vcf = analyseVcf();

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
           << "| DDS1 sine, MIDI 69 | " << juce::String (sineMetrics.fundamental, 6) << " peak | "
           << juce::String (sineMetrics.thdPercent, 4) << "% | n/a |\n"
           << "| DDS1 saw, host rate | n/a | n/a | "
           << juce::String (aliasing.largestSpurDb, 2) << " dBc at "
           << juce::String (aliasing.largestSpurHz, 1) << " Hz |\n\n"
           << "DDS1 aliasing is measured at the 48 kHz host rate, using a "
              "bin-centred saw fundamental and the maximum inharmonic bin below 20 kHz. "
              "It uses the same audible-band spur metric as the oscillator regression test. "
              "It is a repeatable smoke metric, not a substitute for listening against the instrument.\n\n"
           << oscillatorReport (oscillators)
           << analogReport (analog)
           << crossModReport (crossMod)
           << vcfReport (vcf)
           << "## " << SynthEngine::kNumVoices << "-voice render CPU\n\n"
           << "| Mode | ANALOG | Workers | Active voices | ms/block | Real-time factor | Output peak |\n"
           << "|---|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& item : cpu)
        report << "| "
               << (item.workers == 0 ? "serial" : "automatic workers") << " | " << juce::String (item.analog, 1) << " | " << item.workers
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
    printOscillatorSummary (oscillators);
    for (const auto& c : crossMod)
        std::cout << "CROSS MOD " << (c.mode == CrossModMode::linear ? "linear" : "exp") << " MIDI " << c.note << " " << c.ratio
                  << " depth " << c.depth << ": off-series energy " << c.offSeries.energyDb << " dB." << std::endl;
    for (const auto& a : analog)
        std::cout << "ANALOG " << a.amount << ": tuning spread " << a.tuningRmsCents << " ct RMS (range " << a.tuningRangeCents
                  << "), level range " << a.levelRangeDb << " dB, pitch noise " << a.noiseRmsCents << " ct RMS." << std::endl;
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
