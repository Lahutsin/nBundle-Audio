#include "TestHelpers.h"
#include "Core/DSP/VoiceFilter.h"

using namespace nsynth;
using namespace nsynth::test;

class FilterTests : public juce::UnitTest
{
public:
    FilterTests() : juce::UnitTest ("Filter", "nSynth") {}

    static constexpr double sr = 176400.0;

    double gainAt (double freq, float hpf, float lpf, float res, Drive d = Drive::off, float amp = 0.1f)
    {
        VoiceFilter f; f.setSampleRate (sr); f.setParameters (hpf, lpf, res, d);
        const int n = (int) sr;
        std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = f.process (amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr));
        return toneAmplitude (out, freq, sr, (size_t) n / 2) / amp;
    }

    void runTest() override
    {
        beginTest ("4-pole LPF: flat passband, -12 dB at cutoff, -24 dB/oct above");
        {
            const float fc = 1000.0f;
            expectWithinAbsoluteError (toDb (gainAt (50.0, 10.0f, fc, 0.0f)), 0.0, 0.5);
            expectWithinAbsoluteError (toDb (gainAt (1000.0, 10.0f, fc, 0.0f)), -12.0, 0.5);
            const double slope = toDb (gainAt (16000.0, 10.0f, fc, 0.0f)) - toDb (gainAt (8000.0, 10.0f, fc, 0.0f));
            expectWithinAbsoluteError (slope, -24.0, 1.5);
        }

        beginTest ("1-pole HPF: -3 dB at cutoff, 6 dB/oct below");
        {
            const float fc = 1000.0f;
            expectWithinAbsoluteError (toDb (gainAt (1000.0, fc, 25000.0f, 0.0f)), -3.0, 0.3);
            const double slope = toDb (gainAt (125.0, fc, 25000.0f, 0.0f)) - toDb (gainAt (62.5, fc, 25000.0f, 0.0f));
            expectWithinAbsoluteError (slope, 6.0, 0.5);
        }

        beginTest ("RES at maximum self-oscillates at the cutoff frequency (sine-like)");
        for (float fc : { 220.0f, 1000.0f, 4000.0f })
        {
            VoiceFilter f; f.setSampleRate (sr); f.setParameters (10.0f, fc, 1.0f, Drive::off);
            std::vector<float> out ((size_t) sr * 2);
            for (auto& v : out) v = f.process (0.0f);
            expectGreaterThan (rms (out, out.size() / 2), 0.1);
            expectWithinAbsoluteError (zeroCrossingFrequency (out, sr, out.size() / 2) / fc, 1.0, 0.03);
            expect (allFinite (out));
        }

        beginTest ("stability under extreme, fast modulation");
        {
            VoiceFilter f; f.setSampleRate (sr);
            juce::Random rng (3);
            std::vector<float> out ((size_t) sr * 3);
            for (size_t i = 0; i < out.size(); ++i)
            {
                if (i % 16 == 0)
                    f.setParameters (10.0f + rng.nextFloat() * 4000.0f, 15.0f * std::pow (1600.0f, rng.nextFloat()), rng.nextFloat(),
                                     (Drive) rng.nextInt (3));
                out[i] = f.process (rng.nextFloat() * 4.0f - 2.0f);
            }
            expect (allFinite (out));
            expectLessThan (peak (out), 8.0f);
        }

        beginTest ("DRIVE 1 and 2 add progressively more harmonics");
        {
            auto thd = [&] (Drive d)
            {
                VoiceFilter f; f.setSampleRate (sr); f.setParameters (10.0f, 20000.0f, 0.0f, d);
                std::vector<float> out ((size_t) sr);
                for (size_t i = 0; i < out.size(); ++i)
                    out[i] = f.process (0.8f * (float) std::sin (juce::MathConstants<double>::twoPi * 200.0 * (double) i / sr));
                const double h1 = toneAmplitude (out, 200.0, sr, out.size() / 2);
                double hs = 0.0;
                for (int k = 2; k <= 7; ++k) hs += std::pow (toneAmplitude (out, 200.0 * k, sr, out.size() / 2), 2.0);
                return std::sqrt (hs) / h1;
            };
            const double off = thd (Drive::off), one = thd (Drive::one), two = thd (Drive::two);
            expectLessThan (off, 0.001);
            expectGreaterThan (one, off * 10.0);
            expectGreaterThan (two, one * 2.0);
        }

        beginTest ("DRIVE 1 compensates the passband loss caused by resonance");
        {
            const double offLoss = toDb (gainAt (100.0, 10.0f, 2000.0f, 0.9f, Drive::off)) - toDb (gainAt (100.0, 10.0f, 2000.0f, 0.0f, Drive::off));
            const double oneLoss = toDb (gainAt (100.0, 10.0f, 2000.0f, 0.9f, Drive::one, 0.05f)) - toDb (gainAt (100.0, 10.0f, 2000.0f, 0.0f, Drive::one, 0.05f));
            expectLessThan (offLoss, -6.0);
            expectGreaterThan (oneLoss, -3.0);
        }
    }
};

static FilterTests filterTests;
