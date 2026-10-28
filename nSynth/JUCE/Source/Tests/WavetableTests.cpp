#include "TestHelpers.h"
#include "Core/DSP/Wavetables.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace nsynth;
using namespace nsynth::test;

class WavetableTests : public juce::UnitTest
{
public:
    WavetableTests() : juce::UnitTest ("Wavetables", "nSynth") {}

    static std::vector<float> levelAsVector (const MipWavetable& t, int level)
    {
        const float* d = t.levelData (level) + 1; // skip the leading guard point
        return { d, d + MipWavetable::kTableSize };
    }

    // Highest harmonic index carrying energy above -100 dB relative to the strongest one.
    static int highestSignificantHarmonic (const std::vector<float>& cycle)
    {
        juce::dsp::FFT fft (12);
        std::vector<float> buf (2 * 4096, 0.0f);
        std::copy (cycle.begin(), cycle.end(), buf.begin());
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double maxMag = 0.0;
        for (int k = 1; k < 2048; ++k)
            maxMag = juce::jmax (maxMag, (double) buf[(size_t) k]);
        int highest = 0;
        for (int k = 1; k < 2048; ++k)
            if (buf[(size_t) k] > maxMag * 1.0e-5)
                highest = k;
        return highest;
    }

    void runTest() override
    {
        WaveBank bank;

        beginTest ("classic saw: a rising, slightly convex (Minimoog-style) ramp from -1 to +1");
        {
            const auto& saw = bank.classic (1);
            // The table is built from harmonics 1..N/2, so the ramp's DC offset is removed.
            double dc = 0.0;
            for (int i = 0; i < 4096; ++i)
                dc += (2.0 * WaveBank::analogRamp (i / 4096.0) - 1.0) / 4096.0;
            float maxErr = 0.0f, previous = -2.0f;
            bool rising = true;
            for (double p = 0.05; p < 0.95; p += 0.01)
            {
                const float v = saw.read (0, p);
                maxErr = juce::jmax (maxErr, std::abs (v - (float) (2.0 * WaveBank::analogRamp (p) - 1.0 - dc)));
                rising = rising && v > previous;
                previous = v;
            }
            expectLessThan (maxErr, 0.01f);
            expect (rising);
            expectGreaterThan (saw.read (0, 0.5) + (float) dc, 0.02f);   // convex: above the straight line at the midpoint
        }

        beginTest ("classic triangle (rounded tips), square (drooping plateaus), pure sine");
        {
            expectWithinAbsoluteError (bank.classic (3).read (0, 0.25), 1.0f, 0.01f);
            expectWithinAbsoluteError (bank.classic (3).read (0, 0.75), -1.0f, 0.01f);
            expectGreaterThan (bank.classic (3).read (0, 0.125), 0.6f);  // rounded: above the straight 0.5
            const float early = bank.classic (2).read (0, 0.05), late = bank.classic (2).read (0, 0.45);
            expectWithinAbsoluteError (early, 1.0f, 0.03f);
            expectLessThan (late, early - 0.03f);                       // the plateau droops
            expectWithinAbsoluteError (bank.classic (2).read (0, 0.55), -1.0f, 0.03f);
            expectWithinAbsoluteError (bank.classic (0).read (0, 0.25), 1.0f, 1.0e-4f);
        }

        beginTest ("alt waves are named after their hardware families");
        {
            expectEquals (WaveBank::altWaveNames().size(), WaveBank::kNumAltWaves);
            for (const auto& name : WaveBank::altWaveNames())
                expect (name.startsWith ("TX ") || name.startsWith ("CZ ") || name.startsWith ("DW ") || name.startsWith ("PPG "), name);
        }

        beginTest ("every mip level is band-limited to its harmonic budget");
        for (int w = 0; w < 4; ++w)
            for (int level = 0; level < MipWavetable::kNumLevels; ++level)
            {
                const auto h = highestSignificantHarmonic (levelAsVector (bank.classic (w), level));
                expect (h <= bank.classic (w).highestHarmonic (level),
                        "wave " + juce::String (w) + " level " + juce::String (level) + " has harmonic " + juce::String (h));
            }

        beginTest ("level selection never lets a harmonic exceed 0.45 * fs");
        for (double f : { 20.0, 110.0, 440.0, 1000.0, 3520.0, 8000.0, 15000.0 })
            for (double sr : { 88200.0, 176400.0, 192000.0, 384000.0 })
            {
                const double inc = f / sr;
                const int level = MipWavetable::levelForIncrement (inc);
                const double topHarmonicFreq = (double) ((MipWavetable::kTableSize / 2) >> level) * f;
                expect (topHarmonicFreq <= 0.45 * sr + 1.0e-6 || level == MipWavetable::kNumLevels - 1);
                if (level > 0) // and the level is not needlessly dull
                    expect ((double) ((MipWavetable::kTableSize / 2) >> (level - 1)) * f > 0.45 * sr);
            }

        beginTest ("32 alternative waveforms: normalised and band-limited to fs/8");
        for (int i = 0; i < WaveBank::kNumAltWaves; ++i)
        {
            auto level0 = levelAsVector (bank.alt (i), 0);
            expectWithinAbsoluteError (peak (level0), 1.0f, 0.01f);
            expect (highestSignificantHarmonic (level0) <= WaveBank::kAltMaxHarmonic, "alt " + juce::String (i));
        }

        beginTest (".ws6 round trip (4096 x int16 little endian, no header)");
        {
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("nsynth_test", ".ws6");
            auto cycle = WaveBank::makeFactoryAltCycle (6);
            float pk = peak (cycle);
            for (auto& v : cycle) v /= pk;
            expect (WaveBank::writeWs6 (file, cycle));
            expectEquals ((int) file.getSize(), 8192);
            std::vector<float> back;
            expect (WaveBank::readWs6 (file, back));
            float maxErr = 0.0f;
            for (size_t i = 0; i < cycle.size(); ++i)
                maxErr = juce::jmax (maxErr, std::abs (back[i] - cycle[i]));
            expectLessThan (maxErr, 2.0f / 32768.0f);
            file.deleteFile();
        }

        beginTest (".ws6 with a wrong size is rejected");
        {
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("nsynth_bad", ".ws6");
            file.replaceWithText ("not a waveform");
            std::vector<float> back;
            juce::String error;
            expect (! WaveBank::readWs6 (file, back, &error));
            expect (error.isNotEmpty());
            file.deleteFile();
        }

        beginTest ("WAV import resamples one cycle, removes DC, normalises and rejects harmonics above the table limit");
        {
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getNonexistentChildFile ("nsynth_cycle", ".wav");
            constexpr int sourceFrames = 5003;
            juce::AudioBuffer<float> source (2, sourceFrames);
            for (int i = 0; i < sourceFrames; ++i)
            {
                const float sample = 0.4f + 0.25f * std::sin (
                    juce::MathConstants<float>::twoPi * 7.0f * (float) i / (float) sourceFrames)
                    + 0.15f * std::sin (
                    juce::MathConstants<float>::twoPi * 700.0f * (float) i / (float) sourceFrames);
                source.setSample (0, i, sample);
                source.setSample (1, i, sample);
            }

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> output = file.createOutputStream();
            expect (output != nullptr);
            if (output != nullptr)
            {
                auto writer = wav.createWriterFor (
                    output, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24));
                expect (writer != nullptr);
                if (writer != nullptr)
                    expect (writer->writeFromAudioSampleBuffer (source, 0, sourceFrames));
            }

            std::vector<float> cycle;
            juce::String error;
            expect (WaveBank::readAudioCycle (file, cycle, &error), error);
            expectEquals ((int) cycle.size(), MipWavetable::kTableSize);
            expectWithinAbsoluteError (peak (cycle), 1.0f, 1.0e-5f);
            double average = 0.0;
            for (auto sample : cycle)
                average += sample;
            average /= (double) cycle.size();
            expectWithinAbsoluteError ((float) average, 0.0f, 1.0e-5f);
            expect (highestSignificantHarmonic (cycle) <= WaveBank::kAltMaxHarmonic);
            file.deleteFile();
        }

        beginTest ("silent WAV import fails explicitly");
        {
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getNonexistentChildFile ("nsynth_silence", ".wav");
            juce::AudioBuffer<float> source (1, 64);
            source.clear(); // AudioBuffer does not zero its memory
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> output = file.createOutputStream();
            expect (output != nullptr);
            if (output != nullptr)
            {
                auto writer = wav.createWriterFor (
                    output, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16));
                expect (writer != nullptr);
                if (writer != nullptr)
                    writer->writeFromAudioSampleBuffer (source, 0, source.getNumSamples());
            }
            std::vector<float> cycle;
            juce::String error;
            expect (! WaveBank::readAudioCycle (file, cycle, &error));
            expect (error.isNotEmpty());
            expect (cycle.empty());
            file.deleteFile();
        }

        beginTest ("replacing an alt waveform rebuilds its tables");
        {
            WaveBank b2;
            std::vector<float> sine (4096);
            for (int n = 0; n < 4096; ++n)
                sine[(size_t) n] = (float) std::sin (juce::MathConstants<double>::twoPi * n / 4096.0);
            b2.setAltCycle (0, sine.data());
            expectWithinAbsoluteError (b2.alt (0).read (0, 0.25), 1.0f, 1.0e-3f);
            expectEquals (highestSignificantHarmonic (levelAsVector (b2.alt (0), 0)), 1);
        }

        beginTest ("user waveform bank survives ValueTree XML round trip and rejects malformed payloads");
        {
            WaveBank source, restored;
            std::vector<float> cycle (MipWavetable::kTableSize);
            for (int n = 0; n < MipWavetable::kTableSize; ++n)
                cycle[(size_t) n] = (float) std::sin (
                    juce::MathConstants<double>::twoPi * 13.0 * n / MipWavetable::kTableSize);
            source.setAltCycle (17, cycle.data());

            auto tree = source.toValueTree();
            const auto xml = tree.createXml();
            expect (xml != nullptr);
            if (xml != nullptr)
            {
                auto decoded = juce::ValueTree::fromXml (*xml);
                juce::String error;
                expect (restored.restoreFromValueTree (decoded, &error), error);
                for (int n = 0; n < MipWavetable::kTableSize; ++n)
                    expectWithinAbsoluteError (restored.altCycle (17)[(size_t) n], source.altCycle (17)[(size_t) n],
                                               2.0f / 32768.0f);
            }

            juce::ValueTree malformed ("USER_WAVES");
            malformed.setProperty ("schemaVersion", 1, nullptr);
            malformed.setProperty ("cycles", juce::var (juce::MemoryBlock (8)), nullptr);
            juce::String error;
            expect (! WaveBank::isValidValueTree (malformed, &error));
            expect (error.isNotEmpty());
        }
    }
};

static WavetableTests wavetableTests;
