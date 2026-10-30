#include "TestHelpers.h"
#include "Core/DSP/VoiceFilter.h"
#include "Core/DSP/Halfband.h"

using namespace nsynth;
using namespace nsynth::test;

class FilterTests : public juce::UnitTest
{
public:
    FilterTests() : juce::UnitTest ("Filter", "nSynth") {}

    static constexpr double sr = 176400.0;

    double gainAt (double freq, float hpf, float lpf, float res, Drive d = Drive::off, float amp = 0.02f,
                   FilterType type = FilterType::ladder24)
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
        static constexpr FilterType allTypes[] = { FilterType::ladder24, FilterType::ladder12, FilterType::cascade24, FilterType::svf12 };

        beginTest ("Ladder 24 / Cascade 24: flat passband, -12 dB at cutoff, -24 dB/oct above");
        for (auto type : { FilterType::ladder24, FilterType::cascade24 })
        {
            const float fc = 1000.0f;
            expectWithinAbsoluteError (toDb (gainAt (50.0, 10.0f, fc, 0.0f, Drive::off, 0.02f, type)), 0.0, 0.5);
            expectWithinAbsoluteError (toDb (gainAt (1000.0, 10.0f, fc, 0.0f, Drive::off, 0.02f, type)), -12.0, 0.6);
            const double slope = toDb (gainAt (16000.0, 10.0f, fc, 0.0f, Drive::off, 0.02f, type))
                               - toDb (gainAt (8000.0, 10.0f, fc, 0.0f, Drive::off, 0.02f, type));
            expectWithinAbsoluteError (slope, -24.0, 1.5);
        }

        beginTest ("Ladder 12 / SVF 12: flat passband and -12 dB/oct above the cutoff");
        for (auto type : { FilterType::ladder12, FilterType::svf12 })
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
        for (auto type : { FilterType::ladder24, FilterType::cascade24, FilterType::svf12 })
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

        beginTest ("resonance keeps the low end: ladder partly compensated, SVF without loss");
        {
            auto loss = [&] (FilterType type)
            {
                return toDb (gainAt (100.0, 10.0f, 2000.0f, 0.9f, Drive::off, 0.02f, type))
                     - toDb (gainAt (100.0, 10.0f, 2000.0f, 0.0f, Drive::off, 0.02f, type));
            };
            const double ladder = loss (FilterType::ladder24);
            expectLessThan (ladder, -2.0);       // still a ladder: some thinning with resonance
            expectGreaterThan (ladder, -8.0);    // ... but far less than the uncompensated -13.5 dB
            expectGreaterThan (loss (FilterType::cascade24), -8.0);
            expectWithinAbsoluteError (loss (FilterType::svf12), 0.0, 1.5);
        }

        beginTest ("2x half-band: flat to 20 kHz, -85 dB from fs - 20 kHz (folds back above 20 kHz)");
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto design = Halfband::designFor (rate);
            expect (design.count > 0 && design.count <= Halfband::kMaxCoefficients);
            const double passEdge = juce::jmin (20000.0, 0.45 * rate), stopEdge = juce::jmax (rate - 20000.0, 0.55 * rate);
            double worstPass = 0.0, worstStop = -300.0;
            for (double f = 0.0; f <= passEdge; f += 25.0)
                worstPass = juce::jmax (worstPass, std::abs (Halfband::responseDb (design, f, rate)));
            for (double f = stopEdge; f <= rate; f += 25.0)
                worstStop = juce::jmax (worstStop, Halfband::responseDb (design, f, rate));
            expectLessThan (worstPass, 0.01, "rate " + juce::String (rate));
            expectLessThan (worstStop, -85.0, "rate " + juce::String (rate));
        }

        beginTest ("2x half-band: up then down keeps a tone; down-sampling suppresses what would fold back");
        {
            const double rate = 48000.0;
            const auto design = Halfband::designFor (rate);
            HalfbandResampler up, down;
            up.setDesign (design);
            down.setDesign (design);
            std::vector<float> out (1 << 15);
            for (size_t i = 0; i < out.size(); ++i)
            {
                float a = 0.0f, b = 0.0f;
                up.up (0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 3000.0 * (double) i / rate), a, b);
                out[i] = down.down (a, b);
            }
            expectWithinAbsoluteError (toneAmplitude (out, 3000.0, rate, 4096), 0.5, 0.001);

            // 30 kHz at the doubled rate would fold back to 18 kHz.
            HalfbandResampler decimator;
            decimator.setDesign (design);
            for (size_t i = 0; i < out.size(); ++i)
            {
                auto tone = [] (size_t n) { return 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 30000.0 * (double) n / 96000.0); };
                out[i] = decimator.down (tone (2 * i), tone (2 * i + 1));
            }
            expectLessThan (toDb (toneAmplitude (out, 18000.0, rate, 4096) / 0.5), -85.0);
        }
    }
};

static FilterTests filterTests;
