#include "TestHelpers.h"
#include "Core/DSP/VoiceFilter.h"

using namespace nsynth;
using namespace nsynth::test;

class FilterTests : public juce::UnitTest
{
public:
    FilterTests() : juce::UnitTest ("Filter", "nSynth") {}

    static constexpr double sr = 176400.0;

    double gainAt (double freq, float hpf, float lpf, float res, Drive d = Drive::off, float amp = 0.02f,
                   FilterType type = FilterType::moog24)
    {
        VoiceFilter f; f.setSampleRate (sr); f.setParameters (hpf, lpf, res, d, type);
        const int n = (int) sr;
        std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = f.process (amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr));
        return toneAmplitude (out, freq, sr, (size_t) n / 2) / amp;
    }

    double thd (FilterType type, Drive d, float amp, float cutoff = 20000.0f)
    {
        VoiceFilter f; f.setSampleRate (sr); f.setParameters (10.0f, cutoff, 0.0f, d, type);
        std::vector<float> out ((size_t) sr);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = f.process (amp * (float) std::sin (juce::MathConstants<double>::twoPi * 200.0 * (double) i / sr));
        const double h1 = toneAmplitude (out, 200.0, sr, out.size() / 2);
        double hs = 0.0;
        for (int k = 2; k <= 7; ++k) hs += std::pow (toneAmplitude (out, 200.0 * k, sr, out.size() / 2), 2.0);
        return std::sqrt (hs) / h1;
    }

    void runTest() override
    {
        static constexpr FilterType allTypes[] = { FilterType::moog24, FilterType::moog12, FilterType::jupiter24, FilterType::sem12 };

        beginTest ("Moog 24 / Jupiter 24: flat passband, -12 dB at cutoff, -24 dB/oct above");
        for (auto type : { FilterType::moog24, FilterType::jupiter24 })
        {
            const float fc = 1000.0f;
            expectWithinAbsoluteError (toDb (gainAt (50.0, 10.0f, fc, 0.0f, Drive::off, 0.02f, type)), 0.0, 0.5);
            expectWithinAbsoluteError (toDb (gainAt (1000.0, 10.0f, fc, 0.0f, Drive::off, 0.02f, type)), -12.0, 0.6);
            const double slope = toDb (gainAt (16000.0, 10.0f, fc, 0.0f, Drive::off, 0.02f, type))
                               - toDb (gainAt (8000.0, 10.0f, fc, 0.0f, Drive::off, 0.02f, type));
            expectWithinAbsoluteError (slope, -24.0, 1.5);
        }

        beginTest ("Moog 12 / SEM 12: flat passband and -12 dB/oct above the cutoff");
        for (auto type : { FilterType::moog12, FilterType::sem12 })
        {
            const float fc = 1000.0f;
            expectWithinAbsoluteError (toDb (gainAt (60.0, 10.0f, fc, 0.0f, Drive::off, 0.02f, type)), 0.0, 0.5);
            const double slope = toDb (gainAt (16000.0, 10.0f, fc, 0.0f, Drive::off, 0.02f, type))
                               - toDb (gainAt (8000.0, 10.0f, fc, 0.0f, Drive::off, 0.02f, type));
            expectWithinAbsoluteError (slope, -12.0, 1.5);
        }

        beginTest ("1-pole HPF: -3 dB at cutoff, 6 dB/oct below");
        {
            const float fc = 1000.0f;
            expectWithinAbsoluteError (toDb (gainAt (1000.0, fc, 25000.0f, 0.0f)), -3.0, 0.3);
            const double slope = toDb (gainAt (125.0, fc, 25000.0f, 0.0f)) - toDb (gainAt (62.5, fc, 25000.0f, 0.0f));
            expectWithinAbsoluteError (slope, 6.0, 0.5);
        }

        beginTest ("RES at maximum self-oscillates at the cutoff frequency (sine-like), every type");
        for (auto type : { FilterType::moog24, FilterType::jupiter24, FilterType::sem12 })
            for (float fc : { 220.0f, 1000.0f, 4000.0f })
            {
                VoiceFilter f; f.setSampleRate (sr); f.setParameters (10.0f, fc, 1.0f, Drive::off, type);
                std::vector<float> out ((size_t) sr * 2);
                for (auto& v : out) v = f.process (0.0f);
                expectGreaterThan (rms (out, out.size() / 2), 0.1);
                expectWithinAbsoluteError (zeroCrossingFrequency (out, sr, out.size() / 2) / fc, 1.0, 0.04);
                expect (allFinite (out));
            }

        beginTest ("stability under extreme, fast modulation, every type");
        for (auto type : allTypes)
        {
            VoiceFilter f; f.setSampleRate (sr);
            juce::Random rng (3);
            std::vector<float> out ((size_t) sr * 3);
            for (size_t i = 0; i < out.size(); ++i)
            {
                if (i % 16 == 0)
                    f.setParameters (10.0f + rng.nextFloat() * 4000.0f, 15.0f * std::pow (1600.0f, rng.nextFloat()), rng.nextFloat(),
                                     (Drive) rng.nextInt (3), type);
                out[i] = f.process (rng.nextFloat() * 4.0f - 2.0f);
            }
            expect (allFinite (out));
            expectLessThan (peak (out), 8.0f);
        }

        beginTest ("the ladder saturates like analog: soft at low level, DRIVE 1 and 2 add progressively more harmonics");
        for (auto type : allTypes)
        {
            expectLessThan (thd (type, Drive::off, 0.05f), 0.005);
            const double off = thd (type, Drive::off, 0.8f), one = thd (type, Drive::one, 0.8f), two = thd (type, Drive::two, 0.8f);
            expectGreaterThan (off, 0.001);
            expectGreaterThan (one, off * 2.0);
            expectGreaterThan (two, one * 1.5);
        }

        beginTest ("resonance keeps the low end: ladder partly compensated, SEM without loss");
        {
            auto loss = [&] (FilterType type)
            {
                return toDb (gainAt (100.0, 10.0f, 2000.0f, 0.9f, Drive::off, 0.02f, type))
                     - toDb (gainAt (100.0, 10.0f, 2000.0f, 0.0f, Drive::off, 0.02f, type));
            };
            const double moog = loss (FilterType::moog24);
            expectLessThan (moog, -2.0);       // still a ladder: some thinning with resonance
            expectGreaterThan (moog, -8.0);    // ... but far less than the uncompensated -13.5 dB
            expectGreaterThan (loss (FilterType::jupiter24), -8.0);
            expectWithinAbsoluteError (loss (FilterType::sem12), 0.0, 1.5);
        }
    }
};

static FilterTests filterTests;
