#include "TestHelpers.h"
#include "Core/DSP/Effects.h"

using namespace nsynth;
using namespace nsynth::test;

class EffectsTests : public juce::UnitTest
{
public:
    EffectsTests() : juce::UnitTest ("Effects", "nSynth") {}

    static constexpr double sr = 48000.0;

    void runTest() override
    {
        beginTest ("delay: echo arrives after TIME, SEND 0 leaves the signal dry");
        {
            StereoDelay d; d.prepare (sr, 512);
            juce::AudioBuffer<float> b (2, (int) sr);
            b.clear();
            b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
            d.process (b, 0, b.getNumSamples(), 0.1, 1.0f, 0.0f, false);
            expectEquals (b.getSample (0, 0), 1.0f);
            int echoAt = -1;
            for (int i = 1; i < b.getNumSamples(); ++i)
                if (std::abs (b.getSample (0, i)) > 0.1f) { echoAt = i; break; }
            expectWithinAbsoluteError (echoAt, 4800, 1);

            StereoDelay dry; dry.prepare (sr, 512);
            juce::AudioBuffer<float> x (2, 4800);
            juce::Random rng (1);
            for (int i = 0; i < x.getNumSamples(); ++i) { x.setSample (0, i, rng.nextFloat()); x.setSample (1, i, rng.nextFloat()); }
            juce::AudioBuffer<float> ref (x);
            dry.process (x, 0, x.getNumSamples(), 0.01, 0.0f, 0.5f, false);
            for (int i = 0; i < x.getNumSamples(); ++i)
                expectEquals (x.getSample (0, i), ref.getSample (0, i));
        }

        beginTest ("FEEDBACK at maximum repeats forever without decay or degradation");
        {
            StereoDelay d; d.prepare (sr, 512);
            const int period = 4800;
            juce::AudioBuffer<float> b (2, period * 12);
            b.clear();
            b.setSample (0, 0, 1.0f);
            d.process (b, 0, 1, 0.1, 1.0f, 1.0f, false);
            d.process (b, 1, b.getNumSamples() - 1, 0.1, 0.0f, 1.0f, false);
            for (int r = 1; r <= 11; ++r)
                expectWithinAbsoluteError (b.getSample (0, r * period), 1.0f, 1.0e-5f);
        }

        beginTest ("FREEZE keeps the loop and stops new input from entering it");
        {
            StereoDelay d; d.prepare (sr, 512);
            juce::AudioBuffer<float> b (2, 4800 * 4);
            b.clear();
            b.setSample (0, 0, 1.0f);
            d.process (b, 0, 4800, 0.1, 1.0f, 0.2f, false);
            b.setSample (0, 6000, 0.5f); // played while frozen
            d.process (b, 4800, b.getNumSamples() - 4800, 0.1, 1.0f, 0.2f, true);
            expectWithinAbsoluteError (b.getSample (0, 9600), 1.0f, 1.0e-4f); // loop keeps full level
            expectLessThan (std::abs (b.getSample (0, 6000 + 4800)), 1.0e-6f); // new note not recorded
        }

        beginTest ("chorus OFF is transparent; modes use the specified LFO rates");
        {
            StereoChorus c; c.prepare (sr, 512);
            juce::AudioBuffer<float> b (2, 2048);
            juce::Random rng (5);
            for (int i = 0; i < b.getNumSamples(); ++i) { b.setSample (0, i, rng.nextFloat()); b.setSample (1, i, rng.nextFloat()); }
            juce::AudioBuffer<float> ref (b);
            c.process (b, 0, b.getNumSamples(), ChorusMode::off);
            for (int i = 0; i < b.getNumSamples(); ++i)
                expectEquals (b.getSample (1, i), ref.getSample (1, i));

            expectEquals (StereoChorus::specFor (ChorusMode::one).rateHz, 0.513);
            expectEquals (StereoChorus::specFor (ChorusMode::two).rateHz, 0.863);
            expectGreaterThan (StereoChorus::specFor (ChorusMode::oneTwo).rateHz, 5.0);
        }

        beginTest ("chorus ON decorrelates left and right of a mono input");
        {
            StereoChorus c; c.prepare (sr, 512);
            juce::AudioBuffer<float> b (2, (int) sr * 2);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sr);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
            c.process (b, 0, b.getNumSamples(), ChorusMode::two);
            double diff = 0.0;
            for (int i = (int) sr; i < b.getNumSamples(); ++i)
                diff += std::abs (b.getSample (0, i) - b.getSample (1, i));
            expectGreaterThan (diff / sr, 0.01);
        }
    }
};

static EffectsTests effectsTests;
