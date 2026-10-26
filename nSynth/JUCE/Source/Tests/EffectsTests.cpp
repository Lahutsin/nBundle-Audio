#include "TestHelpers.h"
#include "Core/DSP/Effects.h"

using namespace nsynth;
using namespace nsynth::test;

class EffectsTests : public juce::UnitTest
{
public:
    EffectsTests() : juce::UnitTest ("Effects", "nSynth") {}

    static constexpr double sr = 48000.0;

    static StereoDelay::Settings delaySettings (double seconds, float feedback, float mix)
    {
        StereoDelay::Settings s;
        s.timeSeconds = seconds;
        s.feedback = feedback;
        s.mix = mix;
        s.lowCutHz = 20.0f;
        s.highCutHz = 20000.0f;
        return s;
    }

    static void fillNoise (juce::AudioBuffer<float>& b, int seed)
    {
        juce::Random rng (seed);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            b.setSample (0, i, rng.nextFloat() * 2.0f - 1.0f);
            b.setSample (1, i, rng.nextFloat() * 2.0f - 1.0f);
        }
    }

    static std::vector<float> channel (const juce::AudioBuffer<float>& b, int ch, int from, int to)
    {
        return { b.getReadPointer (ch) + from, b.getReadPointer (ch) + to };
    }

    void runTest() override
    {
        beginTest ("mix law: dry stays full up to 50 %, wet is full from 50 %");
        {
            expectEquals (dryGainForMix (0.0f), 1.0f);
            expectEquals (dryGainForMix (0.5f), 1.0f);
            expectEquals (dryGainForMix (1.0f), 0.0f);
            expectEquals (wetGainForMix (0.0f), 0.0f);
            expectEquals (wetGainForMix (0.25f), 0.5f);
            expectEquals (wetGainForMix (0.5f), 1.0f);
        }

        beginTest ("delay: echo arrives after TIME, MIX 0 leaves the signal dry");
        {
            StereoDelay d; d.prepare (sr, 512);
            juce::AudioBuffer<float> b (2, (int) sr);
            b.clear();
            b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
            d.process (b, 0, b.getNumSamples(), delaySettings (0.1, 0.0f, 0.5f));
            expectEquals (b.getSample (0, 0), 1.0f);
            int echoAt = -1;
            for (int i = 1; i < b.getNumSamples(); ++i)
                if (std::abs (b.getSample (0, i)) > 0.1f) { echoAt = i; break; }
            expectWithinAbsoluteError (echoAt, 4800, 1);

            StereoDelay dry; dry.prepare (sr, 512);
            juce::AudioBuffer<float> x (2, 4800);
            fillNoise (x, 1);
            juce::AudioBuffer<float> ref (x);
            dry.process (x, 0, x.getNumSamples(), delaySettings (0.01, 0.5f, 0.0f));
            for (int i = 0; i < x.getNumSamples(); ++i)
                expectEquals (x.getSample (0, i), ref.getSample (0, i));
        }

        beginTest ("delay FEEDBACK at maximum repeats forever without decay or degradation");
        {
            StereoDelay d; d.prepare (sr, 512);
            const int period = 4800;
            juce::AudioBuffer<float> b (2, period * 12);
            b.clear();
            b.setSample (0, 0, 1.0f);
            auto s = delaySettings (0.1, 1.0f, 1.0f);
            s.lowCutHz = 800.0f;   // the echo-path filters are bypassed at infinite feedback
            s.highCutHz = 2000.0f;
            d.process (b, 0, b.getNumSamples(), s);
            for (int r = 1; r <= 11; ++r)
                expectWithinAbsoluteError (b.getSample (0, r * period), 1.0f, 1.0e-5f);
        }

        beginTest ("delay FREEZE keeps the loop and stops new input from entering it");
        {
            StereoDelay d; d.prepare (sr, 512);
            juce::AudioBuffer<float> b (2, 4800 * 4);
            b.clear();
            b.setSample (0, 0, 1.0f);
            auto s = delaySettings (0.1, 0.2f, 1.0f);
            s.lowCutHz = 20.0f;
            s.highCutHz = 20000.0f;
            d.process (b, 0, 4800, s);
            b.setSample (0, 6000, 0.5f); // played while frozen
            s.freeze = true;
            d.process (b, 4800, b.getNumSamples() - 4800, s);
            expectGreaterThan (std::abs (b.getSample (0, 9600)), 0.5f);      // loop keeps sounding
            expectWithinAbsoluteError (b.getSample (0, 14400), b.getSample (0, 9600), 1.0e-5f); // at a constant level
            expectLessThan (std::abs (b.getSample (0, 6000 + 4800)), 1.0e-6f); // new note not recorded
        }

        beginTest ("delay PING-PONG alternates the echoes between left and right");
        {
            StereoDelay d; d.prepare (sr, 512);
            juce::AudioBuffer<float> b (2, 4800 * 3 + 10);
            b.clear();
            b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
            auto s = delaySettings (0.1, 0.8f, 1.0f);
            s.pingPong = true;
            d.process (b, 0, b.getNumSamples(), s);
            auto energy = [&] (int ch, int at) { return rms (channel (b, ch, at - 4, at + 8)); };
            expectGreaterThan (energy (0, 4800), 10.0 * energy (1, 4800));
            expectGreaterThan (energy (1, 9600), 10.0 * energy (0, 9600));
        }

        beginTest ("delay HIGH CUT darkens every repeat, LOW CUT thins it");
        {
            auto echoSpectrumRatio = [&] (float lowCut, float highCut)
            {
                StereoDelay d; d.prepare (sr, 512);
                juce::AudioBuffer<float> b (2, 4800 + 4096);
                b.clear();
                fillNoise (b, 9);
                for (int i = 2048; i < b.getNumSamples(); ++i) { b.setSample (0, i, 0.0f); b.setSample (1, i, 0.0f); }
                auto s = delaySettings (0.1, 0.0f, 1.0f);
                s.lowCutHz = lowCut;
                s.highCutHz = highCut;
                d.process (b, 0, b.getNumSamples(), s);
                const auto spec = spectrum (channel (b, 0, 4800, 4800 + 2048), 11);
                double low = 0.0, high = 0.0;   // below ~1 kHz / above ~8 kHz
                for (size_t k = 1; k < spec.size(); ++k)
                {
                    if (k < spec.size() / 24)
                        low += spec[k] * spec[k];
                    else if (k > spec.size() / 3)
                        high += spec[k] * spec[k];
                }
                return std::make_pair (low, high);
            };
            const auto open = echoSpectrumRatio (20.0f, 20000.0f);
            const auto dark = echoSpectrumRatio (20.0f, 1500.0f);
            const auto thin = echoSpectrumRatio (2000.0f, 20000.0f);
            expectLessThan (dark.second, 0.1 * open.second);
            expectLessThan (thin.first, 0.5 * open.first);
        }

        beginTest ("chorus OFF is transparent; I / II / I+II run 1 / 2 / 3 voices");
        {
            StereoChorus c; c.prepare (sr, 512);
            juce::AudioBuffer<float> b (2, 2048);
            fillNoise (b, 5);
            juce::AudioBuffer<float> ref (b);
            StereoChorus::Settings s;
            s.mode = ChorusMode::off;
            c.process (b, 0, b.getNumSamples(), s);
            for (int i = 0; i < b.getNumSamples(); ++i)
                expectEquals (b.getSample (1, i), ref.getSample (1, i));

            expectEquals (StereoChorus::voicesFor (ChorusMode::off), 0);
            expectEquals (StereoChorus::voicesFor (ChorusMode::one), 1);
            expectEquals (StereoChorus::voicesFor (ChorusMode::two), 2);
            expectEquals (StereoChorus::voicesFor (ChorusMode::oneTwo), 3);
        }

        beginTest ("chorus WIDTH decorrelates left and right of a mono input; width 0 stays mono");
        {
            auto sideLevel = [&] (float width, ChorusMode mode)
            {
                StereoChorus c; c.prepare (sr, 512);
                juce::AudioBuffer<float> b (2, (int) sr * 2);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float v = (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sr);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
                StereoChorus::Settings s;
                s.mode = mode;
                s.width = width;
                s.depth = 1.0f;
                s.rateHz = 1.2f;
                c.process (b, 0, b.getNumSamples(), s);
                double diff = 0.0;
                for (int i = (int) sr; i < b.getNumSamples(); ++i)
                    diff += std::abs (b.getSample (0, i) - b.getSample (1, i));
                return diff / sr;
            };
            expectGreaterThan (sideLevel (1.0f, ChorusMode::one), 0.01);
            expectGreaterThan (sideLevel (1.0f, ChorusMode::oneTwo), 0.01);
            expectLessThan (sideLevel (0.0f, ChorusMode::two), 1.0e-6);
        }

        beginTest ("chorus RATE, DEPTH and FEEDBACK change the output; MIX 0 is dry");
        {
            auto render = [&] (StereoChorus::Settings s)
            {
                StereoChorus c; c.prepare (sr, 512);
                juce::AudioBuffer<float> b (2, 24000);
                fillNoise (b, 3);
                c.process (b, 0, b.getNumSamples(), s);
                return channel (b, 0, 12000, 24000);
            };
            StereoChorus::Settings base;
            base.mode = ChorusMode::two;
            const auto a = render (base);
            for (auto modify : { +[] (StereoChorus::Settings& s) { s.rateHz = 4.0f; },
                                 +[] (StereoChorus::Settings& s) { s.depth = 0.05f; },
                                 +[] (StereoChorus::Settings& s) { s.feedback = 0.8f; } })
            {
                auto changed = base;
                modify (changed);
                const auto b = render (changed);
                double diff = 0.0;
                for (size_t i = 0; i < a.size(); ++i) diff += std::abs (a[i] - b[i]);
                expectGreaterThan (diff / (double) a.size(), 1.0e-3);
            }

            auto dry = base;
            dry.mix = 0.0f;
            StereoChorus c; c.prepare (sr, 512);
            juce::AudioBuffer<float> x (2, 4096);
            fillNoise (x, 4);
            juce::AudioBuffer<float> ref (x);
            c.process (x, 0, x.getNumSamples(), dry);
            for (int i = 0; i < x.getNumSamples(); ++i)
                expectEquals (x.getSample (0, i), ref.getSample (0, i));
        }

        beginTest ("reverb: MIX 0 is bit-transparent and idle");
        {
            StereoReverb r; r.prepare (sr, 512);
            juce::AudioBuffer<float> b (2, 4096);
            fillNoise (b, 2);
            juce::AudioBuffer<float> ref (b);
            StereoReverb::Settings s;
            r.process (b, 0, b.getNumSamples(), s);
            expect (r.isIdle());
            for (int i = 0; i < b.getNumSamples(); ++i)
                expectEquals (b.getSample (0, i), ref.getSample (0, i));
        }

        beginTest ("reverb: tail decays at the requested RT60, stays finite, PRE-DELAY postpones it");
        {
            for (float decay : { 0.8f, 3.0f })
            {
                StereoReverb r; r.prepare (sr, 512);
                juce::AudioBuffer<float> b (2, (int) sr * 2);
                b.clear();
                b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
                StereoReverb::Settings s;
                s.mix = 1.0f;
                s.decaySeconds = decay;
                s.dampingHz = 20000.0f;
                s.lowCutHz = 20.0f;
                s.preDelayMs = 0.0f;
                r.process (b, 0, b.getNumSamples(), s);
                const auto l = channel (b, 0, 0, b.getNumSamples());
                expect (allFinite (l));
                const int w = 4800;
                const double early = rms (channel (b, 0, 9600, 9600 + w));
                const double late = rms (channel (b, 0, 9600 + 24000, 9600 + 24000 + w));
                const double measuredDb = toDb (late) - toDb (early);  // 0.5 s apart
                const double expectedDb = -60.0 * 0.5 / decay;
                expectWithinAbsoluteError (measuredDb, expectedDb, 0.35 * std::abs (expectedDb) + 3.0,
                                           "decay " + juce::String (decay));
            }

            StereoReverb r; r.prepare (sr, 512);
            juce::AudioBuffer<float> b (2, 9600);
            b.clear();
            b.setSample (0, 0, 1.0f);
            StereoReverb::Settings s;
            s.mix = 1.0f;
            s.preDelayMs = 100.0f;
            r.process (b, 0, b.getNumSamples(), s);
            expectLessThan (peak (channel (b, 0, 1, 4700)), 1.0e-6f);
            expectGreaterThan (peak (channel (b, 0, 4800, 9600)), 1.0e-3f);
        }

        beginTest ("reverb: WIDTH 1 is stereo, WIDTH 0 is mono; MIX 50 % keeps the full dry signal");
        {
            auto sideToMid = [&] (float width)
            {
                StereoReverb r; r.prepare (sr, 512);
                juce::AudioBuffer<float> b (2, 24000);
                b.clear();
                b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
                StereoReverb::Settings s;
                s.mix = 1.0f;
                s.width = width;
                r.process (b, 0, b.getNumSamples(), s);
                double side = 0.0, mid = 0.0;
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    side += std::abs (b.getSample (0, i) - b.getSample (1, i));
                    mid += std::abs (b.getSample (0, i) + b.getSample (1, i));
                }
                return side / mid;
            };
            expectGreaterThan (sideToMid (1.0f), 0.2);
            expectLessThan (sideToMid (0.0f), 1.0e-6);

            StereoReverb r; r.prepare (sr, 512);
            juce::AudioBuffer<float> b (2, 64);
            b.clear();
            b.setSample (0, 0, 0.75f);
            StereoReverb::Settings s;
            s.mix = 0.5f;
            s.preDelayMs = 50.0f;
            r.process (b, 0, b.getNumSamples(), s);
            expectEquals (b.getSample (0, 0), 0.75f);
        }

        beginTest ("air shelf: flat in the mids, a gentle lift at the top");
        {
            AirShelf air;
            air.prepare (sr);
            auto gainDb = [&] (double hz)
            {
                air.reset();
                juce::AudioBuffer<float> b (2, 16384);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float v = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sr);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
                air.process (b, 0, b.getNumSamples());
                return toDb (toneAmplitude (channel (b, 0, 0, b.getNumSamples()), hz, sr, 4096) / 0.5);
            };
            expectWithinAbsoluteError (gainDb (200.0), 0.0, 0.1);
            expectWithinAbsoluteError (gainDb (1000.0), 0.0, 0.2);
            expectWithinAbsoluteError (gainDb (16000.0), (double) AirShelf::kGainDb, 0.5);
            expectGreaterThan (gainDb (9000.0), 0.5);
        }
    }
};

static EffectsTests effectsTests;
