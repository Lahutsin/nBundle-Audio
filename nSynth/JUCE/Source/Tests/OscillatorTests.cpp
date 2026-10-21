#include "TestHelpers.h"
#include "Core/DSP/Dds1.h"
#include "Core/DSP/Dds2.h"
#include "Core/DSP/Lfo.h"
#include "Core/Engine/Voice.h"

using namespace nsynth;
using namespace nsynth::test;

class OscillatorTests : public juce::UnitTest
{
public:
    OscillatorTests() : juce::UnitTest ("Oscillators", "nSynth") {}

    static constexpr double sr = 176400.0;

    std::vector<float> renderDds1 (Dds1& osc, double f, const Dds1::Settings& s, int n, juce::Random& rng)
    {
        std::vector<float> out ((size_t) n);
        WrapEvent w;
        for (auto& v : out)
            v = osc.process (f, s, rng, w);
        return out;
    }

    void runTest() override
    {
        WaveBank bank;
        juce::Random rng (42);

        beginTest ("DDS1 pitch accuracy (8', A4 = 440 Hz)");
        {
            Dds1 osc; osc.setWaveBank (&bank); osc.setSampleRate (sr);
            Dds1::Settings s; s.wave = Dds1Wave::sine;
            auto x = renderDds1 (osc, 440.0, s, (int) sr, rng);
            expectWithinAbsoluteError (zeroCrossingFrequency (x, sr), 440.0, 0.05);
        }

        beginTest ("DDS1/DDS2 RANGE offsets: 64'..2' = -36..+24 st, DDS2 LFO position");
        expectEquals (Voice::rangeOffset (Dds1Range::ft64), -36.0f);
        expectEquals (Voice::rangeOffset (Dds1Range::ft8), 0.0f);
        expectEquals (Voice::rangeOffset (Dds1Range::ft2), 24.0f);
        expectEquals (Voice::rangeOffset (Dds2Range::ft32), -24.0f);
        expectEquals (Voice::rangeOffset (Dds2Range::ft4), 12.0f);

        beginTest ("SUPER OFF = only the centroid (pure sine stays pure)");
        {
            Dds1 osc; osc.setWaveBank (&bank); osc.setSampleRate (sr);
            Dds1::Settings s; s.wave = Dds1Wave::sine; s.superMode = SuperMode::off; s.detune = 1.0f;
            auto x = renderDds1 (osc, 1000.0, s, 1 << 16, rng);
            const double fundamental = toneAmplitude (x, 1000.0, sr);
            const double sideband = toneAmplitude (x, 1000.0 * (1.0 + Dds1::coreOffset (6)), sr);
            expectWithinAbsoluteError (fundamental, 1.0, 0.02);
            expectLessThan (toDb (sideband / fundamental), -80.0);
        }

        beginTest ("SUPER ON + DETUNE spreads the six sister oscillators");
        {
            Dds1 osc; osc.setWaveBank (&bank); osc.setSampleRate (sr); osc.randomisePhases (rng);
            Dds1::Settings s; s.wave = Dds1Wave::sine; s.superMode = SuperMode::on; s.detune = 1.0f;
            auto x = renderDds1 (osc, 1000.0, s, 1 << 17, rng);
            float c = 0, sis = 0;
            Dds1::coreGains (SuperMode::on, c, sis);
            for (int core = 0; core < Dds1::kNumCores; ++core)
            {
                const double f = 1000.0 * (1.0 + Dds1::coreOffset (core));
                const double expected = core == Dds1::kCentroid ? c : sis;
                expectWithinAbsoluteError (toneAmplitude (x, f, sr), expected, 0.05, "core " + juce::String (core));
            }
            expectGreaterThan (sis, 0.0f);
        }

        beginTest ("SUPER 1/2 resets the DDS1 phase at note-on, OFF/ON are free-running");
        {
            Dds1 osc; osc.setWaveBank (&bank); osc.setSampleRate (sr); osc.randomisePhases (rng);
            Dds1::Settings s; s.wave = Dds1Wave::saw; s.superMode = SuperMode::half; s.detune = 0.5f;
            osc.noteOn (SuperMode::half);
            auto a = renderDds1 (osc, 300.0, s, 64, rng);
            renderDds1 (osc, 300.0, s, 1234, rng);
            osc.noteOn (SuperMode::half);
            auto b = renderDds1 (osc, 300.0, s, 64, rng);
            expect (std::equal (a.begin(), a.end(), b.begin()));

            s.superMode = SuperMode::on;
            osc.noteOn (SuperMode::on);
            auto c1 = renderDds1 (osc, 300.0, s, 64, rng);
            renderDds1 (osc, 300.0, s, 1234, rng);
            osc.noteOn (SuperMode::on);
            auto c2 = renderDds1 (osc, 300.0, s, 64, rng);
            expect (! std::equal (c1.begin(), c1.end(), c2.begin()));
        }

        beginTest ("PWM/WAVE morph: 0 = selected wave, 1 = right-hand neighbour; alt A -> B");
        {
            Dds1 osc; osc.setWaveBank (&bank); osc.setSampleRate (sr); osc.resetPhases();
            Dds1::Settings s; s.wave = Dds1Wave::saw; s.morph = 1.0f;
            WrapEvent w;
            const float v = osc.process (sr / 8.0 / 100.0, s, rng, w); // phase advances by 1/800
            const double p = 1.0 / 800.0;
            expectWithinAbsoluteError (v, bank.classic (2).read (MipWavetable::levelForIncrement (p), p), 1.0e-4f);

            Dds1 alt; alt.setWaveBank (&bank); alt.setSampleRate (sr); alt.resetPhases();
            Dds1::Settings a; a.wave = Dds1Wave::alt; a.altA = 4; a.altB = 9; a.morph = 0.5f;
            const float m = alt.process (sr / 800.0, a, rng, w);
            const int level = MipWavetable::levelForIncrement (p);
            expectWithinAbsoluteError (m, 0.5f * (bank.alt (4).read (level, p) + bank.alt (9).read (level, p)), 1.0e-4f);
        }

        beginTest ("DDS1 saw is alias-free in the audible band at 4x oversampling");
        {
            Dds1 osc; osc.setWaveBank (&bank); osc.setSampleRate (sr);
            Dds1::Settings s; s.wave = Dds1Wave::saw;
            const double f = 2000.0 * sr / 65536.0; // ~5.4 kHz, bin-centred so window leakage cannot mask aliases
            auto x = renderDds1 (osc, f, s, 1 << 16, rng);
            auto spec = spectrum (x, 16);
            const double binHz = sr / 65536.0;
            double harmonic = 0.0, inharmonic = 0.0;
            for (size_t k = 2; k < spec.size(); ++k)
            {
                const double hz = (double) k * binHz;
                if (hz > 20000.0) break;
                const double nearest = std::round (hz / f) * f;
                if (std::abs (hz - nearest) < 6.0 * binHz) harmonic = juce::jmax (harmonic, spec[k]);
                else                                     inharmonic = juce::jmax (inharmonic, spec[k]);
            }
            expectLessThan (toDb (inharmonic / harmonic), -90.0);
        }

        beginTest ("DDS2 phase resets to zero on every note");
        {
            Dds2 osc; osc.setSampleRate (sr);
            std::vector<float> a (64), b (64);
            osc.noteOn();
            for (auto& v : a) v = osc.process (500.0, Dds2Wave::saw, 0.5f, nullptr, rng);
            for (int i = 0; i < 999; ++i) osc.process (500.0, Dds2Wave::saw, 0.5f, nullptr, rng);
            osc.noteOn();
            for (auto& v : b) v = osc.process (500.0, Dds2Wave::saw, 0.5f, nullptr, rng);
            // the first output sample is the BLEP delay line's previous value; compare the rest
            expect (std::equal (a.begin() + 1, a.end(), b.begin() + 1));
        }

        beginTest ("DDS2 pulse width sets the duty cycle");
        for (float pw : { 0.1f, 0.25f, 0.5f, 0.8f })
        {
            Dds2 osc; osc.setSampleRate (sr);
            int positive = 0;
            const int n = 176400;
            for (int i = 0; i < n; ++i)
                positive += osc.process (100.0, Dds2Wave::pulse, pw, nullptr, rng) > 0.0f ? 1 : 0;
            expectWithinAbsoluteError ((double) positive / n, (double) pw, 0.005);
        }

        beginTest ("DDS2 hard sync locks the slave to the master period");
        {
            Dds1 master; master.setWaveBank (&bank); master.setSampleRate (sr); master.resetPhases();
            Dds2 slave; slave.setSampleRate (sr);
            Dds1::Settings s; s.wave = Dds1Wave::sine;
            const double fm = 441.0; // period = 400 samples
            std::vector<float> out (40000);
            for (auto& v : out)
            {
                WrapEvent w;
                master.process (fm, s, rng, w);
                v = slave.process (fm * 2.37, Dds2Wave::saw, 0.5f, &w, rng);
            }
            double maxDiff = 0.0;
            for (size_t i = 4000; i + 400 < out.size(); ++i)
                maxDiff = juce::jmax (maxDiff, (double) std::abs (out[i] - out[i + 400]));
            expectLessThan (maxDiff, 0.02);
        }

        beginTest ("sub oscillator: one octave below DDS1, square and sine");
        for (bool sine : { false, true })
        {
            Dds1 master; master.setWaveBank (&bank); master.setSampleRate (sr);
            Dds2 sub; sub.setSampleRate (sr);
            Dds1::Settings s; s.wave = Dds1Wave::saw;
            std::vector<float> out (176400);
            for (auto& v : out)
            {
                WrapEvent w;
                master.process (220.0, s, rng, w);
                v = sub.processSub (sine, master.centroidPhase(), w);
            }
            expectWithinAbsoluteError (zeroCrossingFrequency (out, sr, 100), 110.0, 0.05);
            expectWithinAbsoluteError ((double) peak (out, 100), 1.0, sine ? 0.01 : 0.15);
        }

        beginTest ("polyBLEP lowers DDS2 pulse aliasing versus a naive pulse");
        {
            Dds2 osc; osc.setSampleRate (sr);
            const double f = 7000.0 * 1.13;
            std::vector<float> blep (1 << 16), naive (1 << 16);
            double ph = 0.0;
            for (size_t i = 0; i < blep.size(); ++i)
            {
                blep[i] = osc.process (f, Dds2Wave::pulse, 0.3f, nullptr, rng);
                naive[i] = Dds2::naive (Dds2Wave::pulse, ph, 0.3f);
                ph += f / sr; ph -= std::floor (ph);
            }
            auto inharmonicDb = [&] (const std::vector<float>& x)
            {
                auto spec = spectrum (x, 16);
                const double binHz = sr / 65536.0;
                double h = 0.0, ih = 0.0;
                for (size_t k = 2; k < spec.size() && (double) k * binHz < 20000.0; ++k)
                {
                    const double hz = (double) k * binHz;
                    if (std::abs (hz - std::round (hz / f) * f) < 6.0 * binHz) h = juce::jmax (h, spec[k]);
                    else ih = juce::jmax (ih, spec[k]);
                }
                return toDb (ih / h);
            };
            expectLessThan (inharmonicDb (blep), inharmonicDb (naive) - 15.0);
        }

        beginTest ("DDS2 LFO range maps TUNE -7..+7 to ~0.1..100 Hz");
        expectWithinAbsoluteError (rates::dds2LfoHz (-7.0f), 0.1, 1.0e-9);
        expectWithinAbsoluteError (rates::dds2LfoHz (7.0f), 100.0, 1.0e-6);
    }
};

static OscillatorTests oscillatorTests;
